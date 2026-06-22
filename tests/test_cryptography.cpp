#include "../src/core/cryptography.h"
#include "test_utils.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>


std::string generateRandomString(std::default_random_engine& e)
{
  std::uniform_int_distribution<int> length_dist(1, 100);
  std::uniform_int_distribution<int> char_dist(0, 255);
  std::string str;
  const int str_len = length_dist(e);
  for(int j = 0; j < str_len; j++)
    str += static_cast<char>(char_dist(e));
  return str;
}

TEST_CASE("String are encrypted", "[crypto]")
{
  std::random_device r;
  std::default_random_engine e(r());
  std::vector<std::pair<std::string, std::string>> text_key_pairs{ { "this is a super secret text",
                                                                     "some key" } };

  for(int i = 0; i < 10; i++)
    text_key_pairs.emplace_back(generateRandomString(e), generateRandomString(e));

  for(const auto& [plain_text, key] : text_key_pairs)
  {
    const auto [cipher, nonce, tag] = cryptography::encrypt(plain_text, key);
    REQUIRE(!cipher.empty());
    REQUIRE(!nonce.empty());
    REQUIRE(!tag.empty());
    REQUIRE(cipher != plain_text);
    std::string decrypted_text = cryptography::decrypt(cipher, key, nonce, tag);
    REQUIRE(decrypted_text == plain_text);
  }

  const std::string key = "my key";
  const std::string plain_text = "some text";
  const auto [cipher, nonce, tag] = cryptography::encrypt(plain_text, key);
  REQUIRE_THROWS_AS(cryptography::decrypt(cipher + "a", key, nonce, tag), CryptographyError);
  REQUIRE_THROWS_AS(cryptography::decrypt(cipher, key + "a", nonce, tag), CryptographyError);
  REQUIRE_THROWS_AS(cryptography::decrypt(cipher, key, nonce == "a" ? "b" : "a", tag),
                    CryptographyError);
  REQUIRE_THROWS_AS(cryptography::decrypt(cipher, key, nonce, tag == "a" ? "b" : "a"),
                    CryptographyError);
}

TEST_CASE("Secrets round-trip through an opaque encrypted token", "[crypto]")
{
  // Isolate the per-installation key file in a temp config dir so the token's installation-key
  // encryption is hermetic and does not touch the developer's real ~/.config/Limo.
  const std::filesystem::path config_home = DATA_DIR / "crypto_token_cfg";
  std::filesystem::remove_all(config_home);
  setenv("XDG_CONFIG_HOME", config_home.c_str(), 1);

  SECTION("Round-trips plain, empty and binary secrets")
  {
    std::random_device r;
    std::default_random_engine e(r());
    std::vector<std::string> secrets{ "hunter2", "", "p@ss:word:with:colons" };
    for(int i = 0; i < 10; i++)
      secrets.push_back(generateRandomString(e));

    for(const std::string& secret : secrets)
    {
      const std::string token = cryptography::encryptToToken(secret);
      // The token is opaque and must not leak the secret verbatim.
      REQUIRE(token.rfind("v1:", 0) == 0);
      if(!secret.empty())
        REQUIRE(token.find(secret) == std::string::npos);
      const std::optional<std::string> recovered = cryptography::decryptFromToken(token);
      REQUIRE(recovered.has_value());
      REQUIRE(*recovered == secret);
    }
  }

  SECTION("Malformed or tampered tokens yield nullopt instead of throwing")
  {
    REQUIRE_FALSE(cryptography::decryptFromToken("").has_value());
    REQUIRE_FALSE(cryptography::decryptFromToken("not a token").has_value());
    REQUIRE_FALSE(cryptography::decryptFromToken("v1:zz:zz:zz").has_value()); // non-hex
    REQUIRE_FALSE(cryptography::decryptFromToken("v1:aabb:ccdd").has_value()); // too few fields
    REQUIRE_FALSE(cryptography::decryptFromToken("v2:aa:bb:cc").has_value()); // wrong version

    std::string token = cryptography::encryptToToken("secret");
    // Flip the last hex nibble of the auth tag: GCM verification must fail -> nullopt.
    token.back() = (token.back() == 'a' ? 'b' : 'a');
    REQUIRE_FALSE(cryptography::decryptFromToken(token).has_value());
  }
}
