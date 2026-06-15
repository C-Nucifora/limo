/*!
 * \file nexuschangelogdialog.h
 * \brief Header for the NexusChangelogDialog class.
 */

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <string>
#include <utility>
#include <vector>


namespace Ui
{
class NexusChangelogDialog;
}

/*!
 * \brief Dialog used to display a NexusMods mod's changelogs.
 *
 * Given a NexusMods domain and mod id, the dialog fetches the mod's changelogs from the
 * NexusMods API off the UI thread and displays them as version headers with bullet lists in a
 * read-only QTextBrowser. Network failures are handled gracefully by showing an error message.
 */
class NexusChangelogDialog : public QDialog
{
  Q_OBJECT

public:
  /*! \brief Type used to store changelogs: For every version a vector of changes. */
  using Changelog = std::vector<std::pair<std::string, std::vector<std::string>>>;

  /*!
   * \brief Initializes the UI and starts fetching the changelogs.
   * \param domain_name The NexusMods domain containing the mod, e.g. "skyrimspecialedition".
   * \param mod_id Target mod id.
   * \param target_version If not empty: Only changes for versions newer than or equal to this
   * version are highlighted; older entries are still shown below them.
   * \param parent Parent widget.
   */
  explicit NexusChangelogDialog(const QString& domain_name,
                                long mod_id,
                                const QString& target_version = {},
                                QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~NexusChangelogDialog();

private slots:
  /*! \brief Called when the background fetch has finished. Populates the changelog view. */
  void onFetchFinished();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::NexusChangelogDialog* ui;
  /*! \brief The NexusMods domain containing the mod. */
  QString domain_name_;
  /*! \brief Target mod id. */
  long mod_id_;
  /*! \brief Version since which changes are highlighted. May be empty. */
  QString target_version_;
  /*! \brief Watches the background changelog fetch. */
  QFutureWatcher<Changelog> watcher_;

  /*!
   * \brief Renders the given changelog entries into the QTextBrowser.
   * \param changelog The changelog to display.
   */
  void displayChangelog(const Changelog& changelog);
};
