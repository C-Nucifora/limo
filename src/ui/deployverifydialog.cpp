// fork #53: deployment integrity verification

#include "deployverifydialog.h"
#include "ui_deployverifydialog.h"
#include <QString>


DeployVerifyDialog::DeployVerifyDialog(const QString& deployer_name,
                                       const Deployer::VerificationResult& result,
                                       QWidget* parent) :
  QDialog(parent), ui(new Ui::DeployVerifyDialog)
{
  ui->setupUi(this);
  setWindowTitle(QString("Verify Deployment - %1").arg(deployer_name));

  if(result.isClean())
  {
    ui->summary_label->setText(
      QString("Deployment of '%1' is intact: all %2 deployed files verified successfully.")
        .arg(deployer_name)
        .arg(result.total_checked));
  }
  else
  {
    ui->summary_label->setText(
      QString("Deployment of '%1': checked %2 files - %3 missing, %4 modified, "
              "%5 wrong link type, %6 with missing source.")
        .arg(deployer_name)
        .arg(result.total_checked)
        .arg(result.missing.size())
        .arg(result.modified.size())
        .arg(result.not_a_link.size())
        .arg(result.source_missing.size()));
  }

  appendSection("Missing (file no longer present in target)", result.missing);
  appendSection(result.used_checksums ? "Modified (link broken / content differs from source)"
                                      : "Modified (link no longer points to source)",
                result.modified);
  appendSection("Wrong link type (e.g. real file where a link was expected)", result.not_a_link);
  appendSection("Source missing (staged file gone, cannot verify)", result.source_missing);

  if(result.isClean())
    ui->details_edit->setPlainText("No drift detected.");
  else
    ui->details_edit->moveCursor(QTextCursor::Start);
}

DeployVerifyDialog::~DeployVerifyDialog()
{
  delete ui;
}

void DeployVerifyDialog::appendSection(const QString& title,
                                       const std::vector<std::filesystem::path>& paths)
{
  if(paths.empty())
    return;
  ui->details_edit->appendPlainText(QString("== %1 (%2) ==").arg(title).arg(paths.size()));
  for(const auto& path : paths)
    ui->details_edit->appendPlainText(QString::fromStdString(path.string()));
  ui->details_edit->appendPlainText("");
}
