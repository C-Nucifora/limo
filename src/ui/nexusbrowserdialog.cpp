#include "nexusbrowserdialog.h"
#include "core/log.h"
#include "ui_nexusbrowserdialog.h"
#include <QListWidgetItem>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>


// Role used to store the index into results_ on each list item.
static constexpr int result_index_role = Qt::UserRole + 1;


NexusBrowserDialog::NexusBrowserDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::NexusBrowserDialog)
{
  ui->setupUi(this);

  connect(ui->search_button, &QPushButton::clicked, this, &NexusBrowserDialog::onSearchClicked);
  connect(ui->search_field, &QLineEdit::returnPressed, this, &NexusBrowserDialog::onSearchClicked);
  connect(ui->result_list,
          &QListWidget::itemSelectionChanged,
          this,
          &NexusBrowserDialog::onResultSelectionChanged);
  connect(ui->install_button, &QPushButton::clicked, this, &NexusBrowserDialog::onInstallClicked);
  connect(
    &search_watcher_, &QFutureWatcher<std::vector<nexus::SearchResult>>::finished, this, &NexusBrowserDialog::onSearchFinished);
  connect(&install_watcher_,
          &QFutureWatcher<std::vector<nexus::File>>::finished,
          this,
          &NexusBrowserDialog::onInstallFilesFetched);
}

NexusBrowserDialog::~NexusBrowserDialog()
{
  delete ui;
}

void NexusBrowserDialog::setupDialog(int app_id, const QString& domain_name)
{
  app_id_ = app_id;
  domain_name_ = domain_name;
  setWindowTitle(QString("Browse NexusMods - %1").arg(domain_name));
  ui->status_label->setText(
    QString("Searching domain \"%1\". Enter a query and press Search.").arg(domain_name));
  ui->result_list->clear();
  ui->detail_browser->clear();
  ui->install_button->setEnabled(false);
  results_.clear();
}

nexus::SortOrder NexusBrowserDialog::currentSortOrder() const
{
  switch(ui->sort_box->currentIndex())
  {
    case 1:
      return nexus::SortOrder::downloads;
    case 2:
      return nexus::SortOrder::recent;
    default:
      return nexus::SortOrder::endorsements;
  }
}

int NexusBrowserDialog::currentCategoryId() const
{
  return ui->category_box->value();
}

void NexusBrowserDialog::setBusy(bool busy)
{
  ui->search_button->setEnabled(!busy);
  ui->search_field->setEnabled(!busy);
  ui->sort_box->setEnabled(!busy);
  ui->category_box->setEnabled(!busy);
}

void NexusBrowserDialog::onSearchClicked()
{
  if(domain_name_.isEmpty())
  {
    ui->status_label->setText("No NexusMods domain set for the current application.");
    return;
  }

  setBusy(true);
  ui->status_label->setText("Searching NexusMods...");
  ui->result_list->clear();
  ui->detail_browser->clear();
  ui->install_button->setEnabled(false);

  const std::string domain = domain_name_.toStdString();
  const std::string query = ui->search_field->text().toStdString();
  const nexus::SortOrder sort_order = currentSortOrder();
  const int category_id = currentCategoryId();

  // Run the network request off the UI thread, mirroring MainWindow's QtConcurrent usage.
  search_watcher_.setFuture(QtConcurrent::run(
    [domain, query, sort_order, category_id]()
    { return nexus::Api::searchMods(domain, query, sort_order, category_id); }));
}

void NexusBrowserDialog::onSearchFinished()
{
  setBusy(false);
  results_ = search_watcher_.result();

  if(results_.empty())
  {
    ui->status_label->setText("No results (or the request failed - check the log).");
    return;
  }

  for(std::size_t i = 0; i < results_.size(); i++)
  {
    const nexus::Mod& mod = results_[i].mod;
    auto* item = new QListWidgetItem(
      QString("%1\n  %2 endorsements | %3 downloads")
        .arg(QString::fromStdString(mod.name))
        .arg(mod.endorsement_count)
        .arg(mod.mod_downloads),
      ui->result_list);
    item->setData(result_index_role, static_cast<qulonglong>(i));
  }
  ui->status_label->setText(QString("%1 result(s).").arg(results_.size()));
}

void NexusBrowserDialog::onResultSelectionChanged()
{
  auto* item = ui->result_list->currentItem();
  if(item == nullptr)
  {
    ui->install_button->setEnabled(false);
    return;
  }

  const std::size_t index = item->data(result_index_role).toULongLong();
  if(index >= results_.size())
    return;
  const nexus::SearchResult& result = results_[index];
  const nexus::Mod& mod = result.mod;

  const QString mod_url =
    QString("https://www.nexusmods.com/%1/mods/%2")
      .arg(QString::fromStdString(result.domain_name))
      .arg(mod.mod_id);

  // Only accept http(s) thumbnail URLs to avoid embedding/fetching arbitrary schemes.
  const QUrl picture_qurl(QString::fromStdString(mod.picture_url));
  const bool picture_url_valid =
    !mod.picture_url.empty() && picture_qurl.isValid() &&
    (picture_qurl.scheme() == "http" || picture_qurl.scheme() == "https");

  QString html;
  if(picture_url_valid)
    html += QString("<img src=\"%1\" width=\"320\"><br><br>")
              .arg(QString::fromStdString(mod.picture_url).toHtmlEscaped());
  html += QString("<h2>%1</h2>").arg(QString::fromStdString(mod.name).toHtmlEscaped());
  if(!mod.version.empty())
    html += QString("<p><b>Version:</b> %1</p>")
              .arg(QString::fromStdString(mod.version).toHtmlEscaped());
  if(!mod.author.empty())
    html += QString("<p><b>Author:</b> %1</p>")
              .arg(QString::fromStdString(mod.author).toHtmlEscaped());
  html += QString("<p><b>Endorsements:</b> %1 &nbsp;|&nbsp; <b>Downloads:</b> %2</p>")
            .arg(mod.endorsement_count)
            .arg(mod.mod_downloads);
  html += QString("<p>%1</p>").arg(QString::fromStdString(mod.summary).toHtmlEscaped());
  html += QString("<p><a href=\"%1\">Open on NexusMods</a></p>").arg(mod_url);
  ui->detail_browser->setHtml(html);

  // Lazily load the thumbnail off the UI thread and feed it into the document. Degrades
  // gracefully: on any error the alt text already shown by QTextBrowser remains.
  if(picture_url_valid)
  {
    QNetworkRequest request{ picture_qurl };
    QNetworkReply* reply = network_manager_.get(request);
    QTextBrowser* browser = ui->detail_browser;
    const QString picture_url = QString::fromStdString(mod.picture_url);
    connect(reply,
            &QNetworkReply::finished,
            this,
            [reply, browser, picture_url]()
            {
              reply->deleteLater();
              if(reply->error() != QNetworkReply::NoError)
                return;
              QPixmap pixmap;
              if(!pixmap.loadFromData(reply->readAll()))
                return;
              browser->document()->addResource(
                QTextDocument::ImageResource, QUrl(picture_url), pixmap);
              browser->setLineWrapColumnOrWidth(browser->lineWrapColumnOrWidth());
              browser->viewport()->update();
            });
  }

  ui->install_button->setEnabled(true);
}

void NexusBrowserDialog::onInstallClicked()
{
  auto* item = ui->result_list->currentItem();
  if(item == nullptr)
    return;
  const std::size_t index = item->data(result_index_role).toULongLong();
  if(index >= results_.size())
    return;
  const nexus::SearchResult& result = results_[index];

  ui->install_button->setEnabled(false);
  ui->status_label->setText("Fetching files for the selected mod...");
  pending_install_mod_id_ = result.mod.mod_id;

  const QString mod_url =
    QString("https://www.nexusmods.com/%1/mods/%2")
      .arg(QString::fromStdString(result.domain_name))
      .arg(result.mod.mod_id);
  const std::string mod_url_std = mod_url.toStdString();

  // Fetch the available files off the UI thread; the actual download is handled by the
  // existing import path once we emit installModRequested.
  install_watcher_.setFuture(QtConcurrent::run(
    [mod_url_std]() -> std::vector<nexus::File>
    {
      try
      {
        return nexus::Api::getModFiles(mod_url_std);
      }
      catch(const std::exception& e)
      {
        Log::error(std::string("NexusMods browser: failed to fetch mod files: ") + e.what());
        return {};
      }
    }));
}

void NexusBrowserDialog::onInstallFilesFetched()
{
  const std::vector<nexus::File> files = install_watcher_.result();
  ui->install_button->setEnabled(true);

  if(files.empty())
  {
    ui->status_label->setText("No installable files found (or the request failed).");
    return;
  }

  // Prefer the primary file, fall back to the first available file.
  const nexus::File* chosen = &files.front();
  for(const nexus::File& file : files)
  {
    if(file.is_primary)
    {
      chosen = &file;
      break;
    }
  }

  // Locate the result for the queued mod id to build the mod page URL.
  auto iter = std::find_if(results_.begin(),
                           results_.end(),
                           [this](const nexus::SearchResult& r)
                           { return r.mod.mod_id == pending_install_mod_id_; });
  if(iter == results_.end())
  {
    ui->status_label->setText("Selected mod is no longer available.");
    return;
  }

  const QString mod_url =
    QString("https://www.nexusmods.com/%1/mods/%2")
      .arg(QString::fromStdString(iter->domain_name))
      .arg(iter->mod.mod_id);

  // Emit data compatible with MainWindow::onModDownloadRequested so the dialog routes
  // through the existing download/import flow without reimplementing downloading.
  emit installModRequested(app_id_,
                           static_cast<int>(iter->mod.mod_id),
                           static_cast<int>(chosen->file_id),
                           mod_url,
                           QString::fromStdString(chosen->version));
  ui->status_label->setText(
    QString("Queued \"%1\" for download.").arg(QString::fromStdString(iter->mod.name)));
}
