/*!
 * \file bsaarchive.h
 * \brief Header for the BsaArchive class.
 */

// fork #201: BSA/BA2 archive browser & extractor

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>


/*!
 * \brief Read-only reader for Bethesda BSA (Skyrim/Oblivion/FO3/FNV) and BA2
 * (FO4/Starfield) archives.
 *
 * Given a path to a .bsa or .ba2 file the class parses the header plus the
 * file/name tables and exposes a flat list of entries. It can extract
 * uncompressed and zlib-compressed entries to disk. All reads are bounds
 * checked; a malformed archive throws std::runtime_error instead of reading out
 * of bounds.
 */
class BsaArchive
{
public:
  /*! \brief Identifies the on-disk archive format that was detected. */
  enum class Format
  {
    Unknown,  /*!< Format could not be determined. */
    Bsa,      /*!< Classic "BSA\0" archive (Oblivion/FO3/FNV/Skyrim/SSE). */
    Ba2Gnrl,  /*!< "BTDX" general archive. */
    Ba2Dx10   /*!< "BTDX" texture archive. */
  };

  /*! \brief A single file entry inside the archive. */
  struct Entry
  {
    /*! \brief Full internal path using forward slashes. */
    std::string path;
    /*! \brief Uncompressed size in bytes (0 if unknown). */
    uint64_t size = 0;
    /*! \brief True if the entry is stored compressed. */
    bool compressed = false;
    /*! \brief True if the entry can only be listed, not extracted. */
    bool listing_only = false;

    // Internal locators used during extraction.
    uint64_t offset = 0;          /*!< Offset of the data block in the archive. */
    uint64_t packed_size = 0;     /*!< On-disk size of the data block. */
    bool has_name_prefix = false; /*!< BSA: data block is prefixed with a name string. */
  };

  /*!
   * \brief Opens and parses the archive at the given path.
   * \param path Path to a .bsa or .ba2 file.
   * \throws std::runtime_error If the file cannot be opened, the format is
   * unsupported, or the archive is malformed.
   */
  explicit BsaArchive(const std::filesystem::path& path);

  /*!
   * \brief Returns the parsed entries.
   * \return Vector of all file entries.
   */
  const std::vector<Entry>& entries() const { return entries_; }
  /*!
   * \brief Returns the detected archive format.
   * \return The format.
   */
  Format format() const { return format_; }
  /*!
   * \brief Returns a human readable description of the detected format.
   * \return The description.
   */
  std::string formatName() const;

  /*!
   * \brief Extracts a single entry, identified by its internal path, to a file.
   * \param internal_path Internal path of the entry (as returned in Entry::path).
   * \param dest Destination file path. Parent directories are created.
   * \return True on success, false if the entry cannot be extracted (e.g.
   * unsupported compression, listing-only).
   * \throws std::runtime_error On I/O errors or malformed data.
   */
  bool extractTo(const std::string& internal_path, const std::filesystem::path& dest);

private:
  /*! \brief Path to the source archive. */
  std::filesystem::path path_;
  /*! \brief Detected format. */
  Format format_ = Format::Unknown;
  /*! \brief Archive version (BSA) or BA2 version, for diagnostics. */
  uint32_t version_ = 0;
  /*! \brief Total file size, cached for bounds checks. */
  uint64_t file_size_ = 0;
  /*! \brief Parsed entries. */
  std::vector<Entry> entries_;

  /*! \brief Parses a classic "BSA\0" archive. */
  void parseBsa(std::ifstream& file);
  /*! \brief Parses a "BTDX" BA2 archive. */
  void parseBa2(std::ifstream& file);
  /*! \brief Reads raw on-disk data for an entry and writes it to dest. */
  bool writeEntry(const Entry& entry, const std::filesystem::path& dest);
};
