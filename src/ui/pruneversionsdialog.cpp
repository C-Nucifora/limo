#include "pruneversionsdialog.h"
#include "ui_pruneversionsdialog.h"
#include <QPushButton>


PruneVersionsDialog::PruneVersionsDialog(
  const std::vector<std::pair<std::filesystem::path, unsigned long>>& archives,
  unsigned long total_size,
  QWidget* parent) :
  QDialog(parent), ui(new Ui::PruneVersionsDialog)
{
  ui->setupUi(this);
  setWindowTitle("Remove old archive versions");

  for(const auto& [path, size] : archives)
    ui->archive_list->addItem(
      QString("%1  (%2)").arg(QString::fromStdString(path.filename().string()),
                              formatSize(size)));

  if(archives.empty())
    ui->info_label->setText("No outdated archive versions were found to remove.");

  ui->total_label->setText("Total size freed: " + formatSize(total_size));

  auto* delete_button = ui->button_box->addButton("Delete", QDialogButtonBox::AcceptRole);
  delete_button->setEnabled(!archives.empty());
}

PruneVersionsDialog::~PruneVersionsDialog()
{
  delete ui;
}

QString PruneVersionsDialog::formatSize(unsigned long bytes)
{
  static const char* const units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  double size = static_cast<double>(bytes);
  int unit = 0;
  while(size >= 1024.0 && unit < 4)
  {
    size /= 1024.0;
    unit++;
  }
  if(unit == 0)
    return QString("%1 %2").arg(bytes).arg(units[unit]);
  return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unit]);
}
