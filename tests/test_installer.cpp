#include "../src/core/installer.h"
#include "test_utils.h"
#include <archive.h>
#include <archive_entry.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <iostream>
#include <string>
#include <utility>
#include <vector>


namespace
{
/*!
 * \brief Writes a minimal (uncompressed) tar archive whose entries use the given
 * in-archive path names, each holding the given content. Entry names are stored
 * verbatim so callers can embed hostile paths (e.g. "../escape.txt" or an
 * absolute path) to exercise the extractor's zip-slip protection.
 */
void writeMaliciousTar(const sfs::path& archive_path,
                       const std::vector<std::pair<std::string, std::string>>& entries)
{
  struct archive* a = archive_write_new();
  REQUIRE(a != nullptr);
  // GNU tar format preserves long/absolute/".." names without normalizing them.
  archive_write_set_format_gnutar(a);
  REQUIRE(archive_write_open_filename(a, archive_path.c_str()) == ARCHIVE_OK);
  for(const auto& [name, content] : entries)
  {
    struct archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, name.c_str());
    archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    REQUIRE(archive_write_header(a, entry) == ARCHIVE_OK);
    if(!content.empty())
      archive_write_data(a, content.data(), content.size());
    archive_entry_free(entry);
  }
  archive_write_close(a);
  archive_write_free(a);
}
} // namespace



TEST_CASE("Files are extracted", "[installer]")
{
  resetStagingDir();
  Installer::extract(DATA_DIR / "source" / "mod0.tar.gz", DATA_DIR / "staging" / "extract");
  verifyDirsAreEqual(DATA_DIR / "source" / "0", DATA_DIR / "staging" / "extract");
}

TEST_CASE("Mods are (un)installed", "[installer]")
{
  resetStagingDir();
  Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                     DATA_DIR / "staging",
                     Installer::preserve_case | Installer::preserve_directories);

  SECTION("Simple installer")
  verifyDirsAreEqual(DATA_DIR / "source/0", DATA_DIR / "staging");
  SECTION("Uninstallation")
  {
    Installer::uninstall(DATA_DIR / "staging", Installer::SIMPLEINSTALLER);
    REQUIRE_FALSE(sfs::exists(DATA_DIR / "staging"));
  }
}

TEST_CASE("Installer options", "[installer]")
{
  resetStagingDir();
  SECTION("Upper case conversion")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "upper",
                       Installer::upper_case | Installer::preserve_directories);
    verifyDirsAreEqual(DATA_DIR / "target" / "upper", DATA_DIR / "staging" / "upper");
  }
  SECTION("lower case conversion")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "lower",
                       Installer::lower_case | Installer::preserve_directories);
    verifyDirsAreEqual(DATA_DIR / "target" / "lower", DATA_DIR / "staging" / "lower");
  }
  SECTION("Single directory")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "single_dir",
                       Installer::preserve_case | Installer::single_directory);
    verifyDirsAreEqual(DATA_DIR / "target" / "single_dir", DATA_DIR / "staging" / "single_dir");
  }
  SECTION("Upper case and single directory")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "upper_single",
                       Installer::upper_case | Installer::single_directory);
    verifyDirsAreEqual(DATA_DIR / "target" / "upper_single", DATA_DIR / "staging" / "upper_single");
  }
}

TEST_CASE("Root levels", "[installer]")
{
  resetStagingDir();
  SECTION("Level 0")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "0",
                       Installer::preserve_case | Installer::preserve_directories,
                       Installer::SIMPLEINSTALLER,
                       0);
    verifyDirsAreEqual(DATA_DIR / "target" / "root_level" / "0", DATA_DIR / "staging" / "0");
  }
  SECTION("Level 1")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "1",
                       Installer::preserve_case | Installer::preserve_directories,
                       Installer::SIMPLEINSTALLER,
                       1);
    verifyDirsAreEqual(DATA_DIR / "target" / "root_level" / "1", DATA_DIR / "staging" / "1");
  }
  SECTION("Level 2")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "2",
                       Installer::preserve_case | Installer::preserve_directories,
                       Installer::SIMPLEINSTALLER,
                       2);
    verifyDirsAreEqual(DATA_DIR / "target" / "root_level" / "2", DATA_DIR / "staging" / "2");
  }
  SECTION("Level 3")
  {
    Installer::install(DATA_DIR / "source" / "mod0.tar.gz",
                       DATA_DIR / "staging" / "3",
                       Installer::preserve_case | Installer::preserve_directories,
                       Installer::SIMPLEINSTALLER,
                       3);
    verifyDirsAreEqual(DATA_DIR / "target" / "root_level" / "3", DATA_DIR / "staging" / "3");
  }
}

TEST_CASE("Extraction rejects path traversal and absolute path entries", "[installer][security]")
{
  resetStagingDir();
  const sfs::path sandbox = DATA_DIR / "staging" / "zip_slip";
  const sfs::path dest = sandbox / "dest";
  sfs::create_directories(dest);

  // Sentinel locations OUTSIDE the destination that a malicious entry would target.
  const sfs::path relative_escape_target = sandbox / "escape.txt"; // "../escape.txt" from dest
  const sfs::path absolute_escape_target = sandbox / "absolute_escape.txt";
  REQUIRE_FALSE(sfs::exists(relative_escape_target));
  REQUIRE_FALSE(sfs::exists(absolute_escape_target));

  SECTION("Relative '..' traversal entry escapes nothing")
  {
    const sfs::path archive = sandbox / "traversal.tar";
    writeMaliciousTar(archive, { { "../escape.txt", "pwned" }, { "benign.txt", "ok" } });
    // The extractor may either skip the hostile entry or abort with a CompressionError;
    // either way the security invariant is that nothing is written outside dest.
    try
    {
      Installer::extract(archive, dest);
    }
    catch(const std::exception&)
    {}
    REQUIRE_FALSE(sfs::exists(relative_escape_target));
  }

  SECTION("Absolute path entry escapes nothing")
  {
    const sfs::path archive = sandbox / "absolute.tar";
    writeMaliciousTar(archive, { { absolute_escape_target.string(), "pwned" } });
    try
    {
      Installer::extract(archive, dest);
    }
    catch(const std::exception&)
    {}
    REQUIRE_FALSE(sfs::exists(absolute_escape_target));
  }

  SECTION("Deeper '..' traversal entry escapes nothing")
  {
    const sfs::path archive = sandbox / "deep_traversal.tar";
    writeMaliciousTar(archive, { { "../../escape.txt", "pwned" } });
    try
    {
      Installer::extract(archive, dest);
    }
    catch(const std::exception&)
    {}
    REQUIRE_FALSE(sfs::exists(DATA_DIR / "staging" / "escape.txt"));
    REQUIRE_FALSE(sfs::exists(DATA_DIR / "escape.txt"));
  }
}
