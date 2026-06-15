/*!
 * \file integrityverifier.h
 * \brief Header for the nexus::IntegrityVerifier class.
 */

#pragma once

#include "file.h"
#include <optional>
#include <string>


/*!
 * \brief The nexus namespace contains structs and functions needed for accessing the NexusMods API.
 */
namespace nexus
{
/*!
 * \brief Provides functions for verifying the integrity of a downloaded archive against the
 * metadata provided by the NexusMods API.
 */
class IntegrityVerifier
{
public:
  /*! \brief Possible outcomes of an integrity check. */
  enum class Status
  {
    /*! \brief Both the MD5 hash and the file size match the expected values. */
    passed,
    /*! \brief The local file could not be opened or read. */
    file_error,
    /*! \brief The file size does not match the expected value. */
    size_mismatch,
    /*! \brief The MD5 hash does not match the value reported by NexusMods. */
    md5_mismatch,
    /*! \brief NexusMods does not recognize the computed MD5 hash for the expected mod. */
    not_found,
    /*! \brief The remote MD5 could not be retrieved, e.g. due to a network or API error. */
    api_error
  };

  /*!
   * \brief Holds the result of an integrity check.
   */
  struct Result
  {
    /*! \brief The outcome of the check. */
    Status status = Status::api_error;
    /*! \brief The MD5 hash computed for the local file (lower case hex). */
    std::string computed_md5;
    /*! \brief A human readable message describing the result. */
    std::string message;

    /*!
     * \brief Convenience check for a successful verification.
     * \return True if the status is Status::passed.
     */
    bool passed() const { return status == Status::passed; }
  };

  /*! \brief This is an abstract class, so the constructor is deleted. */
  IntegrityVerifier() = delete;

  /*!
   * \brief Computes the MD5 hash of the file at the given path.
   * \param file_path Path to the local file.
   * \return The MD5 hash as a lower case hex string, or an empty optional if the file
   * could not be read.
   */
  static std::optional<std::string> computeMd5(const std::string& file_path);

  /*!
   * \brief Verifies the file at the given path against the metadata of the given NexusMods file.
   *
   * Checks the file size against File::size_in_bytes and computes the local MD5 hash, then
   * queries the NexusMods md5_search endpoint to confirm the hash is associated with the
   * expected file id.
   *
   * \param file_path Path to the local archive to verify.
   * \param expected The NexusMods file metadata to verify against.
   * \param domain_name The NexusMods domain (e.g. "skyrimspecialedition") the file belongs to.
   * \return A Result describing the outcome of the check.
   */
  static Result verify(const std::string& file_path,
                       const File& expected,
                       const std::string& domain_name);
};
}
