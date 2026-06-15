#include "downloadswidget.h"
#include "ui_downloadswidget.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QWidget>

// fork #8: persistent download queue with progress and retry.

namespace
{
// column indices of the download table
enum Column
{
  COL_NAME = 0,
  COL_PROGRESS = 1,
  COL_SPEED = 2,
  COL_STATUS = 3,
  COL_ACTIONS = 4,
  COL_COUNT = 5
};
} // namespace

DownloadsWidget::DownloadsWidget(QWidget* parent) :
  QWidget(parent), ui(new Ui::DownloadsWidget)
{
  ui->setupUi(this);
  ui->download_table->setColumnCount(COL_COUNT);
  ui->download_table->setHorizontalHeaderLabels(
    { "Name", "Progress", "Speed", "Status", "Actions" });
  ui->download_table->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
  ui->download_table->horizontalHeader()->setSectionResizeMode(COL_PROGRESS,
                                                               QHeaderView::ResizeToContents);
  ui->download_table->horizontalHeader()->setSectionResizeMode(COL_SPEED,
                                                               QHeaderView::ResizeToContents);
  ui->download_table->horizontalHeader()->setSectionResizeMode(COL_STATUS,
                                                               QHeaderView::ResizeToContents);
  ui->download_table->horizontalHeader()->setSectionResizeMode(COL_ACTIONS,
                                                               QHeaderView::ResizeToContents);

  connect(ui->clear_finished_button,
          &QPushButton::clicked,
          this,
          &DownloadsWidget::onClearFinishedClicked);

  // ask the application manager for the current queue
  emit requestDownloadQueue();
}

DownloadsWidget::~DownloadsWidget()
{
  delete ui;
}

QString DownloadsWidget::statusString(DownloadQueueItem::Status status)
{
  switch(status)
  {
    case DownloadQueueItem::queued:
      return "Queued";
    case DownloadQueueItem::active:
      return "Active";
    case DownloadQueueItem::done:
      return "Done";
    case DownloadQueueItem::failed:
      return "Failed";
    case DownloadQueueItem::cancelled:
      return "Cancelled";
  }
  return "Unknown";
}

QString DownloadsWidget::formatSize(double bytes)
{
  const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  int unit = 0;
  while(bytes >= 1024.0 && unit < 4)
  {
    bytes /= 1024.0;
    unit++;
  }
  return QString::number(bytes, 'f', unit == 0 ? 0 : 1) + " " + units[unit];
}

void DownloadsWidget::onDownloadQueueChanged(std::vector<DownloadQueueItem> queue)
{
  queue_ = queue;
  ui->download_table->setRowCount((int)queue.size());
  for(int row = 0; row < (int)queue.size(); row++)
  {
    const DownloadQueueItem& item = queue[row];

    // name
    auto* name_item = new QTableWidgetItem(QString::fromStdString(item.name));
    name_item->setToolTip(QString::fromStdString(item.remote_source));
    ui->download_table->setItem(row, COL_NAME, name_item);

    // progress bar
    auto* bar = new QProgressBar();
    bar->setMinimum(0);
    bar->setMaximum(100);
    if(item.status == DownloadQueueItem::done)
      bar->setValue(100);
    else if(item.bytes_total > 0)
      bar->setValue((int)(100.0 * (double)item.bytes_done / (double)item.bytes_total));
    else
      bar->setValue(0);
    ui->download_table->setCellWidget(row, COL_PROGRESS, bar);

    // speed (only meaningful while active)
    QString speed_text = "-";
    if(item.status == DownloadQueueItem::active && item.speed > 0.0)
      speed_text = formatSize(item.speed) + "/s";
    ui->download_table->setItem(row, COL_SPEED, new QTableWidgetItem(speed_text));

    // status
    QString status_text = statusString(item.status);
    if(item.retry_count > 0)
      status_text += QString(" (retry %1)").arg(item.retry_count);
    ui->download_table->setItem(row, COL_STATUS, new QTableWidgetItem(status_text));

    // action buttons
    auto* actions = new QWidget();
    auto* layout = new QHBoxLayout(actions);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);
    const int id = item.id;

    auto* cancel_button = new QPushButton("Cancel");
    cancel_button->setEnabled(item.status == DownloadQueueItem::queued ||
                              item.status == DownloadQueueItem::active ||
                              item.status == DownloadQueueItem::failed);
    connect(cancel_button, &QPushButton::clicked, this, [this, id]() { emit cancelDownload(id); });
    layout->addWidget(cancel_button);

    auto* retry_button = new QPushButton("Retry");
    retry_button->setEnabled(item.status == DownloadQueueItem::failed ||
                             item.status == DownloadQueueItem::cancelled);
    connect(retry_button, &QPushButton::clicked, this, [this, id]() { emit retryDownload(id); });
    layout->addWidget(retry_button);

    ui->download_table->setCellWidget(row, COL_ACTIONS, actions);
  }
}

void DownloadsWidget::onClearFinishedClicked()
{
  for(const auto& item : queue_)
  {
    if(item.status == DownloadQueueItem::done || item.status == DownloadQueueItem::failed ||
       item.status == DownloadQueueItem::cancelled)
      emit removeDownload(item.id);
  }
}
