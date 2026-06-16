// fork #54: dialog listing deploy restore points (load-order snapshots).

#include "restorepointsdialog.h"
#include "ui_restorepointsdialog.h"
#include <QDateTime>
#include <QMessageBox>


RestorePointsDialog::RestorePointsDialog(const std::vector<RestorePoint>& points,
                                         QWidget* parent) :
  QDialog(parent), ui(new Ui::RestorePointsDialog)
{
  ui->setupUi(this);
  setWindowTitle("Restore Points");

  for(const auto& point : points)
  {
    const QString time =
      QDateTime::fromSecsSinceEpoch(point.timestamp).toString("yyyy-MM-dd HH:mm:ss");
    QString name = QString::fromStdString(point.name);
    QString text = name.isEmpty() ? time : QString("%1  (%2)").arg(name, time);
    ui->list->addItem(text);
  }
  if(ui->list->count() > 0)
    ui->list->setCurrentRow(0);

  const bool has_points = ui->list->count() > 0;
  ui->restore_button->setEnabled(has_points);
  ui->delete_button->setEnabled(has_points);
}

RestorePointsDialog::~RestorePointsDialog()
{
  delete ui;
}

void RestorePointsDialog::on_restore_button_clicked()
{
  const int index = ui->list->currentRow();
  if(index < 0)
    return;
  emit restoreRequested(index);
  accept();
}

void RestorePointsDialog::on_delete_button_clicked()
{
  const int index = ui->list->currentRow();
  if(index < 0)
    return;
  if(QMessageBox::question(this,
                           "Delete Restore Point",
                           "Are you sure you want to delete this restore point?",
                           QMessageBox::Yes | QMessageBox::No,
                           QMessageBox::No) != QMessageBox::Yes)
    return;
  emit deleteRequested(index);
  accept();
}

void RestorePointsDialog::on_close_button_clicked()
{
  reject();
}
