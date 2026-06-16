#include "repositoriesdialog.h"
#include "ui_repositoriesdialog.h"
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTreeWidgetItem>


RepositoriesDialog::RepositoriesDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::RepositoriesDialog)
{
  ui->setupUi(this);
  loadRepos();
  refreshRepoList();

  connect(ui->add_repo_button, &QPushButton::clicked, this, &RepositoriesDialog::onAddRepoClicked);
  connect(
    ui->remove_repo_button, &QPushButton::clicked, this, &RepositoriesDialog::onRemoveRepoClicked);
  connect(ui->repo_list,
          &QListWidget::currentRowChanged,
          this,
          &RepositoriesDialog::onRepoSelectionChanged);
  connect(ui->refresh_button, &QPushButton::clicked, this, &RepositoriesDialog::onRefreshClicked);
  connect(ui->install_button, &QPushButton::clicked, this, &RepositoriesDialog::onInstallClicked);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &RepositoriesDialog::reject);

  ui->install_button->setEnabled(false);
}

RepositoriesDialog::~RepositoriesDialog()
{
  saveRepos();
  delete ui;
}

QString RepositoriesDialog::configFilePath()
{
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/repositories.json";
}

void RepositoriesDialog::loadRepos()
{
  repos_.clear();
  QFile file(configFilePath());
  if(!file.open(QIODevice::ReadOnly))
    return;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if(!doc.isArray())
    return;
  for(const QJsonValue& value : doc.array())
  {
    const QJsonObject obj = value.toObject();
    RepoConfig config;
    config.name = obj.value("name").toString();
    config.url = obj.value("url").toString();
    config.user = obj.value("user").toString();
    config.save_password = obj.value("save_password").toBool();
    // Passwords are stored base64-obfuscated only (see class docs).
    const QString encoded = obj.value("password").toString();
    if(!encoded.isEmpty())
      config.password =
        QString::fromUtf8(QByteArray::fromBase64(encoded.toUtf8()));
    if(!config.url.isEmpty())
      repos_.push_back(config);
  }
}

void RepositoriesDialog::saveRepos() const
{
  QJsonArray array;
  for(const RepoConfig& config : repos_)
  {
    QJsonObject obj;
    obj.insert("name", config.name);
    obj.insert("url", config.url);
    obj.insert("user", config.user);
    obj.insert("save_password", config.save_password);
    if(config.save_password && !config.password.isEmpty())
      obj.insert("password",
                 QString::fromUtf8(config.password.toUtf8().toBase64()));
    array.append(obj);
  }
  QFile file(configFilePath());
  if(file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    // Restrict to owner read/write before writing any credentials. The stored
    // password is only base64-obfuscated (recoverable), so the file must not be
    // readable by other users on the system.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
  }
}

void RepositoriesDialog::refreshRepoList()
{
  ui->repo_list->clear();
  for(const RepoConfig& config : repos_)
    ui->repo_list->addItem(config.name.isEmpty() ? config.url : config.name);
}

int RepositoriesDialog::selectedRepoIndex() const
{
  const int row = ui->repo_list->currentRow();
  if(row < 0 || row >= static_cast<int>(repos_.size()))
    return -1;
  return row;
}

void RepositoriesDialog::onAddRepoClicked()
{
  bool ok = false;
  const QString url =
    QInputDialog::getText(this,
                          tr("Add Repository"),
                          tr("Repository descriptor URL:"),
                          QLineEdit::Normal,
                          QString(),
                          &ok);
  if(!ok || url.trimmed().isEmpty())
    return;

  RepoConfig config;
  config.url = url.trimmed();
  config.user = QInputDialog::getText(this,
                                      tr("Add Repository"),
                                      tr("User name (optional):"),
                                      QLineEdit::Normal,
                                      QString(),
                                      &ok);
  if(ok && !config.user.isEmpty())
  {
    config.password = QInputDialog::getText(this,
                                            tr("Add Repository"),
                                            tr("Password (optional):"),
                                            QLineEdit::Password,
                                            QString(),
                                            &ok);
    if(ok && !config.password.isEmpty())
    {
      const QMessageBox::StandardButton answer =
        QMessageBox::question(this,
                              tr("Save Password"),
                              tr("WARNING: the password will only be base64 obfuscated, "
                                 "NOT encrypted. Anyone who can read the configuration "
                                 "file can recover it in plain text. Save it to disk "
                                 "anyway?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);
      config.save_password = answer == QMessageBox::Yes;
    }
  }

  // Try to connect to obtain a friendly title; fall back to the URL.
  remote::OmmRepository repo(
    config.url.toStdString(), config.user.toStdString(), config.password.toStdString());
  if(repo.connect())
    config.name = QString::fromStdString(repo.title());
  else
  {
    QMessageBox::warning(this,
                         tr("Connection Failed"),
                         tr("Could not connect to or parse the repository. It will be "
                            "added anyway; check the URL and credentials."));
    config.name = config.url;
  }

  repos_.push_back(config);
  refreshRepoList();
  ui->repo_list->setCurrentRow(static_cast<int>(repos_.size()) - 1);
  saveRepos();
}

void RepositoriesDialog::onRemoveRepoClicked()
{
  const int index = selectedRepoIndex();
  if(index < 0)
    return;
  repos_.erase(repos_.begin() + index);
  refreshRepoList();
  ui->package_tree->clear();
  current_packages_.clear();
  ui->install_button->setEnabled(false);
  saveRepos();
}

void RepositoriesDialog::onRepoSelectionChanged()
{
  const int index = selectedRepoIndex();
  if(index < 0)
  {
    ui->package_tree->clear();
    current_packages_.clear();
    ui->install_button->setEnabled(false);
    return;
  }
  loadPackages(index);
}

void RepositoriesDialog::onRefreshClicked()
{
  const int index = selectedRepoIndex();
  if(index >= 0)
    loadPackages(index);
}

void RepositoriesDialog::loadPackages(int index)
{
  ui->package_tree->clear();
  current_packages_.clear();
  ui->install_button->setEnabled(false);
  if(index < 0 || index >= static_cast<int>(repos_.size()))
    return;

  const RepoConfig& config = repos_[index];
  QApplication::setOverrideCursor(Qt::WaitCursor);
  remote::OmmRepository repo(
    config.url.toStdString(), config.user.toStdString(), config.password.toStdString());
  current_packages_ = repo.listPackages();
  QApplication::restoreOverrideCursor();

  if(current_packages_.empty())
  {
    QMessageBox::information(this,
                             tr("No Packages"),
                             tr("No packages were found for this repository (it may be "
                                "unreachable or empty)."));
    return;
  }

  for(const remote::RemotePackage& package : current_packages_)
  {
    auto* item = new QTreeWidgetItem(ui->package_tree);
    item->setText(0, QString::fromStdString(package.name));
    item->setText(1, QString::fromStdString(package.version));
    item->setText(2, QString::fromStdString(package.category));
  }
  ui->install_button->setEnabled(true);
}

void RepositoriesDialog::onInstallClicked()
{
  const int repo_index = selectedRepoIndex();
  const int package_index = ui->package_tree->indexOfTopLevelItem(ui->package_tree->currentItem());
  if(repo_index < 0 || package_index < 0 ||
     package_index >= static_cast<int>(current_packages_.size()))
  {
    QMessageBox::information(
      this, tr("No Selection"), tr("Select a package to install first."));
    return;
  }

  const RepoConfig& config = repos_[repo_index];
  const remote::RemotePackage& package = current_packages_[package_index];

  remote::OmmRepository repo(
    config.url.toStdString(), config.user.toStdString(), config.password.toStdString());
  const std::optional<std::string> download_url = repo.resolveDownloadUrl(package);
  if(!download_url)
  {
    QMessageBox::warning(this,
                         tr("Install Failed"),
                         tr("Could not resolve a download URL for the selected package."));
    return;
  }

  remote::RemoteDownloadInfo info;
  info.package_name = package.name;
  info.version = package.version;
  info.file_name =
    package.files.empty() ? package.name : package.files.front().file_name;
  info.download_url = *download_url;
  emit installPackageRequested(info);
}
