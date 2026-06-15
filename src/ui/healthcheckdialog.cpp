// fork #50: 'Problems' / health-check panel

#include "healthcheckdialog.h"
#include "ui_healthcheckdialog.h"
#include <QString>


HealthCheckDialog::HealthCheckDialog(const Deployer::HealthCheckResult& result, QWidget* parent) :
  QDialog(parent), ui(new Ui::HealthCheckDialog)
{
  ui->setupUi(this);
  const QString name = QString::fromStdString(result.deployer_name);
  setWindowTitle(QString("Health Check - %1").arg(name));

  if(result.isHealthy())
  {
    ui->summary_label->setText(
      QString("No problems found for '%1': %2 deployed files checked.")
        .arg(name)
        .arg(result.verification.total_checked));
  }
  else
  {
    ui->summary_label->setText(
      QString("'%1': %2 problems found - %3 orphaned files, %4 broken/incorrect links, "
              "%5 conflicts.")
        .arg(name)
        .arg(result.numProblems())
        .arg(result.orphaned.size())
        .arg(result.numBrokenLinks())
        .arg(result.conflicts.size()));
  }

  appendSection("Orphaned (recorded as deployed but owning mod no longer enabled)",
                result.orphaned);
  appendSection("Missing (file no longer present in target)", result.verification.missing);
  appendSection(result.verification.used_checksums
                  ? "Broken links (link broken / content differs from source)"
                  : "Broken links (link no longer points to source)",
                result.verification.modified);
  appendSection("Wrong link type (e.g. real file where a link was expected)",
                result.verification.not_a_link);
  appendSection("Source missing (staged file gone, cannot verify)",
                result.verification.source_missing);
  appendConflicts(result.conflicts);

  if(result.isHealthy())
    ui->details_edit->setPlainText("No problems detected.");
  else
    ui->details_edit->moveCursor(QTextCursor::Start);
}

HealthCheckDialog::~HealthCheckDialog()
{
  delete ui;
}

void HealthCheckDialog::appendSection(const QString& title,
                                      const std::vector<std::filesystem::path>& paths)
{
  if(paths.empty())
    return;
  ui->details_edit->appendPlainText(QString("== %1 (%2) ==").arg(title).arg(paths.size()));
  for(const auto& path : paths)
    ui->details_edit->appendPlainText(QString::fromStdString(path.string()));
  ui->details_edit->appendPlainText("");
}

void HealthCheckDialog::appendConflicts(
  const std::vector<Deployer::HealthCheckResult::Conflict>& conflicts)
{
  if(conflicts.empty())
    return;
  ui->details_edit->appendPlainText(
    QString("== Conflicts (files claimed by multiple enabled mods) (%1) ==").arg(conflicts.size()));
  for(const auto& conflict : conflicts)
  {
    QStringList mod_ids;
    for(int mod_id : conflict.mod_ids)
      mod_ids << QString::number(mod_id);
    ui->details_edit->appendPlainText(
      QString("%1  [mods: %2, winner: %3]")
        .arg(QString::fromStdString(conflict.path.string()))
        .arg(mod_ids.join(", "))
        .arg(conflict.winner_mod_id));
  }
  ui->details_edit->appendPlainText("");
}
