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
 * Passwords are persisted only if the user opted in. They are stored base64
 * encoded (obfuscation, NOT encryption). This is a deliberate, documented
 * limitation: Limo has no per-user secret store wired up for repository
 * credentials, so users who do not want their password on disk should leave it
 * empty and will be prompted (left as a future improvement) or rely on tokens
 * embedded in the URL.
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

  /*! \brief Returns the path of the repositories JSON file. */
  static QString configFilePath();
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
