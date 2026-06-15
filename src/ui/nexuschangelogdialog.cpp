#include "nexuschangelogdialog.h"
#include "core/nexus/api.h"
#include "ui_nexuschangelogdialog.h"
#include <QtConcurrent/QtConcurrent>


NexusChangelogDialog::NexusChangelogDialog(const QString& domain_name,
                                           long mod_id,
                                           const QString& target_version,
                                           QWidget* parent) :
  QDialog(parent), ui(new Ui::NexusChangelogDialog), domain_name_(domain_name), mod_id_(mod_id),
  target_version_(target_version)
{
  ui->setupUi(this);
  setWindowTitle("Changelog");
  ui->status_label->setText("Loading changelog...");

  connect(&watcher_, &QFutureWatcher<Changelog>::finished, this,
          &NexusChangelogDialog::onFetchFinished);

  const std::string domain = domain_name_.toStdString();
  const long id = mod_id_;
  // getModChangelogs degrades gracefully (returns empty + logs) on failure.
  auto future = QtConcurrent::run([domain, id]() -> Changelog
                                  { return nexus::Api::getModChangelogs(domain, id); });
  watcher_.setFuture(future);
}

NexusChangelogDialog::~NexusChangelogDialog()
{
  watcher_.waitForFinished();
  delete ui;
}

void NexusChangelogDialog::onFetchFinished()
{
  const Changelog changelog = watcher_.result();
  if(changelog.empty())
  {
    ui->status_label->setText("No changelog available for this mod.");
    return;
  }
  ui->status_label->setText(QString("Changelog for mod %1:").arg(mod_id_));
  displayChangelog(changelog);
}

void NexusChangelogDialog::displayChangelog(const Changelog& changelog)
{
  ui->changelog->clear();
  QString html;
  for(const auto& [version, changes] : changelog)
  {
    const bool is_target =
      !target_version_.isEmpty() && QString::fromStdString(version) == target_version_;
    html += QString("<h2>Version %1%2</h2>")
              .arg(QString::fromStdString(version).toHtmlEscaped())
              .arg(is_target ? " (target)" : "");
    html += "<ul>";
    for(const auto& change : changes)
      html += QString("<li>%1</li>").arg(QString::fromStdString(change).toHtmlEscaped());
    html += "</ul>";
  }
  ui->changelog->setHtml(html);
  ui->changelog->moveCursor(QTextCursor::Start);
}
