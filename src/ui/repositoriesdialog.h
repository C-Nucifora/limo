/*!
 * \file repositoriesdialog.h
 * \brief Header for the RepositoriesDialog class.
 *
 * Implements the UI side of fork feature #114: managing OMM-compatible network
 * mod repositories, listing their packages and requesting installs.
 */

#pragma once

#include "../core/remote/ommrepository.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QString>
#include <map>
#include <optional>
#include <vector>


namespace Ui
{
class RepositoriesDialog;
}

/*!
 * \brief Dialog for managing OMM-compatible network mod repositories.
 *
 * Lets the user add/remove repositories (URL + optional credentials), browse the
 * packages of a selected repository and request an install. Installs are not
 * performed here: the dialog resolves the download URL and emits
 * installPackageRequested(), which the MainWindow routes through the existing
 * download/import flow.
 *
 * \par Persistence
 * Configured repositories are stored as JSON under the application's app-data
 * location (QStandardPaths::AppDataLocation), file \c repositories.json.
 *
 * \par Credential storage
 * Passwords are persisted only if the user opted in. They are stored AES-256-GCM
 * encrypted under the per-installation key (cryptography::encryptToToken), the same
 * "no master password" scheme used for the Nexus API key. Recovering a stored
 * password therefore requires both this config file and the owner-only installation
 * key file, not merely a read of the (previously base64-obfuscated) config. Configs
 * written by older versions, which stored a base64 password, are migrated to the
 * encrypted form on the next save.
 */
class RepositoriesDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Loads persisted repositories and initializes the UI.
   * \param parent Parent widget passed to QDialog.
   */
  explicit RepositoriesDialog(QWidget* parent = nullptr);
  /*! \brief Persists repositories and deletes the UI. */
  ~RepositoriesDialog();

signals:
  /*!
   * \brief Emitted when the user requests installation of a package.
   *
   * The MainWindow connects this to route the download through the existing
   * import flow. The dialog does not perform the download itself.
   * \param info Resolved download information for the chosen package.
   */
  void installPackageRequested(remote::RemoteDownloadInfo info);

private slots:
  /*! \brief Prompts for a new repository and adds it. */
  void onAddRepoClicked();
  /*! \brief Removes the selected repository. */
  void onRemoveRepoClicked();
  /*! \brief Loads the packages of the newly selected repository. */
  void onRepoSelectionChanged();
  /*! \brief Re-fetches the packages of the selected repository. */
  void onRefreshClicked();
  /*! \brief Resolves the selected package and emits installPackageRequested(). */
  void onInstallClicked();
  /*! \brief Handles completion of the off-thread package listing. */
  void onPackagesFetched();
  /*! \brief Handles completion of the off-thread connect (title lookup) when adding a repo. */
  void onConnectFetched();
  /*! \brief Handles completion of the off-thread download-URL resolution for an install. */
  void onResolveFetched();

private:
  /*! \brief A configured repository plus its credentials. */
  struct RepoConfig
  {
    /*! \brief Display name (repository title or URL). */
    QString name;
    /*! \brief Descriptor URL. */
    QString url;
    /*! \brief Optional HTTP basic-auth user. */
    QString user;
    /*! \brief Optional HTTP basic-auth password (see class docs on storage). */
    QString password;
    /*! \brief Whether the password should be persisted. */
    bool save_password = false;
  };

  /*! \brief Auto-generated UI elements. */
  Ui::RepositoriesDialog* ui;
  /*! \brief Configured repositories, in display order. */
  std::vector<RepoConfig> repos_;
  /*! \brief Packages of the currently selected repository. */
  std::vector<remote::RemotePackage> current_packages_;

  /*! \brief Watches the off-thread package listing (fork audit F080). */
  QFutureWatcher<std::vector<remote::RemotePackage>> packages_watcher_;
  /*! \brief Watches the off-thread connect/title lookup when adding a repo. Holds the title if
   *  the connection succeeded, std::nullopt otherwise. */
  QFutureWatcher<std::optional<std::string>> connect_watcher_;
  /*! \brief Watches the off-thread download-URL resolution for an install. */
  QFutureWatcher<std::optional<std::string>> resolve_watcher_;
  /*! \brief Cached packages keyed by repository identity (url\nuser) so switching between
   *  repositories does not re-fetch from the network on every selection change (audit F163). */
  std::map<QString, std::vector<remote::RemotePackage>> package_cache_;
  /*! \brief Identity of the repository whose package listing is currently in flight. */
  QString pending_packages_key_;
  /*! \brief Whether a busy/override cursor is currently active (keeps the cursor stack balanced). */
  bool is_busy_ = false;
  /*! \brief Repository being added while its title lookup is in flight. */
  RepoConfig pending_add_config_;
  /*! \brief Partially filled download info awaiting URL resolution. */
  remote::RemoteDownloadInfo pending_install_info_;

  /*! \brief Returns the path of the repositories JSON file. */
  static QString configFilePath();
  /*! \brief Stable cache/identity key for a repository (url + user). */
  static QString repoKey(const RepoConfig& config);
  /*! \brief Toggles the busy/loading state (cursor, button enablement, status text). */
  void setBusy(bool busy);
  /*! \brief Fills the package tree widget from current_packages_. */
  void populatePackageTree();
  /*! \brief Loads repos_ from disk. */
  void loadRepos();
  /*! \brief Saves repos_ to disk. */
  void saveRepos() const;
  /*! \brief Rebuilds the repository list widget from repos_. */
  void refreshRepoList();
  /*! \brief Loads and displays the packages for the repository at \p index. */
  void loadPackages(int index);
  /*! \brief Index of the currently selected repository, or -1. */
  int selectedRepoIndex() const;
};
