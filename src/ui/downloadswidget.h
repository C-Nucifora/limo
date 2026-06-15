/*!
 * \file downloadswidget.h
 * \brief Header for the DownloadsWidget class.
 */

#pragma once

#include "applicationmanager.h"
#include <QWidget>
#include <vector>


namespace Ui
{
class DownloadsWidget;
}

/*!
 * \brief fork #8: Panel that displays the persistent download queue.
 *
 * Shows one row per download with name, progress %, speed, status and per-item
 * Cancel/Retry buttons. It is a pure view: all queue mutations are performed by
 * \ref ApplicationManager via Qt signals (queued connections), and the widget is
 * refreshed whenever \ref ApplicationManager::downloadQueueChanged fires.
 */
class DownloadsWidget : public QWidget
{
  Q_OBJECT

public:
  /*!
   * \brief Sets up the UI.
   * \param parent Parent widget.
   */
  explicit DownloadsWidget(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~DownloadsWidget();

public slots:
  /*!
   * \brief Rebuilds the table from the given queue snapshot.
   * \param queue Current download queue.
   */
  void onDownloadQueueChanged(std::vector<DownloadQueueItem> queue);

signals:
  /*! \brief Requests the current queue state (emitted on construction). */
  void requestDownloadQueue();
  /*! \brief Requests cancellation of the item with the given id. */
  void cancelDownload(int id);
  /*! \brief Requests a retry of the item with the given id. */
  void retryDownload(int id);
  /*! \brief Requests removal of the item with the given id. */
  void removeDownload(int id);

private slots:
  /*! \brief Removes all finished/failed/cancelled items from the queue. */
  void onClearFinishedClicked();

private:
  /*! \brief Auto-generated UI. */
  Ui::DownloadsWidget* ui;
  /*! \brief Last queue snapshot, used by the Clear finished button. */
  std::vector<DownloadQueueItem> queue_;

  /*! \brief Returns a human readable status string for the given status. */
  static QString statusString(DownloadQueueItem::Status status);
  /*! \brief Returns a human readable size string (e.g. "12.3 MiB"). */
  static QString formatSize(double bytes);
};
