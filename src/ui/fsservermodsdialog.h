/*!
 * \file fsservermodsdialog.h
 * \brief Header for the FsServerModsDialog class.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>


class QLineEdit;
class QListWidget;
class QLabel;

/*!
 * \brief fork #241: Lists the mods served by a Farming Simulator dedicated server and queues
 * the selected ones for download through the normal import flow.
 */
class FsServerModsDialog : public QDialog
{
  Q_OBJECT

public:
  /*! \brief Builds the dialog UI. */
  explicit FsServerModsDialog(QWidget* parent = nullptr);

signals:
  /*!
   * \brief Emitted when the user requests a download of the selected mods.
   * \param mods One [file_name, download_url] entry per selected mod.
   */
  void downloadRequested(QList<QStringList> mods);

private slots:
  /*! \brief Fetches and lists the mods from the entered server URL. */
  void onFetchClicked();
  /*! \brief Emits downloadRequested for the checked mods, then closes. */
  void onDownloadClicked();

private:
  /*! \brief Server address input. */
  QLineEdit* url_field_ = nullptr;
  /*! \brief Checkable list of available mods. */
  QListWidget* list_ = nullptr;
  /*! \brief Status / error line. */
  QLabel* status_ = nullptr;
};
