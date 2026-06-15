/*!
 * \file ommrepository.h
 * \brief Header for the remote::OmmRepository class and its provider-neutral data structs.
 *
 * Implements fork feature #114: Open-Mod-Manager (OMM) compatible network mod
 * repository support. OMM repositories are described by an XML descriptor served
 * over HTTP(S). This file provides a provider-neutral abstraction (mirroring the
 * established remote:: provider pattern from fork #60) so the rest of Limo does
 * not need to know about the OMM specific wire format.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>


/*!
 * \brief Contains structs and classes for accessing remote mod sources in a
 * provider neutral way.
 */
namespace remote
{
/*!
 * \brief A single downloadable file belonging to a remote package version.
 */
struct RemoteFile
{
  /*! \brief File name as advertised by the repository (used as the on-disk name). */
  std::string file_name;
  /*! \brief Absolute or repository-relative URL from which the file can be downloaded. */
  std::string url;
  /*! \brief File size in bytes, or -1 if unknown. */
  long size = -1;
  /*! \brief Optional checksum (e.g. md5/xxh) as advertised, empty if none. */
  std::string checksum;
};

/*!
 * \brief A versioned remote mod package as described by a repository descriptor.
 *
 * This is the provider-neutral representation; OMM specific fields are mapped
 * onto these members during parsing.
 */
struct RemotePackage
{
  /*! \brief Human readable package name. */
  std::string name;
  /*! \brief Version string, as advertised by the repository (free form). */
  std::string version;
  /*! \brief Category the package belongs to, empty if uncategorized. */
  std::string category;
  /*! \brief Optional short description. */
  std::string description;
  /*! \brief Files belonging to this package version. */
  std::vector<RemoteFile> files;

  /*!
   * \brief Returns the primary (first) download URL for this package.
   * \return The download URL or an empty string if the package has no files.
   */
  std::string primaryDownloadUrl() const
  {
    return files.empty() ? std::string{} : files.front().url;
  }
};

/*!
 * \brief Holds the data needed to start a download for a chosen package.
 *
 * This struct is what RepositoriesDialog emits when the user requests an
 * install; the MainWindow hook routes it through the existing download/import
 * flow.
 */
struct RemoteDownloadInfo
{
  /*! \brief Name of the package being installed. */
  std::string package_name;
  /*! \brief Version of the package being installed. */
  std::string version;
  /*! \brief Name the downloaded file should be saved as. */
  std::string file_name;
  /*! \brief Fully resolved (absolute) download URL. */
  std::string download_url;
};

/*!
 * \brief Provides access to an Open-Mod-Manager compatible network repository.
 *
 * Given a repository URL and optional HTTP basic-auth credentials, this class
 * fetches and parses the OMM XML descriptor into the provider-neutral structs
 * above. All network and parse errors are handled gracefully: methods return
 * empty containers / std::nullopt and log the cause via the Log namespace
 * rather than throwing.
 *
 * \par OMM descriptor schema (documented fields / assumptions)
 * The OMM repository descriptor is an XML document whose root carries the
 * repository title and contains a list of package (a.k.a. "remote") entries.
 * As the exact published schema varies between OMM versions, the parser is
 * intentionally lenient and accepts the commonly documented element/attribute
 * names. The following are recognised (first match wins):
 * - Repository title: root attribute \c title / \c name, or a child
 *   \c <title> element.
 * - Base download URL (optional): root attribute \c downpath / \c url, or a
 *   child \c <downpath>/<url> element. Relative file URLs are resolved against
 *   it, falling back to the repository URL's directory.
 * - Package entries: any element named \c remote, \c package, \c mod or
 *   \c entry directly under the root (or under a \c <remotes>/<packages>
 *   container).
 * - Per package: \c name / \c ident / \c title (name), \c version / \c vers
 *   (version), \c category / \c cat (category), \c description / \c desc.
 * - Per package files: child \c <file>/<download> elements (or a single
 *   \c file / \c url attribute on the package) providing \c name / \c file
 *   (file name) and \c url / \c href / \c file (URL), plus optional \c size
 *   and \c checksum / \c md5.
 */
class OmmRepository
{
public:
  /*! \brief Constructs an uninitialized repository. */
  OmmRepository() = default;
  /*!
   * \brief Constructs a repository for the given descriptor URL.
   * \param url URL of the OMM XML descriptor.
   * \param user Optional HTTP basic-auth user name.
   * \param password Optional HTTP basic-auth password.
   */
  explicit OmmRepository(std::string url,
                         std::string user = "",
                         std::string password = "");

  /*!
   * \brief Fetches the descriptor and validates that it is a parseable OMM
   * repository.
   * \return True if the repository could be reached and parsed.
   */
  bool connect();
  /*!
   * \brief Returns whether the repository has been successfully connected to.
   * \return True if connect() succeeded.
   */
  bool isConnected() const;
  /*!
   * \brief Returns the repository title, available after a successful connect().
   * \return The title, or an empty string.
   */
  std::string title() const;
  /*!
   * \brief Returns the descriptor URL of this repository.
   * \return The URL.
   */
  std::string url() const;

  /*!
   * \brief Lists all packages (with their versions) advertised by the repository.
   *
   * Fetches and parses the descriptor if it has not been fetched yet. On any
   * network or parse error an empty vector is returned and the error is logged.
   * \return The advertised packages.
   */
  std::vector<RemotePackage> listPackages();

  /*!
   * \brief Resolves the absolute download URL for a package's primary file.
   * \param package Package to resolve.
   * \return The absolute download URL, or std::nullopt if none could be resolved.
   */
  std::optional<std::string> resolveDownloadUrl(const RemotePackage& package) const;

  /*!
   * \brief Checks whether a newer version of an installed package is available.
   * \param package_name Name of the installed package.
   * \param installed_version Currently installed version string.
   * \return The newest RemotePackage if it is newer than \p installed_version,
   * else std::nullopt.
   */
  std::optional<RemotePackage> checkForUpdate(const std::string& package_name,
                                              const std::string& installed_version);

  /*!
   * \brief Compares two free-form version strings numerically where possible.
   *
   * Splits each version into dot/dash separated components and compares them
   * numerically when both are numeric, lexically otherwise. Missing trailing
   * components are treated as 0.
   * \param a First version.
   * \param b Second version.
   * \return Negative if a<b, zero if equal, positive if a>b.
   */
  static int compareVersions(const std::string& a, const std::string& b);

private:
  /*! \brief URL of the OMM XML descriptor. */
  std::string url_;
  /*! \brief Optional HTTP basic-auth user. */
  std::string user_;
  /*! \brief Optional HTTP basic-auth password. */
  std::string password_;
  /*! \brief Repository title parsed from the descriptor. */
  std::string title_;
  /*! \brief Base URL against which relative file URLs are resolved. */
  std::string base_url_;
  /*! \brief Cached packages from the last successful fetch. */
  std::vector<RemotePackage> packages_;
  /*! \brief Whether connect() succeeded. */
  bool connected_ = false;

  /*!
   * \brief Fetches and parses the descriptor, filling title_/base_url_/packages_.
   * \return True on success.
   */
  bool fetchAndParse();
  /*!
   * \brief Resolves a possibly-relative URL against the repository base URL.
   * \param raw_url URL as found in the descriptor.
   * \return The absolute URL.
   */
  std::string resolveUrl(const std::string& raw_url) const;
};
} // namespace remote
