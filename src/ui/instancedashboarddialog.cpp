// fork #203: instance dashboard with mod/plugin counts, total size and quick status

#include "instancedashboarddialog.h"
#include "ui_instancedashboarddialog.h"


InstanceDashboardDialog::InstanceDashboardDialog(const InstanceDashboardStats& stats,
                                                 QWidget* parent) :
  QDialog(parent), ui(new Ui::InstanceDashboardDialog)
{
  ui->setupUi(this);

  const QString app_name = stats.app_name.isEmpty() ? QStringLiteral("unknown") : stats.app_name;
  setWindowTitle(QString("Instance Dashboard - %1").arg(app_name));
  ui->app_name_value->setText(app_name);

  ui->total_mods_value->setText(QString::number(stats.total_mods));
  ui->enabled_mods_value->setText(QString::number(stats.enabled_mods));
  ui->disabled_mods_value->setText(QString::number(stats.disabled_mods));

  if(stats.plugin_count < 0)
    ui->plugin_count_value->setText(QStringLiteral("—"));
  else
    ui->plugin_count_value->setText(QString::number(stats.plugin_count));

  ui->total_size_value->setText(formatSize(stats.total_staging_bytes));

  ui->last_deploy_value->setText(stats.last_deploy.isEmpty() ? QStringLiteral("—")
                                                             : stats.last_deploy);

  ui->status_value->setText(stats.status_line.isEmpty() ? QStringLiteral("—")
                                                        : stats.status_line);
}

InstanceDashboardDialog::~InstanceDashboardDialog()
{
  delete ui;
}

QString InstanceDashboardDialog::formatSize(qint64 bytes)
{
  if(bytes < 0)
    return QStringLiteral("unknown");
  if(bytes < 1024)
    return QString("%1 B").arg(bytes);

  const char* const units[] = { "KiB", "MiB", "GiB", "TiB", "PiB" };
  double size = static_cast<double>(bytes);
  int unit = -1;
  do
  {
    size /= 1024.0;
    unit++;
  } while(size >= 1024.0 && unit < 4);

  return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unit]);
}
