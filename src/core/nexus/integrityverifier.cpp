#include "integrityverifier.h"
#include "api.h"
#include "../parseerror.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <json/json.h>
#include <openssl/evp.h>
#include <cpr/cpr.h>

using namespace nexus;
namespace fs = std::filesystem;


std::optional<std::string> IntegrityVerifier::computeMd5(const std::string& file_path)
{
  std::ifstream file(file_path, std::ios::binary);
  if(!file)
    return {};

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if(ctx == nullptr)
    return {};

  if(EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1)
  {
    EVP_MD_CTX_free(ctx);
    return {};
  }

  std::array<char, 64 * 1024> buffer;
  while(file)
  {
    file.read(buffer.data(), buffer.size());
    const std::streamsize read = file.gcount();
    if(read > 0)
    {
      if(EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(read)) != 1)
      {
        EVP_MD_CTX_free(ctx);
        return {};
      }
    }
  }
  if(file.bad())
  {
    EVP_MD_CTX_free(ctx);
    return {};
  }

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
  unsigned int digest_len = 0;
  if(EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1)
  {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  EVP_MD_CTX_free(ctx);

  std::string hex;
  hex.reserve(static_cast<size_t>(digest_len) * 2);
  for(unsigned int i = 0; i < digest_len; i++)
  {
    char part[3];
    std::snprintf(part, sizeof(part), "%02x", digest[i]);
    hex.append(part);
  }
  return hex;
}

IntegrityVerifier::Result IntegrityVerifier::verify(const std::string& file_path,
                                                    const File& expected,
                                                    const std::string& domain_name)
{
  Result result;

  // Verify file size first, since it is cheap and requires no hashing or network access.
  std::error_code ec;
  if(!fs::exists(file_path, ec) || ec)
  {
    result.status = Status::file_error;
    result.message = std::format("File \"{}\" does not exist.", file_path);
    return result;
  }

  const long actual_size = static_cast<long>(fs::file_size(file_path, ec));
  if(ec)
  {
    result.status = Status::file_error;
    result.message = std::format("Failed to read file \"{}\".", file_path);
    return result;
  }

  if(expected.size_in_bytes > 0 && actual_size != expected.size_in_bytes)
  {
    result.status = Status::size_mismatch;
    result.message = std::format(
      "File size mismatch: expected {} bytes, got {} bytes.", expected.size_in_bytes, actual_size);
    return result;
  }

  const auto md5_opt = computeMd5(file_path);
  if(!md5_opt)
  {
    result.status = Status::file_error;
    result.message = std::format("Failed to read file \"{}\".", file_path);
    return result;
  }
  result.computed_md5 = *md5_opt;

  // Query the NexusMods md5_search endpoint to confirm the computed hash belongs to the
  // expected file. The endpoint returns every mod file matching the given MD5.
  cpr::Response response =
    cpr::Get(cpr::Url(std::format("https://api.nexusmods.com/v1/games/{}/mods/md5_search/{}.json",
                                  domain_name,
                                  result.computed_md5)),
             cpr::Header{ { "apikey", Api::getApiKey() } });

  if(response.status_code == 404)
  {
    result.status = Status::not_found;
    result.message =
      "NexusMods does not recognize the computed MD5 hash. The file may be corrupted or "
      "modified.";
    return result;
  }
  if(response.status_code != 200)
  {
    result.status = Status::api_error;
    result.message = std::format(
      "Failed to query NexusMods for the MD5 hash. Response code was {}.", response.status_code);
    return result;
  }

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
  {
    result.status = Status::api_error;
    result.message = "Failed to parse response from NexusMods.";
    return result;
  }

  bool file_id_matches = false;
  for(int i = 0; i < json_body.size(); i++)
  {
    const auto& details = json_body[i]["file_details"];
    if(details["file_id"].asInt64() == expected.file_id)
    {
      file_id_matches = true;
      break;
    }
  }

  if(!file_id_matches)
  {
    result.status = Status::md5_mismatch;
    result.message =
      "The MD5 hash does not match the expected NexusMods file. The downloaded archive "
      "does not correspond to the selected file.";
    return result;
  }

  result.status = Status::passed;
  result.message = "Integrity verified: file size and MD5 hash match NexusMods.";
  return result;
}
