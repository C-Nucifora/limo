/*!
 * \file collectiondialog.h
 * \brief Header for the CollectionDialog class. Fork features #1/#2: import and
 * export of Nexus Collection manifests (collection.json).
 */

#pragma once

#include "../core/modinfo.h"
#include "../core/nexus/collection.h"
#include <QDialog>
#include <QString>
#include <vector>


namespace Ui
{
class CollectionDialog;
}

/*!
 * \brief Dialog used to import and export Nexus Collection manifests.
 *
 * In import mode the dialog parses a collection.json, displays its mods and, on accept,
 * emits modDownloadRequested for every Nexus mod in install order so the main window can
 * route each request through the existing queued download/import flow.
 *
 * In export mode the dialog displays the current deployer's mods, lets the user pick a
 * destination and writes a collection.json built from those mods.
 */
class CollectionDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, this is passed to the constructor of QDialog.
   */
  explicit CollectionDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~CollectionDialog();

  /*!
   * \brief Prepares the dialog for importing a collection. Prompts for a collection.json
   * file, parses it and populates the mod table. Does nothing if the user cancels the
   * file selection or the file can not be parsed.
   * \param app_id App the imported mods will be added to.
   * \return True if a collection was loaded and the dialog should be shown.
   */
  bool setupImport(int app_id);
  /*!
   * \brief Prepares the dialog for exporting a collection from the given mods.
   * \param app_id App the mods belong to.
   * \param game_domain NexusMods domain of the target game.
   * \param collection_name Default name for the exported collection.
   * \param mods The current mod list to export.
   */
  void setupExport(int app_id,
                   const QString& game_domain,
                   const QString& collection_name,
                   const std::vector<ModInfo>& mods);

signals:
  /*!
   * \brief Emitted once per Nexus mod when an import is accepted. Mirrors the signature of
   * NexusModDialog::modDownloadRequested so the existing download slot can be reused.
   * \param app_id Target app.
   * \param mod_id NexusMods mod id.
   * \param file_id NexusMods file id.
   * \param mod_url NexusMods mod page URL.
   * \param version Mod version recorded in the manifest.
   */
  void modDownloadRequested(int app_id, int mod_id, int file_id, QString mod_url, QString version);
  /*!
   * \brief Emitted when a non-fatal error occurs (e.g. a skipped mod). Lets the main window
   * surface a message without aborting the whole import/export.
   * \param title Error title.
   * \param message Error message.
   */
  void collectionError(QString title, QString message);

private slots:
  /*! \brief In import mode queues every Nexus mod; in export mode writes the manifest. */
  void on_buttonBox_accepted();
  /*! \brief Closes the dialog. */
  void on_buttonBox_rejected();

private:
  /*! \brief Indicates whether the dialog operates in export mode. */
  enum Mode
  {
    import_mode = 0,
    export_mode = 1
  };

  /*! \brief Contains auto-generated UI elements. */
  Ui::CollectionDialog* ui;
  /*! \brief Current dialog mode. */
  Mode mode_ = import_mode;
  /*! \brief App the dialog operates on. */
  int app_id_ = 0;
  /*! \brief NexusMods domain of the target game. */
  QString game_domain_;
  /*! \brief Default name used for the exported collection. */
  QString collection_name_;
  /*! \brief Parsed collection used in import mode. */
  nexus::Collection collection_;
  /*! \brief Entries to be emitted/written, in install order. */
  std::vector<nexus::Collection::Entry> entries_;
  /*! \brief Indicates whether the dialog has been completed. */
  bool dialog_completed_ = false;

  /*! \brief Fills the mod table from entries_. */
  void populateTable();
};
