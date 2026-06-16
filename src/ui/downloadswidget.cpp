#include "downloadswidget.h"
#include "ui_downloadswidget.h"
#include <QDateTime>      // fork #141
#include <QDir>           // fork #141
#include <QFile>          // fork #141
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>     // fork #141
#include <QJsonDocument>  // fork #141
#include <QJsonObject>    // fork #141
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths> // fork #141
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

  // fork #141: restore completed downloads from previous sessions before any
  // live queue snapshot arrives, so prior-session history is visible at startup.
  loadHistory();
  onDownloadQueueChanged({});

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

  // fork #141: persist any newly completed downloads from this snapshot.
  if(recordCompletedDownloads(queue))
    saveHistory();

  // fork #141: append persisted history rows that are not represented by a live
  // queue item, so prior-session completed downloads stay visible after restart.
  std::vector<const HistoryEntry*> shown_history;
  for(int i = (int)history_.size() - 1; i >= 0; i--)
  {
    const HistoryEntry& entry = history_[i];
    bool in_queue = false;
    for(const DownloadQueueItem& item : queue)
    {
      if(item.status == DownloadQueueItem::done &&
         QString::fromStdString(item.name) == entry.name)
      {
        in_queue = true;
        break;
      }
    }
    if(!in_queue)
      shown_history.push_back(&entry);
  }

  ui->download_table->setRowCount((int)queue.size() + (int)shown_history.size());
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

  // fork #141: render persisted history rows as clearly-finished, read-only
  // entries (no progress widget, no Cancel/Retry that could restart anything).
  for(int i = 0; i < (int)shown_history.size(); i++)
  {
    const HistoryEntry& entry = *shown_history[i];
    const int row = (int)queue.size() + i;

    auto* name_item = new QTableWidgetItem(entry.name);
    if(!entry.source.isEmpty())
      name_item->setToolTip(entry.source);
    ui->download_table->setItem(row, COL_NAME, name_item);

    auto* bar = new QProgressBar();
    bar->setMinimum(0);
    bar->setMaximum(100);
    bar->setValue(100);
    ui->download_table->setCellWidget(row, COL_PROGRESS, bar);

    QString size_text = entry.size > 0 ? formatSize((double)entry.size) : QString("-");
    ui->download_table->setItem(row, COL_SPEED, new QTableWidgetItem(size_text));

    QString status_text = "Done";
    if(entry.timestamp > 0)
    {
      const QDateTime when = QDateTime::fromSecsSinceEpoch(entry.timestamp);
      status_text += " (" + when.toLocalTime().toString("yyyy-MM-dd hh:mm") + ")";
    }
    ui->download_table->setItem(row, COL_STATUS, new QTableWidgetItem(status_text));

    // empty actions cell: history rows have no per-item buttons.
    ui->download_table->setItem(row, COL_ACTIONS, new QTableWidgetItem(QString()));
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

  // fork #141: clearing finished downloads also empties the persisted history,
  // keeping the in-memory rows and the on-disk store consistent.
  history_.clear();
  saveHistory();
  onDownloadQueueChanged(queue_);
}

QString DownloadsWidget::historyFilePath()
{
  // fork #141: same base dir the app uses for its other JSON settings.
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/download_history.json";
}

void DownloadsWidget::loadHistory()
{
  // fork #141: missing or corrupt file simply yields an empty history.
  history_.clear();
  QFile file(historyFilePath());
  if(!file.open(QIODevice::ReadOnly))
    return;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if(!doc.isArray())
    return;
  for(const QJsonValue& value : doc.array())
  {
    if(!value.isObject())
      continue;
    const QJsonObject obj = value.toObject();
    HistoryEntry entry;
    entry.name = obj.value("name").toString();
    entry.source = obj.value("source").toString();
    entry.size = (long long)obj.value("size").toDouble(0);
    entry.timestamp = (qint64)obj.value("timestamp").toDouble(0);
    if(entry.name.isEmpty())
      continue;
    history_.push_back(entry);
  }
  // enforce the cap in case an older/edited file exceeds it.
  if((int)history_.size() > HISTORY_MAX)
    history_.erase(history_.begin(), history_.end() - HISTORY_MAX);
}

void DownloadsWidget::saveHistory() const
{
  // fork #141: guarded write; failure to persist is non-fatal.
  QJsonArray array;
  for(const HistoryEntry& entry : history_)
  {
    QJsonObject obj;
    obj.insert("name", entry.name);
    obj.insert("source", entry.source);
    obj.insert("size", (double)entry.size);
    obj.insert("timestamp", (double)entry.timestamp);
    array.append(obj);
  }
  QFile file(historyFilePath());
  if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool DownloadsWidget::recordCompletedDownloads(const std::vector<DownloadQueueItem>& queue)
{
  // fork #141: append a lightweight record for each freshly completed download
  // that is not already in the history (deduped on name + size).
  bool changed = false;
  for(const DownloadQueueItem& item : queue)
  {
    if(item.status != DownloadQueueItem::done)
      continue;
    const QString name = QString::fromStdString(item.name);
    if(name.isEmpty())
      continue;

    bool already_known = false;
    for(const HistoryEntry& entry : history_)
    {
      if(entry.name == name && entry.size == item.bytes_total)
      {
        already_known = true;
        break;
      }
    }
    if(already_known)
      continue;

    HistoryEntry entry;
    entry.name = name;
    entry.source = QString::fromStdString(
      !item.remote_source.empty() ? item.remote_source : item.remote_request_url);
    entry.size = item.bytes_total;
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    history_.push_back(entry);
    changed = true;
  }

  if(changed && (int)history_.size() > HISTORY_MAX)
    history_.erase(history_.begin(), history_.end() - HISTORY_MAX);
  return changed;
}
