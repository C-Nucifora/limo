// fork #202: plugin ESM/ESL flag awareness.

#include "pluginflagsdialog.h"
#include "ui_pluginflagsdialog.h"
#include <QHeaderView>
#include <QTableWidgetItem>
#include <format>


PluginFlagsDialog::PluginFlagsDialog(const std::vector<PluginDeployer::PluginFlagInfo>& plugins,
                                     QWidget* parent) :
  QDialog(parent), ui(new Ui::PluginFlagsDialog)
{
  ui->setupUi(this);
  setWindowTitle("Plugin Flags");

  int full_count = 0;
  int light_count = 0;

  ui->plugin_table->setColumnCount(3);
  ui->plugin_table->setHorizontalHeaderLabels({ "Name", "Master (ESM)", "Light (ESL)" });
  ui->plugin_table->setRowCount(static_cast<int>(plugins.size()));
  for(int row = 0; row < static_cast<int>(plugins.size()); row++)
  {
    const auto& info = plugins[row];
    if(info.is_light)
      light_count++;
    else
      full_count++;

    auto* name_item = new QTableWidgetItem(info.name.c_str());
    if(!info.exists)
      name_item->setToolTip("Plugin file is missing or not a valid TES4 plugin; flags inferred "
                            "from the file extension only.");
    ui->plugin_table->setItem(row, 0, name_item);
    ui->plugin_table->setItem(row, 1, new QTableWidgetItem(info.is_master ? "Yes" : ""));
    ui->plugin_table->setItem(row, 2, new QTableWidgetItem(info.is_light ? "Yes" : ""));
  }
  ui->plugin_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui->plugin_table->resizeColumnToContents(1);
  ui->plugin_table->resizeColumnToContents(2);

  ui->count_label->setText(std::format("{} / {} full, {} / {} light",
                                       full_count,
                                       FULL_CAP,
                                       light_count,
                                       LIGHT_CAP)
                             .c_str());
}

PluginFlagsDialog::~PluginFlagsDialog()
{
  delete ui;
}
