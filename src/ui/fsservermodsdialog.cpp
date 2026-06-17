#include "fsservermodsdialog.h"
#include "../core/remote/fsservermods.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>


FsServerModsDialog::FsServerModsDialog(QWidget* parent) : QDialog(parent)
{
  setWindowTitle("Farming Simulator Server Mods");
  resize(440, 460);

  auto* layout = new QVBoxLayout(this);

  auto* intro = new QLabel(
    tr("Enter a Farming Simulator dedicated-server address. Limo fetches its mod list from "
       "<server>/mods and queues the mods you pick for download."),
    this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto* url_row = new QHBoxLayout();
  url_field_ = new QLineEdit(this);
  url_field_->setPlaceholderText(tr("e.g. 192.168.1.50:8080 or http://host:8080/mods"));
  url_field_->setText(
    QSettings(QCoreApplication::applicationName()).value("fs_server_url", "").toString());
  auto* fetch_button = new QPushButton(tr("Fetch"), this);
  url_row->addWidget(url_field_, 1);
  url_row->addWidget(fetch_button);
  layout->addLayout(url_row);
  connect(fetch_button, &QPushButton::clicked, this, &FsServerModsDialog::onFetchClicked);
  connect(url_field_, &QLineEdit::returnPressed, this, &FsServerModsDialog::onFetchClicked);

  list_ = new QListWidget(this);
  layout->addWidget(list_, 1);

  status_ = new QLabel(this);
  status_->setWordWrap(true);
  layout->addWidget(status_);

  auto* button_box = new QDialogButtonBox(this);
  auto* download_button =
    button_box->addButton(tr("Download Selected"), QDialogButtonBox::AcceptRole);
  button_box->addButton(QDialogButtonBox::Close);
  connect(download_button, &QPushButton::clicked, this, &FsServerModsDialog::onDownloadClicked);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(button_box);
}

void FsServerModsDialog::onFetchClicked()
{
  const QString url = url_field_->text().trimmed();
  if(url.isEmpty())
    return;
  QSettings(QCoreApplication::applicationName()).setValue("fs_server_url", url);

  list_->clear();
  status_->setText(tr("Fetching..."));
  QApplication::setOverrideCursor(Qt::WaitCursor);
  std::string error;
  const auto mods = remote::FsServerMods::fetchModList(url.toStdString(), error);
  QApplication::restoreOverrideCursor();

  if(!error.empty())
  {
    status_->setText(QString::fromStdString(error));
    return;
  }
  for(const auto& mod : mods)
  {
    auto* item = new QListWidgetItem(QString::fromStdString(mod.file_name), list_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    item->setData(Qt::UserRole, QString::fromStdString(mod.download_url));
  }
  status_->setText(tr("Found %1 mod(s). Uncheck any you don't want, then Download Selected.")
                     .arg(mods.size()));
}

void FsServerModsDialog::onDownloadClicked()
{
  QList<QStringList> selected;
  for(int i = 0; i < list_->count(); i++)
  {
    QListWidgetItem* item = list_->item(i);
    if(item->checkState() != Qt::Checked)
      continue;
    selected.append(QStringList{ item->text(), item->data(Qt::UserRole).toString() });
  }
  if(selected.isEmpty())
  {
    status_->setText(tr("No mods selected."));
    return;
  }
  emit downloadRequested(selected);
  accept();
}
