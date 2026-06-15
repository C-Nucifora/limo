/*!
 * \file bsabrowserdialog.h
 * \brief Header for the BsaBrowserDialog class.
 */

// fork #201: BSA/BA2 archive browser & extractor

#pragma once

#include "core/bsaarchive.h"
#include <QDialog>
#include <QString>
#include <memory>


namespace Ui
{
class BsaBrowserDialog;
}

/*!
 * \brief Dialog which opens a Bethesda BSA or BA2 archive, lists its contents
 * and extracts selected entries to a chosen directory.
 */
class BsaBrowserDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, passed to the constructor of QDialog.
   */
  explicit BsaBrowserDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~BsaBrowserDialog();

private slots:
  /*! \brief Prompts for an archive file and loads it. */
  void onOpenArchive();
  /*! \brief Extracts the currently selected entries to a chosen directory. */
  void onExtractSelected();
  /*! \brief Enables/disables the extract button based on the selection. */
  void onSelectionChanged();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::BsaBrowserDialog* ui;
  /*! \brief The currently opened archive, or null if none. */
  std::unique_ptr<BsaArchive> archive_;

  /*! \brief Fills the table from the currently opened archive. */
  void populateTable();
  /*!
   * \brief Returns a human readable size string (e.g. "1.4 MiB").
   * \param bytes Size in bytes.
   * \return The formatted string.
   */
  static QString humanSize(uint64_t bytes);
};
