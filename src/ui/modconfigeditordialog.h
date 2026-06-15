/*!
 * \file modconfigeditordialog.h
 * \brief Header for the ModConfigEditorDialog class
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>


namespace Ui
{
class ModConfigEditorDialog;
}

/*!
 * \brief Dialog used to edit config files (.cfg/.ini/.json/...) shipped by a mod
 * within its staging directory.
 */
class ModConfigEditorDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Recursively scans the given staging directory for editable text files.
   * \param mod_staging_path Path to the mod's staging directory.
   * \param mod_name Optional mod name used in the window title.
   * \param parent Parent widget.
   */
  explicit ModConfigEditorDialog(const QString& mod_staging_path,
                                 const QString& mod_name = {},
                                 QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~ModConfigEditorDialog();

signals:
  /*! \brief Emitted after a config file has been successfully saved. */
  void configSaved();

protected:
  /*!
   * \brief Warns about unsaved changes before closing.
   * \param event Close event.
   */
  void closeEvent(QCloseEvent* event) override;

private slots:
  /*!
   * \brief Loads the file at the given index into the editor.
   * \param index Index of the selected file.
   */
  void onFileSelectionChanged(int index);
  /*! \brief Saves the current file back to staging. */
  void onSaveClicked();
  /*! \brief Marks the current document as modified. */
  void onTextChanged();

private:
  /*! \brief Maximum number of files to list. */
  static constexpr int MAX_FILES = 500;
  /*! \brief Maximum directory recursion depth. */
  static constexpr int MAX_DEPTH = 16;
  /*! \brief Maximum size of a file that will be loaded into the editor. */
  static constexpr qint64 MAX_FILE_SIZE = 16 * 1024 * 1024;

  /*! \brief Contains auto-generated UI elements. */
  Ui::ModConfigEditorDialog* ui;
  /*! \brief Absolute path to the mod's staging directory. */
  QString staging_path_;
  /*! \brief Absolute paths of the found editable files, parallel to the list widget. */
  QStringList file_paths_;
  /*! \brief Index of the currently loaded file, or -1 if none. */
  int current_index_ = -1;
  /*! \brief Indicates that the current document has unsaved changes. */
  bool unsaved_changes_ = false;

  /*!
   * \brief Recursively scans the staging directory for editable files.
   * \param root Directory to scan.
   */
  void scanForFiles(const QString& root);
  /*!
   * \brief Updates the window title and save button state.
   */
  void updateState();
  /*!
   * \brief Asks the user whether to discard unsaved changes.
   * \return True if it is safe to proceed (no changes or user discarded them).
   */
  bool confirmDiscardChanges();
};
