#include "nexusmoddialog.h"
#include <iomanip>
#include "core/log.h"
#include "src/core/nexus/integrityverifier.h"
#include "tablepushbutton.h"
#include "ui_nexusmoddialog.h"
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QSpacerItem>
#include <QUrl>
#include <algorithm>
#include <ranges>
#include <sstream>

namespace str = std::ranges;


NexusModDialog::NexusModDialog(QWidget* parent) : QDialog(parent), ui(new Ui::NexusModDialog)
{
  ui->setupUi(this);
  ui->files_widget->setLayout(new QVBoxLayout());
}

NexusModDialog::~NexusModDialog()
{
  delete ui;
}

void NexusModDialog::setupDialog(int app_id, int mod_id, const nexus::Page& page)
{
  app_id_ = app_id;
  mod_id_ = mod_id;
  page_ = page;
  QString description = page.mod.description.c_str();
  ui->description_box->setHtml(bbcodeToHtml(description));

  QString changelog;
  for(const auto& [version, changes] : page.changelog)
  {
    changelog.append(R"(<span style="font-size:18px"><b>)" +
                     QString::fromStdString(version).toHtmlEscaped() + R"(</b></span><ul>)");
    for(const auto& change : changes)
      changelog.append("<li>" + QString::fromStdString(change).toHtmlEscaped() + "</li>");
    changelog.append("</ul><br />");
  }
  ui->changelog_box->setHtml(changelog);

  resetFilesWidget();

  if(page.files.empty())
    return;

  const QString mod_link =
    std::format(
      "<span style=\"font-size:17px\"><b>" "<a href=\"https://nexusmods.com/{}/mods/{}\">Link To "
                                           "NexusMods Page</a></b></span>",
      page.mod.domain_name,
      page.mod.mod_id)
      .c_str();
  ui->link_label_desc->setText(mod_link);
  ui->link_label_changelog->setText(mod_link);
  ui->link_label_files->setText(
    std::format(
      "<span style=\"font-size:17px\"><b>" "<a "
                                           "href=\"https://nexusmods.com/{}/mods/"
                                           "{}?tab=files\">Link To NexusMods " "Page</a></b></"
                                                                               "span>",
      page.mod.domain_name,
      page.mod.mod_id)
      .c_str());

  std::vector<nexus::File> files = page.files;
  std::sort(files.begin(),
            files.end(),
            [](nexus::File f1, nexus::File f2)
            {
              if(f1.category_id != f2.category_id)
                return f1.category_id < f2.category_id;
              return f1.name < f2.name;
            });
  auto base_layout = qobject_cast<QVBoxLayout*>(ui->files_widget->layout());
  long cur_cat_id = -1;
  QString cur_cat_name = "";
  for(const auto& file : files)
  {
    if(file.category_id != cur_cat_id)
    {
      cur_cat_id = file.category_id;
      cur_cat_name = file.category_name.empty()
                       ? QStringLiteral("Other")
                       : QString::fromStdString(file.category_name).toHtmlEscaped();
      auto label = new QLabel();
      label->setTextFormat(Qt::RichText);
      label->setText(R"(<span style="font-size:18px"><b>)" + cur_cat_name + " Files</b></span>");
      base_layout->addWidget(label);
    }
    auto frame = new QFrame();
    frame->setFrameShadow(QFrame::Plain);
    frame->setFrameShape(QFrame::Panel);
    auto frame_layout = new QVBoxLayout();
    frame->setLayout(frame_layout);
    QString mod_text;
    mod_text.append(R"(<span style="font-size:17px"><b>)" +
                    QString::fromStdString(file.name).toHtmlEscaped() + "</b></span>");
    if(!file.version.empty())
      mod_text.append("<br /><b>Version: " +
                      QString::fromStdString(file.version).toHtmlEscaped() + "</b>");
    std::stringstream ss;
    std::tm tm_buf{};
    if(localtime_r(&file.uploaded_time, &tm_buf) != nullptr)
      ss << std::put_time(&tm_buf, "%F %T");
    else
      ss << "unknown";
    mod_text.append("<br /><b>Upload Time: " +
                    QString::fromStdString(ss.str()).toHtmlEscaped() + "</b>");
    QString size_string;
    long size = file.size_in_bytes;
    if(size < 1024)
      size_string = QString::number(size);
    else
    {
      long last_size = 0;
      int exp = 0;
      const std::vector<QString> units{ "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
      while(size > 1024 && static_cast<size_t>(exp) < units.size())
      {
        last_size = size;
        size /= 1024;
        exp++;
      }
      last_size /= 1.024;
      size_string = QString::number(size);
      const int first_digit = (last_size / 100) % 10;
      const int second_digit = (last_size / 10) % 10;
      if(first_digit != 0 || second_digit != 0)
        size_string += "." + QString::number(first_digit);
      if(second_digit != 0)
        size_string += QString::number(second_digit);
      size_string += " " + units[std::min(static_cast<size_t>(exp), units.size() - 1)];
    }
    mod_text.append("<br /><b>Size: " + size_string + "</b><br />");
    const QString virus_scan_url =
      sanitizeLinkUrl(QString::fromStdString(file.external_virus_scan_url));
    if(virus_scan_url.isEmpty())
      mod_text.append("<b>No external virus scan</b><br />");
    else
      mod_text.append("<b><a href=\"" + virus_scan_url +
                      "\">Virus scan link</a></b><br />");
    mod_text.append("<br /><br />" + bbcodeToHtml(file.description.c_str()) + "<br />");
    if(!file.changelog_html.empty())
      mod_text.append("<br /><b>Changes</b><br />" + bbcodeToHtml(file.changelog_html.c_str()) +
                      "<br />");
    auto text_label = new QLabel();
    text_label->setTextFormat(Qt::RichText);
    text_label->setText(mod_text);
    text_label->setWordWrap(true);
    text_label->setTextInteractionFlags(text_label->textInteractionFlags() |
                                        Qt::TextSelectableByMouse);
    frame_layout->addWidget(text_label);

    auto manual_link_label = new QLabel();
    manual_link_label->setTextFormat(Qt::RichText);
    manual_link_label->setText(
      std::format(
        "<b><a href=\"https://nexusmods.com/{}/mods/{}?tab=files&file_id={}\">" "Manual Download "
                                                                                "Link</a></b>",
        page.mod.domain_name,
        page.mod.mod_id,
        file.file_id)
        .c_str());
    manual_link_label->setOpenExternalLinks(true);
    frame_layout->addWidget(manual_link_label);

    auto manager_link_label = new QLabel();
    manager_link_label->setTextFormat(Qt::RichText);
    manager_link_label->setText(std::format(
                                  "<b><a "
                                  "href=\"https://nexusmods.com/{}/mods/"
                                  "{}?tab=files&file_id={}&nmm=1\">" "Mod Manager Download "
                                                                     "Link</a></b>",
                                  page.mod.domain_name,
                                  page.mod.mod_id,
                                  file.file_id)
                                  .c_str());
    manager_link_label->setOpenExternalLinks(true);
    frame_layout->addWidget(manager_link_label);
    QSettings settings(QCoreApplication::applicationName());
    settings.beginGroup("nexus");
    const bool is_premium = settings.value("info_is_premium", false).toBool();
    settings.endGroup();
    if(is_premium)
    {
      auto download_button = new TablePushButton(file.file_id, file.file_id);
      download_button->setText("Download");
      download_button->setIcon(QIcon::fromTheme("edit-download"));
      connect(
        download_button, &TablePushButton::clickedAt, this, &NexusModDialog::onDownloadClicked);

      frame_layout->addWidget(download_button);
    }

    auto verify_button = new TablePushButton(file.file_id, file.file_id);
    verify_button->setText("Verify integrity");
    verify_button->setIcon(QIcon::fromTheme("dialog-ok"));
    verify_button->setToolTip(
      "Select a downloaded archive and verify its MD5 hash and file size against NexusMods.");
    connect(verify_button, &TablePushButton::clickedAt, this, &NexusModDialog::onVerifyClicked);
    frame_layout->addWidget(verify_button);

    frame_layout->addStretch();
    base_layout->addWidget(frame);
  }
  base_layout->addStretch();
}

QString NexusModDialog::bbcodeToHtml(const QString& bbcode)
{
  QString html = bbcode;
  html.remove(QChar(0xFEFF));
  html.remove("\ufeff");
  html.replace("\xa0", " ");
  html.remove("\n");
  // Escape raw HTML so embedded markup in untrusted input cannot survive token substitution.
  html = html.toHtmlEscaped();

  std::vector<std::tuple<QString, QString, QString>> tokens = {
    { R"(\[center\])", R"(\[/center\])", R"(<center>\1</center>)" },
    { R"(\[b\])", R"(\[/b\])", R"(<b>\1</b>)" },
    { R"(\[i\])", R"(\[/i\])", R"(<i>\1</i>)" },
    { R"(\[u\])", R"(\[/u\])", R"(<u>\1</u>)" },
    { R"(\[s\])", R"(\[/s\])", R"(<s>\1</s>)" },
    { R"(\[url\])", R"(\[/url\])", R"(<a href="\1">\1</a>)" },
    { R"(\[url=(.*?)\])", R"(\[/url\])", R"(<a href="\1">\2</a>)" },
    { R"(\[youtube\])",
      R"(\[/youtube\])",
      R"(<a href="https://www.youtube.com/watch?v=\1">https://www.youtube.com/watch?v=\1</a>)" },
    // { R"(\[img\])", R"(\[/img\])", R"(<img src="\1" />)" },
    { R"(\[img\])", R"(\[/img\])", R"(<a href="\1"> [\1] </a>)" },
    { R"(\[quote\])", R"(\[/quote\])", R"(<blockquote>\1</blockquote>)" },
    { R"(\[quote=(.*?)\])", R"(\[/quote\])", R"(<blockquote><cite>\1</cite>\2</blockquote>)" },
    { R"(\[code\])", R"(\[/code\])", R"(<pre><code>\1</code></pre>)" },
    { R"(\[list\])", R"(\[/list\])", R"(<ul>\1</ul>)" },
    { R"(\[list=1\])", R"(\[/list\])", R"(<ol>\1</ol>)" },
    { R"(\[li\])", R"(\[/li\])", R"(<li>\1</li>)" },
    { R"(\[color=(.*?)\])", R"(\[/color\])", R"(<span style="color:\1">\2</span>)" },
    { R"(\[size=(.*?)\])", R"(\[/size\])", R"(<span style="font-size:\1">\2</span>)" },
    { R"(\[left\])", R"(\[/left\])", R"(<span align="left">\1</span>)" }
  };

  for(const auto& [begin, end, replace] : tokens)
  {
    while(html.contains(QRegularExpression(begin + R"((.*?))" + end)))
      html.replace(QRegularExpression(begin + "((?!" + begin + R"().*?))" + end), replace);
  }

  return html;
}

void NexusModDialog::resetFilesWidget()
{
  if(auto* old_layout = ui->files_widget->layout())
  {
    QLayoutItem* item;
    while((item = old_layout->takeAt(0)) != nullptr)
    {
      delete item->widget();
      delete item;
    }
    delete old_layout;
  }
  ui->files_widget->setLayout(new QVBoxLayout());
}

QString NexusModDialog::sanitizeLinkUrl(const QString& url)
{
  const QString trimmed = url.trimmed();
  if(trimmed.isEmpty())
    return {};
  const QString scheme = QUrl(trimmed).scheme().toLower();
  if(scheme != "http" && scheme != "https")
    return {};
  return trimmed.toHtmlEscaped();
}

void NexusModDialog::onDownloadClicked(int file_id, int file_id_copy)
{
  auto iter = str::find_if(page_.files, [file_id](auto f){return f.file_id == file_id;});
  if(iter == page_.files.end())
    Log::error(std::format("Failed to parse Nexus response for file: {}", file_id));
  else
    emit modDownloadRequested(app_id_, mod_id_, file_id, page_.url.c_str(), iter->version.c_str());
}

void NexusModDialog::onVerifyClicked(int file_id, int file_id_copy)
{
  auto iter = str::find_if(page_.files, [file_id](auto f) { return f.file_id == file_id; });
  if(iter == page_.files.end())
  {
    Log::error(std::format("Failed to find Nexus file for verification: {}", file_id));
    return;
  }

  const QString path = QFileDialog::getOpenFileName(
    this, "Select downloaded archive to verify", QString(), "All Files (*)");
  if(path.isEmpty())
    return;

  if(!nexus::Api::isInitialized())
  {
    QMessageBox::warning(this,
                         "Verification unavailable",
                         "A NexusMods API key is required to verify file integrity.");
    return;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  nexus::IntegrityVerifier::Result result;
  try
  {
    result =
      nexus::IntegrityVerifier::verify(path.toStdString(), *iter, page_.mod.domain_name);
  }
  catch(const std::exception& e)
  {
    QApplication::restoreOverrideCursor();
    Log::error(std::format("Integrity verification failed: {}", e.what()));
    QMessageBox::critical(
      this, "Verification error", QString("Failed to verify file:\n%1").arg(e.what()));
    return;
  }
  QApplication::restoreOverrideCursor();

  if(result.passed())
    QMessageBox::information(
      this, "Integrity verified", QString::fromStdString(result.message));
  else
    QMessageBox::warning(
      this, "Integrity check failed", QString::fromStdString(result.message));
}
