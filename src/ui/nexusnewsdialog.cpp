// fork #211: in-app Nexus news/announcements feed

#include "nexusnewsdialog.h"
#include "core/log.h"
#include "ui_nexusnewsdialog.h"
#include <QDesktopServices>
#include <QListWidgetItem>
#include <QPointer>
#include <QUrl>
#include <QXmlStreamReader>
#include <cpr/cpr.h>
#include <chrono>
#include <format>
#include <thread>

NexusNewsDialog::NexusNewsDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::NexusNewsDialog)
{
  ui->setupUi(this);
  setWindowTitle("Nexus News");
  feed_url_ = DEFAULT_FEED_URL;
  connect(this, &NexusNewsDialog::fetchFinished, this, &NexusNewsDialog::onFetchFinished);
  connect(ui->news_list,
          &QListWidget::currentRowChanged,
          this,
          [this](int row) { ui->open_button->setEnabled(row >= 0); });
  refresh();
}

NexusNewsDialog::~NexusNewsDialog()
{
  delete ui;
}

void NexusNewsDialog::refresh()
{
  if(is_loading_)
    return;
  is_loading_ = true;
  ui->refresh_button->setEnabled(false);
  ui->open_button->setEnabled(false);
  ui->status_label->setText("Loading…");
  ui->news_list->clear();

  const std::string url = feed_url_.toStdString();
  // Guard against the dialog being destroyed while the request is in flight: the
  // queued fetchFinished signal is only delivered if "this" is still alive.
  QPointer<NexusNewsDialog> guard(this);
  std::thread(
    [url, guard]()
    {
      cpr::Response response =
        cpr::Get(cpr::Url(url),
                 cpr::Header{ { "User-Agent", "Limo Mod Manager" }, { "Accept", "application/rss+xml, application/xml, text/xml" } },
                 // Bound the request so the detached thread cannot hang indefinitely.
                 cpr::Timeout{ std::chrono::seconds(30) });
      if(!guard)
        return;
      if(response.status_code == 200)
        emit guard->fetchFinished(true, QString::fromStdString(response.text));
      else
      {
        const QString error =
          response.status_code == 0
            ? QString::fromStdString(std::format("Network error: {}", response.error.message))
            : QString::fromStdString(std::format("Request failed (HTTP {}).", response.status_code));
        emit guard->fetchFinished(false, error);
      }
    })
    .detach();
}

void NexusNewsDialog::onFetchFinished(bool success, QString body)
{
  is_loading_ = false;
  ui->refresh_button->setEnabled(true);

  if(!success)
  {
    items_.clear();
    updateList();
    ui->status_label->setText(body.isEmpty() ? "Failed to load news feed." : body);
    Log::error(std::format("Failed to fetch Nexus news feed: {}", body.toStdString()));
    return;
  }

  items_ = parseRss(body);
  updateList();
  if(items_.empty())
    ui->status_label->setText("No news items found or the feed could not be parsed.");
  else
    ui->status_label->setText(std::format("Loaded {} news item(s).", items_.size()).c_str());
}

std::vector<NexusNewsDialog::NewsItem> NexusNewsDialog::parseRss(const QString& body)
{
  std::vector<NewsItem> items;
  QXmlStreamReader xml(body);
  bool in_item = false;
  NewsItem current;
  while(!xml.atEnd() && !xml.hasError())
  {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if(token == QXmlStreamReader::StartElement)
    {
      const QStringView name = xml.name();
      if(name == u"item")
      {
        in_item = true;
        current = NewsItem{};
      }
      else if(in_item && name == u"title")
        current.title = xml.readElementText().trimmed();
      else if(in_item && name == u"link")
        current.link = xml.readElementText().trimmed();
      else if(in_item && name == u"pubDate")
        current.pub_date = xml.readElementText().trimmed();
    }
    else if(token == QXmlStreamReader::EndElement && xml.name() == u"item")
    {
      in_item = false;
      if(!current.title.isEmpty() || !current.link.isEmpty())
        items.push_back(current);
    }
  }
  if(xml.hasError())
  {
    Log::error(std::format("Failed to parse Nexus news RSS feed: {}",
                           xml.errorString().toStdString()));
    return {};
  }
  return items;
}

void NexusNewsDialog::updateList()
{
  ui->news_list->clear();
  for(const auto& item : items_)
  {
    QString text = item.title;
    if(!item.pub_date.isEmpty())
      text += QString("  —  %1").arg(item.pub_date);
    auto* list_item = new QListWidgetItem(text, ui->news_list);
    list_item->setToolTip(item.link);
  }
  ui->open_button->setEnabled(false);
}

void NexusNewsDialog::openRow(int row)
{
  if(row < 0 || row >= static_cast<int>(items_.size()))
    return;
  const QString& link = items_[row].link;
  if(link.isEmpty())
    return;
  QDesktopServices::openUrl(QUrl(link));
}

void NexusNewsDialog::on_news_list_itemDoubleClicked()
{
  openRow(ui->news_list->currentRow());
}

void NexusNewsDialog::on_open_button_clicked()
{
  openRow(ui->news_list->currentRow());
}

void NexusNewsDialog::on_refresh_button_clicked()
{
  refresh();
}
