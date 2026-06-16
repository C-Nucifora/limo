#include "cryptography.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>


void throwError(const std::string& step)
{
  std::string error = "Error during " + step + ".\n";
  auto code = ERR_get_error();
  char buffer[256];
  while(code)
  {
    ERR_error_string(code, buffer);
    error.append(std::string(buffer));
    error.append("\n");
    code = ERR_get_error();
  }
  // Do not call ERR_free_strings() here: it deinitializes the process-wide OpenSSL error-string
  // tables, affecting all threads. The loop above already drained this thread's error queue.
  throw CryptographyError(error);
}

namespace cryptography
{
namespace
{
/*! \brief Number of random bytes making up the per-installation key. */
constexpr int installation_key_size = 32;

/*!
 * \brief Determines the directory in which the per-installation key is stored.
 *
 * Mirrors where the application keeps its config (QSettings writes to ~/.config/Limo.conf), using
 * $XDG_CONFIG_HOME/Limo or, as a fallback, $HOME/.config/Limo. Qt is intentionally not used here
 * because the core library does not link against Qt.
 */
std::filesystem::path installationKeyDir()
{
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if(xdg != nullptr && xdg[0] != '\0')
    return std::filesystem::path(xdg) / "Limo";
  const char* home = std::getenv("HOME");
  if(home != nullptr && home[0] != '\0')
    return std::filesystem::path(home) / ".config" / "Limo";
  throw CryptographyError("Could not determine config directory for installation key.");
}
}

std::string installationKey()
{
  // SECURITY (fork issue #28): When no master password is set we use a per-installation random
  // key instead of the old hardcoded default_key. The key is stored with owner-only permissions
  // (chmod 600) so that, unlike the previous baked-in constant, it is neither shared across all
  // installations nor recoverable from the (public) binary alone.
  const std::filesystem::path dir = installationKeyDir();
  const std::filesystem::path key_path = dir / "nexus_api.key";

  std::error_code ec;
  if(std::filesystem::exists(key_path, ec))
  {
    std::ifstream in(key_path, std::ios::binary);
    if(!in)
      throw CryptographyError("Could not read installation key file.");
    std::string stored((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(stored.size() == installation_key_size)
      return stored;
    // Fall through to regenerate if the file was empty/corrupt.
  }

  unsigned char key_bytes[installation_key_size];
  if(RAND_bytes(key_bytes, installation_key_size) != 1)
    throwError("installation key generation");
  const std::string raw(reinterpret_cast<const char*>(key_bytes), installation_key_size);

  std::filesystem::create_directories(dir, ec);
  if(ec)
    throw CryptographyError("Could not create config directory for installation key.");

  // Create the file empty, restrict it to owner read/write, then write the secret so the bytes
  // are never briefly world-readable.
  {
    std::ofstream out(key_path, std::ios::binary | std::ios::trunc);
    if(!out)
      throw CryptographyError("Could not write installation key file.");
  }
  std::filesystem::permissions(key_path,
                               std::filesystem::perms::owner_read |
                                 std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace,
                               ec);
  if(ec)
    throw CryptographyError("Could not set permissions on installation key file.");
  std::ofstream out(key_path, std::ios::binary | std::ios::trunc);
  if(!out)
    throw CryptographyError("Could not write installation key file.");
  out.write(raw.data(), raw.size());
  if(!out)
    throw CryptographyError("Could not write installation key file.");

  return raw;
}

/*!
 * \brief Resolves the key actually used for AES operations.
 *
 * An empty key or the legacy default_key sentinel both mean "no master password set" and are
 * mapped to the per-installation random key (see installationKey()).
 */
static std::string resolveKey(const std::string& key)
{
  if(key.empty() || key == default_key)
    return installationKey();
  return key;
}

std::tuple<std::string, std::string, std::string> encrypt(const std::string& plain_text,
                                                          const std::string& key)
{
  auto ctx = EVP_CIPHER_CTX_new();
  if(!ctx)
    throwError("encryption");

  if(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    throwError("encryption");

  constexpr int nonce_size = 12;
  unsigned char nonce[nonce_size];
  if(RAND_bytes(nonce, nonce_size) != 1)
    throwError("encryption");

  std::string actual_key = resolveKey(key);
  if(actual_key.empty())
  {
    EVP_CIPHER_CTX_free(ctx);
    throw CryptographyError("Cannot encrypt with an empty key.");
  }
  constexpr int key_size = 32;
  unsigned char key_padded[key_size];
  for(int i = 0; i < key_size; i++)
    key_padded[i] = actual_key[i % actual_key.size()];
  if(EVP_EncryptInit_ex(ctx, NULL, NULL, key_padded, nonce) != 1)
    throwError("encryption");

  // GCM is a stream cipher: the cipher text has the same length as the plain
  // text. Allocate an output buffer of that size plus the AES block size as a
  // safety margin, avoiding the previous (fragile, possibly out-of-range)
  // floating point size computation.
  const std::size_t buffer_size = plain_text.size() + 16;
  std::vector<unsigned char> cipher_text(buffer_size);
  int cur_length = 0;
  std::vector<unsigned char> plain_array(plain_text.begin(), plain_text.end());
  if(EVP_EncryptUpdate(ctx,
                       cipher_text.data(),
                       &cur_length,
                       plain_array.data(),
                       static_cast<int>(plain_array.size())) != 1)
    throwError("encryption");

  int cipher_length = cur_length;
  if(EVP_EncryptFinal_ex(ctx, cipher_text.data() + cur_length, &cur_length) != 1)
    throwError("encryption");
  cipher_length += cur_length;

  unsigned char tag[16];
  if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
    throwError("encryption");

  EVP_CIPHER_CTX_free(ctx);

  const std::string cipher_str(reinterpret_cast<const char*>(cipher_text.data()), cipher_length);
  const std::string nonce_str(reinterpret_cast<const char*>(nonce), nonce_size);
  const std::string tag_str(reinterpret_cast<const char*>(tag), 16);

  return { cipher_str, nonce_str, tag_str };
}

/*!
 * \brief Performs the actual AES-GCM decryption with an already resolved key.
 */
static std::string decryptWithKey(const std::string& cipher_text,
                                  const std::string& actual_key,
                                  const std::string& nonce,
                                  const std::string& tag)
{
  auto ctx = EVP_CIPHER_CTX_new();
  if(!ctx)
    throwError("decryption");

  if(EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    throwError("decryption");

  if(actual_key.empty())
  {
    EVP_CIPHER_CTX_free(ctx);
    throw CryptographyError("Cannot decrypt with an empty key.");
  }
  constexpr int key_size = 32;
  unsigned char key_arr[key_size];
  for(int i = 0; i < key_size; i++)
    key_arr[i] = actual_key[i % actual_key.size()];
  std::vector<unsigned char> nonce_arr(nonce.begin(), nonce.end());
  if(EVP_DecryptInit_ex(ctx, NULL, NULL, key_arr, nonce_arr.data()) != 1)
    throwError("decryption");

  std::vector<unsigned char> cipher_arr(cipher_text.begin(), cipher_text.end());
  // GCM does not expand the data: the plain text is at most as long as the
  // cipher text. Size the buffer accordingly (plus the AES block size as a
  // safety margin) instead of the previous log()-based computation, which was
  // undefined for empty/short input and could request an out-of-range length.
  std::vector<unsigned char> plain_text(cipher_text.size() + 16);
  int cur_length = 0;
  if(EVP_DecryptUpdate(ctx,
                       plain_text.data(),
                       &cur_length,
                       cipher_arr.data(),
                       static_cast<int>(cipher_arr.size())) != 1)
    throwError("decryption");
  int plain_text_length = cur_length;

  std::vector<unsigned char> tag_arr(tag.begin(), tag.end());
  if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag_arr.data()) != 1)
    throwError("decryption");

  if(EVP_DecryptFinal_ex(ctx, plain_text.data() + cur_length, &cur_length) <= 0)
    throwError("decryption");
  plain_text_length += cur_length;

  return std::string(reinterpret_cast<const char*>(plain_text.data()), plain_text_length);
}

std::string decrypt(const std::string& cipher_text,
                    const std::string& key,
                    const std::string& nonce,
                    const std::string& tag)
{
  // When no master password is set (empty key or the legacy default_key sentinel) try the
  // per-installation key first, then fall back to the old hardcoded default_key so that API keys
  // stored by versions predating fork issue #28 can still be decrypted. They will be re-encrypted
  // under the per-installation key the next time the user updates the key or password.
  const bool no_master_password = key.empty() || key == default_key;
  if(no_master_password)
  {
    try
    {
      return decryptWithKey(cipher_text, installationKey(), nonce, tag);
    }
    catch(CryptographyError&)
    {
      return decryptWithKey(cipher_text, default_key, nonce, tag);
    }
  }
  return decryptWithKey(cipher_text, key, nonce, tag);
}
}
