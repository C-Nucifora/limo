#include "test_utils.h"
#include <fstream>

// DATA_DIR is initialized at runtime by initTestDataDir().
sfs::path DATA_DIR;


std::vector<std::string> getFiles(sfs::path dir, bool get_contents = false)
{
  std::vector<std::string> files;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(dir))
  {
    if(dir_entry.path().filename() == ".lmmfiles" || dir_entry.path().filename() == ".lmm_managed_dir"
       || dir_entry.path().filename() == ".gitkeep")
      continue;
    std::string entry = dir_entry.path().string().erase(0, dir.string().size());
    // Symlinks are recorded together with their (textual) target so that the deploy
    // tests which rely on symlinks actually verify where the link points, not merely
    // that a link of some name exists.
    if(dir_entry.is_symlink())
    {
      std::error_code ec;
      const sfs::path target = sfs::read_symlink(dir_entry.path(), ec);
      entry.push_back('\0');
      entry.append(ec ? std::string("<unreadable symlink>") : target.string());
    }
    else if(get_contents && dir_entry.is_regular_file())
    {
      std::ifstream file(dir_entry.path());
      entry.push_back('\0');
      entry.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      file.close();
    }
    files.push_back(entry);
  }
  return files;
}

void verifyDirsAreEqual(sfs::path first_dir, sfs::path second_dir, bool test_content)
{
  std::vector<std::string> first_dir_files = getFiles(first_dir, test_content);
  std::vector<std::string> second_dir_files = getFiles(second_dir, test_content);

  CAPTURE(first_dir.string());
  CAPTURE(second_dir.string());
  REQUIRE(second_dir_files.size() == first_dir_files.size());
  REQUIRE_THAT(first_dir_files, Catch::Matchers::UnorderedEquals(second_dir_files));
}

void resetAppDir()
{
  if(sfs::exists(DATA_DIR / "app"))
    sfs::remove_all(DATA_DIR / "app");
  sfs::copy_options options(sfs::copy_options::recursive | sfs::copy_options::overwrite_existing);
  sfs::copy(DATA_DIR / "source" / "app", DATA_DIR / "app", options);
}

void resetStagingDir()
{
  if(sfs::exists(DATA_DIR / "staging"))
    sfs::remove_all(DATA_DIR / "staging");
  sfs::create_directories(DATA_DIR / "staging");
}

void verifyFilesAreEqual(sfs::path first_file, sfs::path second_file)
{
  CAPTURE(first_file.string());
  CAPTURE(second_file.string());
  REQUIRE(sfs::exists(first_file));
  REQUIRE(sfs::exists(second_file));
  std::string content_first_file = "";
  std::ifstream file(first_file);
  content_first_file.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  file.close();
  std::string content_second_file = "";
  file.open(second_file);
  content_second_file.append(std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>());
  file.close();
  REQUIRE(content_first_file == content_second_file);
}

void initTestDataDir()
{
  // Create a unique temporary working directory for this test run.
  sfs::path tmp_base = sfs::temp_directory_path() / "limo_tests";
  sfs::remove_all(tmp_base);
  sfs::create_directories(tmp_base);

  // Copy the entire immutable fixture tree into the temp dir.
  sfs::copy(FIXTURE_DIR, tmp_base, sfs::copy_options::recursive);

  DATA_DIR = tmp_base;
}
