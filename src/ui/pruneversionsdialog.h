/*!
 * \file pruneversionsdialog.h
 * \brief Header for the PruneVersionsDialog class
 */

// fork #145: confirmation dialog for the bulk prune of outdated mod archive versions.

#pragma once

#include <QDialog>
#include <filesystem>
#include <utility>
#include <vector>


namespace Ui
{
class PruneVersionsDialog;
}

/*!
 * \brief Confirmation dialog listing the outdated mod archive files which are about to be
 * deleted together with the total size that would be freed. The dialog only displays and
 * confirms; the caller performs the actual deletion.
 */
class PruneVersionsDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Populates the dialog with the given archives.
   * \param archives For every prunable archive: A pair of its path and its size in bytes.
   * \param total_size The total size of all archives in bytes.
   * \param parent Parent widget.
   */
  explicit PruneVersionsDialog(
    const std::vector<std::pair<std::filesystem::path, unsigned long>>& archives,
    unsigned long total_size,
    QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~PruneVersionsDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::PruneVersionsDialog* ui;

  /*!
   * \brief Formats a byte count into a human-readable string (B, KiB, MiB, GiB, TiB).
   * \param bytes The size in bytes.
   * \return The formatted string.
   */
  static QString formatSize(unsigned long bytes);
};
