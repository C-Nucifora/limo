/*!
 * \file assetpreviewdialog.h
 * \brief Header for the AssetPreviewDialog class.
 */

#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>


namespace Ui
{
class AssetPreviewDialog;
}

/*!
 * \brief Dialog used to preview common game-asset files (text, images, textures
 * and meshes) shipped by a mod within its staging directory, without external
 * tools.
 *
 * The dialog lists previewable files on the left and shows a preview of the
 * selected file on the right. Text files are shown in a read-only monospace
 * editor, images (including some textures) are rendered scaled-to-fit, and
 * meshes are shown as information only. Decoding is fully guarded so a malformed
 * file never crashes the dialog.
 */
class AssetPreviewDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Recursively scans the given staging directory for previewable files.
   * \param mod_staging_path Path to the mod's staging directory.
   * \param mod_name Optional mod name used in the window title.
   * \param parent Parent widget.
   */
  explicit AssetPreviewDialog(const QString& mod_staging_path,
                              const QString& mod_name = {},
                              QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~AssetPreviewDialog();

protected:
  /*!
   * \brief Re-fits the current image preview when the dialog is resized.
   * \param event Resize event.
   */
  void resizeEvent(QResizeEvent* event) override;

private slots:
  /*!
   * \brief Loads and previews the file at the given index.
   * \param index Index of the selected file.
   */
  void onFileSelectionChanged(int index);

private:
  /*! \brief Maximum number of files to list. */
  static constexpr int MAX_FILES = 2000;
  /*! \brief Maximum directory recursion depth. */
  static constexpr int MAX_DEPTH = 16;
  /*! \brief Maximum size of a text file that will be loaded into the viewer. */
  static constexpr qint64 MAX_TEXT_SIZE = 4 * 1024 * 1024;
  /*! \brief Maximum size of an image/texture file that will be decoded. */
  static constexpr qint64 MAX_IMAGE_SIZE = 64 * 1024 * 1024;

  /*! \brief Index of the stacked text page. */
  static constexpr int PAGE_TEXT = 0;
  /*! \brief Index of the stacked image page. */
  static constexpr int PAGE_IMAGE = 1;

  /*! \brief Contains auto-generated UI elements. */
  Ui::AssetPreviewDialog* ui;
  /*! \brief Absolute path to the mod's staging directory. */
  QString staging_path_;
  /*! \brief Absolute paths of the found previewable files, parallel to the list. */
  QStringList file_paths_;
  /*! \brief The currently displayed image, used to re-fit on resize. */
  QImage current_image_;

  /*!
   * \brief Recursively scans the staging directory for previewable files.
   * \param root Directory to scan.
   */
  void scanForFiles(const QString& root);

  /*!
   * \brief Shows text in the read-only viewer.
   * \param path Absolute path of the file.
   */
  void previewText(const QString& path);
  /*!
   * \brief Shows an image/texture in the scaled image label.
   * \param path Absolute path of the file.
   * \param suffix Lower-case file extension (without dot).
   */
  void previewImage(const QString& path, const QString& suffix);
  /*!
   * \brief Shows information about a mesh (.nif) file.
   * \param path Absolute path of the file.
   */
  void previewMesh(const QString& path);
  /*!
   * \brief Shows a "no preview available" message with the file size.
   * \param path Absolute path of the file.
   * \param message Optional extra message line.
   */
  void previewUnsupported(const QString& path, const QString& message = {});

  /*! \brief Displays the given image scaled-to-fit in the image page. */
  void setImage(const QImage& image);
  /*! \brief Shows the given text on the text page. */
  void setText(const QString& text);

  /*!
   * \brief Attempts to decode a DDS file into a QImage.
   * \param path Absolute path of the file.
   * \param info_out Set to header info text when decoding is not possible.
   * \return A valid image on success, or a null image (info_out set) otherwise.
   */
  QImage decodeDds(const QString& path, QString& info_out);
};
