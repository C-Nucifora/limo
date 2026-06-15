/*!
 * \file cryptography.h
 * \brief Header for the cryptography namespace.
 */

#pragma once

#include <stdexcept>
#include <string>


/*!
 * \brief Exception indicating an error during a cryptographic operation.
 */
class CryptographyError : public std::runtime_error
{
public:
  /*!
   * \brief Constructor.
   * \param message Message for the exception.
   */
  CryptographyError(const char* message) : std::runtime_error(message) {}
  /*!
   * \brief Constructor.
   * \param message Message for the exception.
   */
  CryptographyError(const std::string& message) : std::runtime_error(message) {}
};


namespace cryptography
{
/*!
 * \brief Encrypts the given string using AES-GCM with the given key.
 * \param plain_text Text to be encrapted.
 * \param key Key to use for encryption.
 * \return The cipher text, the random nonce(IV) used, the authentication tag.
 * \throws CryptographyError When an OpenSSL internal error occurs.
 */
std::tuple<std::string, std::string, std::string> encrypt(const std::string& plain_text,
                                                          const std::string& key);
/*!
 * \brief Decrypts the given cipher text using AES-GCM.
 * \param cipher_text Text to be decrypted.
 * \param key Key used for decryption.
 * \param nonce Nonce (IV) used during enryption.
 * \param tag Authentication tag.
 * \return The plain text.
 * \throws CryptographyError When an OpenSSL internal error occurs.
 */
std::string decrypt(const std::string& cipher_text,
                    const std::string& key,
                    const std::string& nonce,
                    const std::string& tag);

/*!
 * \brief Sentinel value passed by callers to indicate that no master password was chosen.
 *
 * SECURITY (fork issue #28): This used to be a single, hardcoded key baked into the binary.
 * Encrypting with it offered no real protection: anyone with the config file and the (public)
 * binary could trivially recover the API key, while the UI implied the key was protected.
 *
 * It is now only a sentinel. When this value (or an empty string) is passed to encrypt()/
 * decrypt(), a per-installation random key is used instead (see installationKey()). The random
 * key is generated once and stored in a file with 0600 permissions inside the application's
 * config directory, so it is at least not a single constant shared across all users/installs.
 *
 * The literal below is kept unchanged for backwards compatibility only: decrypt() falls back to
 * it so that API keys encrypted by older versions can still be read (and re-saved under the new
 * scheme on the next change).
 */
constexpr char default_key[] = "rWnYJVdtxz8Iu62GSJy0OPlOat7imMb8";

/*!
 * \brief Returns the per-installation encryption key used when no master password is set.
 *
 * The key is 32 random bytes, generated on first use and persisted base64-encoded in a file
 * with owner-only (0600) permissions in the application config directory. It replaces the old
 * hardcoded \ref default_key for newly stored API keys.
 * \return The per-installation key.
 * \throws CryptographyError If the key cannot be generated or persisted.
 */
std::string installationKey();
};
