#include "conflictdetaildialog.h"
#include "ui_conflictdetaildialog.h"
#include <QString>
#include <QListWidget>


ConflictDetailDialog::ConflictDetailDialog(int mod_id,
                                           const QString& mod_name,
                                           const std::vector<ConflictInfo>& conflicts,
                                           QWidget* parent) :
  QDialog(parent), ui(new Ui::ConflictDetailDialog)
{
  ui->setupUi(this);
  setWindowTitle("Conflict detail for \"" + mod_name + "\"");

  for(const auto& info : conflicts)
  {
    // mod_ids are stored in load-order sequence; the last element is the winner
    // (the mod whose file is actually deployed to the target directory).
    if(info.mod_ids.empty())
      continue;

    const QString file_path = QString::fromStdString(info.file);

    if(info.mod_ids.back() == mod_id)
    {
      // This mod is the winner for this file.
      ui->wins_list->addItem(file_path);
    }
    else
    {
      // Another mod overwrites this mod's file.  Identify the winning mod.
      // mod_names is parallel to mod_ids and is filled by ModdedApplication::getFileConflicts.
      QString winner_label;
      if(!info.mod_names.empty())
        winner_label = QString::fromStdString(info.mod_names.back()) + " [" +
                       QString::number(info.mod_ids.back()) + "]";
      else
        winner_label = "[mod " + QString::number(info.mod_ids.back()) + "]";

      ui->losses_list->addItem(file_path + "  →  " + winner_label);
    }
  }

  // Update group-box titles with counts for a quick overview.
  ui->wins_group->setTitle("Files this mod wins  (" +
                           QString::number(ui->wins_list->count()) + ")");
  ui->losses_group->setTitle("Files this mod loses  (" +
                             QString::number(ui->losses_list->count()) + ")");

  // Fix for limo-app/limo#36: when both lists are empty the dialog showed two
  // blank panels with no explanation.  Hide the lists and show a plain message
  // instead so the user knows the mod simply has no conflicting files.
  const bool has_any = ui->wins_list->count() > 0 || ui->losses_list->count() > 0;
  ui->no_conflicts_label->setVisible(!has_any);
  ui->wins_group->setVisible(has_any);
  ui->losses_group->setVisible(has_any);
}

ConflictDetailDialog::~ConflictDetailDialog()
{
  delete ui;
}
