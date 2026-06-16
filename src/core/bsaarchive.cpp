// fork #201: BSA/BA2 archive browser & extractor

#include "bsaarchive.h"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

namespace sfs = std::filesystem;

namespace
{
// Reads a fixed-size little-endian unsigned integer from a binary stream.
// On any read failure (EOF, error) throws to keep parsing safe.
template<typename T>
T readLE(std::ifstream& file)
{
  static_assert(std::is_unsigned_v<T>, "readLE only handles unsigned integers");
  unsigned char buffer[sizeof(T)];
  file.read(reinterpret_cast<char*>(buffer), sizeof(T));
  if(!file)
    throw std::runtime_error("Unexpected end of archive while reading header data.");
  T value = 0;
  for(size_t i = 0; i < sizeof(T); ++i)
    value |= static_cast<T>(buffer[i]) << (8 * i);
  return value;
}

// Reads exactly count bytes into a vector, throwing if fewer are available.
std::vector<char> readBytes(std::ifstream& file, size_t count)
{
  std::vector<char> buffer(count);
  if(count > 0)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(count));
    if(!file)
      throw std::runtime_error("Unexpected end of archive while reading data block.");
  }
  return buffer;
}

// Inflates a zlib (deflate) stream into a buffer of the given expected size.
std::vector<char> inflateZlib(const std::vector<char>& input, uint64_t expected_size)
{
  if(expected_size == 0 || expected_size > (1ull << 32))
    throw std::runtime_error("Refusing to decompress entry with implausible size.");

  z_stream stream{};
  if(inflateInit(&stream) != Z_OK)
    throw std::runtime_error("zlib initialization failed.");

  std::vector<char> output(expected_size);
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_out = static_cast<uInt>(output.size());
  stream.next_out = reinterpret_cast<Bytef*>(output.data());

  const int code = inflate(&stream, Z_FINISH);
  const uint64_t produced = static_cast<uint64_t>(stream.total_out);
  inflateEnd(&stream);
  if(code != Z_STREAM_END)
    throw std::runtime_error("zlib decompression failed.");
  if(produced != expected_size)
    throw std::runtime_error("zlib decompression produced unexpected size.");
  return output;
}

// Replaces backslashes with forward slashes for display/extraction.
std::string normalizePath(std::string path)
{
  for(char& c : path)
    if(c == '\\')
      c = '/';
  return path;
}
} // namespace


BsaArchive::BsaArchive(const sfs::path& path) : path_(path)
{
  std::ifstream file(path_, std::ios::binary);
  if(!file)
    throw std::runtime_error("Failed to open archive: " + path_.string());

  file.seekg(0, std::ios::end);
  file_size_ = static_cast<uint64_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  if(file_size_ < 12)
    throw std::runtime_error("File is too small to be a valid BSA/BA2 archive.");

  char magic[4];
  file.read(magic, 4);
  if(!file)
    throw std::runtime_error("Failed to read archive magic.");

  if(std::memcmp(magic, "BSA\0", 4) == 0)
  {
    file.seekg(0, std::ios::beg);
    parseBsa(file);
  }
  else if(std::memcmp(magic, "BTDX", 4) == 0)
  {
    file.seekg(0, std::ios::beg);
    parseBa2(file);
  }
  else
  {
    throw std::runtime_error("Unrecognised archive format (not a BSA or BA2 file).");
  }
}

std::string BsaArchive::formatName() const
{
  switch(format_)
  {
    case Format::Bsa:
      return "BSA v" + std::to_string(version_);
    case Format::Ba2Gnrl:
      return "BA2 GNRL (general)";
    case Format::Ba2Dx10:
      return "BA2 DX10 (texture)";
    default:
      return "Unknown";
  }
}

void BsaArchive::parseBsa(std::ifstream& file)
{
  format_ = Format::Bsa;

  // Header: magic(4), version(4), folder_records_offset(4), archive_flags(4),
  // folder_count(4), file_count(4), total_folder_name_length(4),
  // total_file_name_length(4), file_flags(4).
  file.seekg(4, std::ios::beg);
  version_ = readLE<uint32_t>(file);
  if(version_ != 103 && version_ != 104 && version_ != 105)
    throw std::runtime_error("Unsupported BSA version: " + std::to_string(version_) +
                             " (supported: 103, 104, 105).");

  const uint32_t folder_records_offset = readLE<uint32_t>(file);
  const uint32_t archive_flags = readLE<uint32_t>(file);
  const uint32_t folder_count = readLE<uint32_t>(file);
  const uint32_t file_count = readLE<uint32_t>(file);
  readLE<uint32_t>(file); // total_folder_name_length (unused; we read inline lengths)
  readLE<uint32_t>(file); // total_file_name_length
  readLE<uint32_t>(file); // file_flags

  // Sanity limits to prevent absurd allocations from corrupt headers.
  static constexpr uint32_t MAX_RECORDS = 5'000'000u;
  if(folder_count > MAX_RECORDS || file_count > MAX_RECORDS)
    throw std::runtime_error("BSA folder/file count exceeds sane limits.");
  if(folder_records_offset >= file_size_)
    throw std::runtime_error("BSA folder records offset out of bounds.");

  // Bit 0x1: archive contains directory names. Bit 0x2: contains file names.
  // Bit 0x100 (Skyrim SE flag in file_flags) is not relevant here.
  // Bit 0x4: default-compressed entries. Per-file 0x40000000 bit inverts this.
  const bool default_compressed = (archive_flags & 0x4) != 0;
  // Bit 0x100: embedded file names prefixed to the data (Skyrim/FO3+).
  const bool embedded_names = (archive_flags & 0x100) != 0;

  // SSE (v105) uses a 24-byte folder record (with 64-bit offset); older
  // versions use 16 bytes. File records are always 16 bytes.
  const bool sse = version_ == 105;

  struct FolderInfo
  {
    uint32_t count = 0;
    uint64_t offset = 0; // byte offset into file-record block (after total_file_name_length)
  };
  std::vector<FolderInfo> folders;
  folders.reserve(folder_count);

  file.seekg(folder_records_offset, std::ios::beg);
  for(uint32_t i = 0; i < folder_count; ++i)
  {
    // hash(8)
    readLE<uint64_t>(file);
    FolderInfo info;
    info.count = readLE<uint32_t>(file);
    if(sse)
    {
      readLE<uint32_t>(file); // padding
      info.offset = readLE<uint64_t>(file);
    }
    else
    {
      info.offset = readLE<uint32_t>(file);
    }
    if(info.count > MAX_RECORDS)
      throw std::runtime_error("BSA folder file-count exceeds sane limits.");
    folders.push_back(info);
  }

  // For each folder, the file-record block begins with the folder name (a
  // length-prefixed bzstring, present when archive_flags & 0x1) followed by
  // <count> 16-byte file records. The folder's 'offset' points just past the
  // total_file_name_length value, i.e. it is an absolute archive offset
  // (Bethesda adds total_file_name_length to the real offset; we use it
  // directly because the stored value already accounts for that).
  struct RawFile
  {
    uint64_t size_field = 0;
    uint64_t offset = 0;
    std::string folder_name;
  };
  std::vector<RawFile> raw_files;
  raw_files.reserve(file_count);

  for(const auto& folder : folders)
  {
    if(folder.offset >= file_size_)
      throw std::runtime_error("BSA folder block offset out of bounds.");
    file.seekg(static_cast<std::streamoff>(folder.offset), std::ios::beg);

    std::string folder_name;
    if(archive_flags & 0x1)
    {
      const uint8_t name_len = readLE<uint8_t>(file);
      std::vector<char> name = readBytes(file, name_len);
      // bzstring is null-terminated; drop the trailing null if present.
      if(!name.empty() && name.back() == '\0')
        name.pop_back();
      folder_name.assign(name.begin(), name.end());
    }

    for(uint32_t f = 0; f < folder.count; ++f)
    {
      // hash(8), size(4), offset(4)
      readLE<uint64_t>(file);
      RawFile rf;
      rf.size_field = readLE<uint32_t>(file);
      rf.offset = readLE<uint32_t>(file);
      rf.folder_name = folder_name;
      raw_files.push_back(rf);
    }
  }

  if(raw_files.size() != file_count)
    throw std::runtime_error("BSA file record count mismatch.");

  // The file-name block: file_count null-terminated strings, in order.
  std::vector<std::string> file_names;
  if(archive_flags & 0x2)
  {
    file_names.reserve(file_count);
    std::string current;
    for(uint32_t i = 0; i < file_count;)
    {
      char c;
      file.read(&c, 1);
      if(!file)
        throw std::runtime_error("Unexpected end of archive while reading file names.");
      if(c == '\0')
      {
        file_names.push_back(current);
        current.clear();
        ++i;
      }
      else
      {
        current.push_back(c);
      }
    }
  }

  entries_.reserve(file_count);
  for(size_t i = 0; i < raw_files.size(); ++i)
  {
    const RawFile& rf = raw_files[i];

    // The high bit (0x40000000) of the size field inverts the default
    // compression setting. Bit 0x80000000 of size is rarely used; mask both.
    const bool comp_flag_set = (rf.size_field & 0x40000000u) != 0;
    const bool compressed = default_compressed != comp_flag_set;
    const uint64_t raw_size = rf.size_field & 0x3FFFFFFFu;

    Entry entry;
    std::string name = i < file_names.size() ? file_names[i] : ("file_" + std::to_string(i));
    if(!rf.folder_name.empty())
      entry.path = normalizePath(rf.folder_name) + "/" + normalizePath(name);
    else
      entry.path = normalizePath(name);
    entry.compressed = compressed;
    entry.offset = rf.offset;
    entry.packed_size = raw_size;
    entry.has_name_prefix = embedded_names;

    // For compressed entries, the data block starts with a 4-byte uncompressed
    // size. We resolve that lazily at extraction time; for listing we report
    // the on-disk size as a best effort when unknown.
    entry.size = compressed ? 0 : raw_size;

    if(rf.offset >= file_size_)
      throw std::runtime_error("BSA file data offset out of bounds.");
    entries_.push_back(std::move(entry));
  }
}

void BsaArchive::parseBa2(std::ifstream& file)
{
  // Header: magic(4), version(4), type(4 chars), file_count(4),
  // name_table_offset(8).
  file.seekg(4, std::ios::beg);
  version_ = readLE<uint32_t>(file);

  char type[4];
  file.read(type, 4);
  if(!file)
    throw std::runtime_error("Failed to read BA2 archive type.");

  const uint32_t file_count = readLE<uint32_t>(file);
  const uint64_t name_table_offset = readLE<uint64_t>(file);

  static constexpr uint32_t MAX_RECORDS = 5'000'000u;
  if(file_count > MAX_RECORDS)
    throw std::runtime_error("BA2 file count exceeds sane limits.");
  if(name_table_offset > file_size_)
    throw std::runtime_error("BA2 name table offset out of bounds.");

  const bool general = std::memcmp(type, "GNRL", 4) == 0;
  const bool dx10 = std::memcmp(type, "DX10", 4) == 0;
  if(!general && !dx10)
    throw std::runtime_error("Unsupported BA2 archive type: " + std::string(type, 4) +
                             " (supported: GNRL, DX10).");
  format_ = general ? Format::Ba2Gnrl : Format::Ba2Dx10;

  // First read the file records, then the name table (which gives full paths in
  // the same order as the records).
  struct Ba2File
  {
    uint64_t offset = 0;
    uint64_t packed = 0;
    uint64_t unpacked = 0;
  };
  std::vector<Ba2File> files;
  files.reserve(file_count);

  // File records immediately follow the 24-byte header.
  file.seekg(24, std::ios::beg);
  for(uint32_t i = 0; i < file_count; ++i)
  {
    if(general)
    {
      // GNRL record (36 bytes): name_hash(4), ext(4), dir_hash(4), flags(4),
      // offset(8), packed_size(4), unpacked_size(4), align(4).
      readLE<uint32_t>(file); // name hash
      file.read(type, 4);     // extension (reuse buffer)
      readLE<uint32_t>(file); // dir hash
      readLE<uint32_t>(file); // flags
      Ba2File bf;
      bf.offset = readLE<uint64_t>(file);
      bf.packed = readLE<uint32_t>(file);
      bf.unpacked = readLE<uint32_t>(file);
      readLE<uint32_t>(file); // 0xBAADF00D alignment marker
      files.push_back(bf);
    }
    else
    {
      // DX10 record header (24 bytes): name_hash(4), ext(4), dir_hash(4),
      // unk8(1), num_chunks(1), chunk_header_size(2), height(2), width(2),
      // num_mips(1), format(1), unk16(2). Followed by num_chunks chunks of
      // 24 bytes each: offset(8), packed(4), unpacked(4), start_mip(2),
      // end_mip(2), align(4).
      readLE<uint32_t>(file); // name hash
      file.read(type, 4);     // extension
      readLE<uint32_t>(file); // dir hash
      readLE<uint8_t>(file);  // unk8
      const uint8_t num_chunks = readLE<uint8_t>(file);
      readLE<uint16_t>(file); // chunk header size
      readLE<uint16_t>(file); // height
      readLE<uint16_t>(file); // width
      readLE<uint8_t>(file);  // num mips
      readLE<uint8_t>(file);  // format
      readLE<uint16_t>(file); // unk16

      uint64_t total_unpacked = 0;
      uint64_t first_offset = 0;
      for(uint8_t c = 0; c < num_chunks; ++c)
      {
        const uint64_t offset = readLE<uint64_t>(file);
        const uint32_t packed = readLE<uint32_t>(file);
        const uint32_t unpacked = readLE<uint32_t>(file);
        readLE<uint16_t>(file); // start mip
        readLE<uint16_t>(file); // end mip
        readLE<uint32_t>(file); // 0xBAADF00D
        if(c == 0)
          first_offset = offset;
        total_unpacked += unpacked;
      }
      // Guard against corrupt records pointing outside the archive or
      // reporting an implausibly large unpacked size.
      if(num_chunks > 0 && first_offset >= file_size_)
        throw std::runtime_error("BA2 DX10 chunk offset out of bounds.");
      if(total_unpacked > file_size_)
        throw std::runtime_error("BA2 DX10 unpacked size exceeds archive bounds.");
      Ba2File bf;
      bf.offset = first_offset;
      bf.unpacked = total_unpacked;
      files.push_back(bf);
    }
  }

  // Name table: for each file, a uint16 length followed by that many name
  // bytes (full path, backslash separated, no null terminator).
  std::vector<std::string> names;
  names.reserve(file_count);
  file.seekg(static_cast<std::streamoff>(name_table_offset), std::ios::beg);
  for(uint32_t i = 0; i < file_count; ++i)
  {
    const uint16_t len = readLE<uint16_t>(file);
    std::vector<char> name = readBytes(file, len);
    names.emplace_back(name.begin(), name.end());
  }

  entries_.reserve(file_count);
  for(uint32_t i = 0; i < file_count; ++i)
  {
    const Ba2File& bf = files[i];
    Entry entry;
    entry.path = i < names.size() ? normalizePath(names[i]) : ("file_" + std::to_string(i));
    entry.size = bf.unpacked;
    entry.offset = bf.offset;
    entry.packed_size = bf.packed;
    entry.compressed = bf.packed != 0;
    // DX10 textures need DDS-header reconstruction; we list but do not extract.
    entry.listing_only = dx10;
    entries_.push_back(std::move(entry));
  }
}

bool BsaArchive::writeEntry(const Entry& entry, const sfs::path& dest)
{
  std::ifstream file(path_, std::ios::binary);
  if(!file)
    throw std::runtime_error("Failed to open archive: " + path_.string());

  uint64_t data_offset = entry.offset;
  uint64_t on_disk_size = entry.packed_size;

  // BSA entries may be prefixed with an embedded name bzstring; skip it.
  if(format_ == Format::Bsa && entry.has_name_prefix)
  {
    if(data_offset >= file_size_)
      throw std::runtime_error("Entry offset out of bounds.");
    file.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
    const uint8_t name_len = readLE<uint8_t>(file);
    const uint64_t skip = static_cast<uint64_t>(name_len) + 1; // length byte + name
    if(skip > on_disk_size)
      throw std::runtime_error("Embedded name length exceeds entry size.");
    data_offset += skip;
    on_disk_size -= skip;
  }

  if(data_offset > file_size_ || on_disk_size > file_size_ - data_offset)
    throw std::runtime_error("Entry data region exceeds archive bounds.");

  std::vector<char> output;

  if(format_ == Format::Bsa)
  {
    if(entry.compressed)
    {
      // Compressed BSA blocks start with a 4-byte uncompressed size, then the
      // zlib stream.
      file.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
      const uint32_t uncompressed_size = readLE<uint32_t>(file);
      if(on_disk_size < 4)
        throw std::runtime_error("Compressed entry too small.");
      std::vector<char> packed = readBytes(file, on_disk_size - 4);
      output = inflateZlib(packed, uncompressed_size);
    }
    else
    {
      file.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
      output = readBytes(file, on_disk_size);
    }
  }
  else if(format_ == Format::Ba2Gnrl)
  {
    file.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
    if(entry.compressed)
    {
      // GNRL compressed entries are raw zlib streams; packed_size is the
      // on-disk size, size the uncompressed size.
      std::vector<char> packed = readBytes(file, on_disk_size);
      output = inflateZlib(packed, entry.size);
    }
    else
    {
      output = readBytes(file, entry.size);
    }
  }
  else
  {
    // DX10 textures: listing only.
    return false;
  }

  std::error_code ec;
  if(dest.has_parent_path())
    sfs::create_directories(dest.parent_path(), ec);
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if(!out)
    throw std::runtime_error("Failed to open destination file: " + dest.string());
  out.write(output.data(), static_cast<std::streamsize>(output.size()));
  if(!out)
    throw std::runtime_error("Failed to write destination file: " + dest.string());
  return true;
}

bool BsaArchive::extractTo(const std::string& internal_path, const sfs::path& dest)
{
  const std::string target = normalizePath(internal_path);
  for(const auto& entry : entries_)
  {
    if(entry.path == target)
    {
      if(entry.listing_only)
        return false;
      return writeEntry(entry, dest);
    }
  }
  throw std::runtime_error("Entry not found in archive: " + internal_path);
}
