#include "lspakextractor.h"
#include <format>
#include <fstream>
#include <iostream>
#include <lz4.h>
#include <vector>
#include <zstd.h>
#include <zlib.h>

namespace sfs = std::filesystem;


LsPakExtractor::LsPakExtractor(const sfs::path& source_path) : source_path_(source_path) {}

void LsPakExtractor::init()
{
  std::ifstream file(source_path_, std::ios::binary);
  if(!file)
    throw std::runtime_error(std::format("Failed to open archive: {}", source_path_.string()));

  // Determine the actual file size before trusting any header fields.
  file.seekg(0, std::ios::end);
  const uint64_t archive_size = static_cast<uint64_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  if(archive_size < sizeof(LsPakHeader))
    throw std::runtime_error(
      std::format("Archive too small to contain a valid header ({} bytes).", archive_size));

  header_ = std::make_unique<LsPakHeader>();
  file.read(reinterpret_cast<char*>(header_.get()), sizeof(LsPakHeader));

  if(static_cast<unsigned int>(header_->magic_number) != LS_PAK_MAGIC_HEADER_NUMBER)
    throw std::runtime_error(std::format("Unknown file format with magic number: {}",
                                         static_cast<unsigned int>(header_->magic_number)));
  if(static_cast<unsigned int>(header_->version) != LS_PAK_SUPPORTED_VERSION)
  {
    throw std::runtime_error(
      std::format("Unsupported file version: {}", static_cast<unsigned int>(header_->version)));
  }

  // Validate file_list_offset before using it.
  // Copy the packed field to a local to avoid taking a reference to a misaligned member.
  const uint64_t file_list_offset = header_->file_list_offset;
  if(file_list_offset < sizeof(LsPakHeader) || file_list_offset >= archive_size)
    throw std::runtime_error(
      std::format("file_list_offset ({}) is out of bounds for archive of size {}.",
                  file_list_offset, archive_size));

  auto compressed_size = readFileList(archive_size);
  if(compressed_size + 8 != header_->file_list_size)
  {
    throw std::runtime_error(std::format("Mismatch for file list size! Expected {}, found {}.",
                                         static_cast<unsigned int>(header_->file_list_size - 8),
                                         compressed_size));
  }
}

std::string LsPakExtractor::extractData(unsigned long offset,
                                        unsigned int length,
                                        unsigned int uncompressed_size,
                                        int compression_type)
{
  // this is used to extract xml files; they should never exceed 1GiB
  if(uncompressed_size > 1u << 30)
    throw std::runtime_error(std::format("Uncompressed file size is too large: {}B.", uncompressed_size));

  std::ifstream file(source_path_, std::ios::binary);
  if(!file)
    throw std::runtime_error(std::format("Failed to open archive: {}", source_path_.string()));

  file.seekg(0, std::ios::end);
  const uint64_t archive_size = static_cast<uint64_t>(file.tellg());

  // Validate that [offset, offset+length) lies within the archive.
  if(static_cast<uint64_t>(offset) >= archive_size ||
     static_cast<uint64_t>(length) > archive_size - static_cast<uint64_t>(offset))
    throw std::runtime_error(
      std::format("Data region [offset={}, length={}] exceeds archive size {}.",
                  offset, length, archive_size));

  std::vector<char> input_buffer(length);
  file.seekg(offset);
  file.read(input_buffer.data(), length);

  if(compression_type == COMPRESSION_NONE)
    return { input_buffer.data(), length };
  else if(compression_type == COMPRESSION_LZ4)
  {
    std::vector<char> output_buffer(uncompressed_size);
    int ret_code = LZ4_decompress_safe_partial(
      input_buffer.data(), output_buffer.data(), length, uncompressed_size, uncompressed_size);
    if(ret_code < 0)
      throw std::runtime_error(std::format("LZ4 decompression failed with code: {}", ret_code));

    return { output_buffer.data(), uncompressed_size };
  }
  else if(compression_type == COMPRESSION_ZSTD)
  {
    std::vector<char> output_buffer(uncompressed_size);
    const size_t actual_size = ZSTD_decompress(reinterpret_cast<void*>(output_buffer.data()),
                                               uncompressed_size,
                                               reinterpret_cast<const void*>(input_buffer.data()),
                                               input_buffer.size());
    if(ZSTD_isError(actual_size))
      throw std::runtime_error(std::format("zstd decompression failed with code: {}", actual_size));
    return { output_buffer.data(), uncompressed_size };
  }
  else if(compression_type == COMPRESSION_ZLIB)
  {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    if (inflateInit(&stream) != Z_OK)
      throw std::runtime_error("zlib initialization failed.");
    stream.avail_in = input_buffer.size();
    stream.next_in = reinterpret_cast<Bytef*>(input_buffer.data());

    std::vector<char> output_buffer(uncompressed_size);
    stream.avail_out = uncompressed_size;
    stream.next_out = reinterpret_cast<Bytef*>(output_buffer.data());
    inflateInit(&stream);
    int code = inflate(&stream, Z_NO_FLUSH);
    inflateEnd(&stream);
    if(code < 0)
      throw std::runtime_error(std::format("zlib decompression failed with code: {}", code));
    return { output_buffer.data(), uncompressed_size };
  }
  else
    throw std::runtime_error(std::format("Unsopported compression type: {}", compression_type));
}

std::vector<std::filesystem::path> LsPakExtractor::getFileList()
{
  std::vector<sfs::path> path_list;
  for(const auto& f : file_list_)
    path_list.emplace_back(f.path);
  return path_list;
}

std::string LsPakExtractor::extractFile(int file_id)
{
  const auto& file = file_list_[file_id];
  return extractData(
    file.offset, file.compressed_size, file.uncompressed_size, file.flags & COMPRESSION_MASK);
}

unsigned int LsPakExtractor::readFileList(uint64_t archive_size)
{
  // Maximum sane number of file entries: prevents multiplication overflow and
  // unreasonable memory allocation.  A single .pak with more than 1 M entries
  // is implausible for any BG3 release.
  static constexpr uint32_t MAX_FILE_ENTRIES = 1'000'000u;

  std::ifstream file(source_path_, std::ios::binary);
  if(!file)
    throw std::runtime_error(std::format("Failed to open archive: {}", source_path_.string()));

  // file_list_offset was already validated in init(); the region must also
  // contain the 8-byte (num_files + compressed_size) prefix.
  // Copy the packed field to a local to avoid taking a reference to a misaligned member.
  const uint64_t file_list_offset = header_->file_list_offset;
  if(archive_size - file_list_offset < 8u)
    throw std::runtime_error(
      std::format("file_list_offset ({}) leaves no room for the file-list header.",
                  file_list_offset));

  file.seekg(static_cast<std::streamoff>(file_list_offset));

  std::vector<char> buffer(4);
  file.read(buffer.data(), 4);
  uint32_t num_files = *reinterpret_cast<uint32_t*>(buffer.data());
  file.read(buffer.data(), 4);
  uint32_t compressed_size = *reinterpret_cast<uint32_t*>(buffer.data());

  // Reject implausibly large entry counts.
  if(num_files > MAX_FILE_ENTRIES)
    throw std::runtime_error(
      std::format("Entry count {} exceeds maximum allowed ({}).", num_files, MAX_FILE_ENTRIES));

  // Check that the computed uncompressed list size does not overflow.
  // sizeof(LsPakFileListEntry) is a compile-time constant; the multiplication
  // is done in uint64_t to avoid 32-bit overflow.
  const uint64_t uncompressed_list_size =
    static_cast<uint64_t>(sizeof(LsPakFileListEntry)) * static_cast<uint64_t>(num_files);
  if(uncompressed_list_size > static_cast<uint64_t>(1u << 30))
    throw std::runtime_error(
      std::format("Computed file-list uncompressed size ({}) is too large.", uncompressed_list_size));

  // Validate that the compressed blob fits inside the archive.
  const uint64_t data_offset = static_cast<uint64_t>(file.tellg());
  if(data_offset >= archive_size ||
     static_cast<uint64_t>(compressed_size) > archive_size - data_offset)
    throw std::runtime_error(
      std::format("Compressed file list (offset={}, size={}) exceeds archive size {}.",
                  data_offset, compressed_size, archive_size));

  std::string data = extractData(
    data_offset,
    compressed_size,
    static_cast<unsigned int>(uncompressed_list_size),
    COMPRESSION_LZ4);

  // Validate that decompressed data is exactly the expected size before
  // casting individual entries out of it.
  if(data.size() != uncompressed_list_size)
    throw std::runtime_error(
      std::format("Decompressed file list size ({}) does not match expected ({}).",
                  data.size(), uncompressed_list_size));

  file_list_.clear();
  file_list_.reserve(num_files);
  for(uint32_t i = 0; i < num_files; ++i)
  {
    const uint64_t entry_offset = static_cast<uint64_t>(i) * sizeof(LsPakFileListEntry);
    LsPakFileListEntry entry =
      *reinterpret_cast<const LsPakFileListEntry*>(data.data() + entry_offset);

    // Validate that each entry's data region lies within the archive.
    // Copy packed fields to locals to avoid taking references to misaligned members.
    const uint64_t entry_file_offset = entry.offset;
    const uint32_t entry_csize = entry.compressed_size;
    const uint64_t entry_data_end = entry_file_offset + static_cast<uint64_t>(entry_csize);
    if(entry_csize > 0 &&
       (entry_file_offset >= archive_size || entry_data_end > archive_size))
      throw std::runtime_error(
        std::format("File list entry {} has data region [offset={}, size={}] that exceeds "
                    "archive size {}.",
                    i, entry_file_offset, entry_csize, archive_size));

    file_list_.push_back(entry);
  }
  return compressed_size;
}
