#include "repositoriesdialog.h"
#include "../core/cryptography.h"
#include "../core/log.h"
#include "ui_repositoriesdialog.h"
#include <QApplication>
#include <QtConcurrent/QtConcurrent>
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

  // Network operations run off the UI thread; their watchers deliver results back here (audit F080).
  connect(&packages_watcher_,
          &QFutureWatcher<std::vector<remote::RemotePackage>>::finished,
          this,
          &RepositoriesDialog::onPackagesFetched);
  connect(&connect_watcher_,
          &QFutureWatcher<std::optional<std::string>>::finished,
          this,
          &RepositoriesDialog::onConnectFetched);
  connect(&resolve_watcher_,
          &QFutureWatcher<std::optional<std::string>>::finished,
          this,
          &RepositoriesDialog::onResolveFetched);

  ui->install_button->setEnabled(false);
}

RepositoriesDialog::~RepositoriesDialog()
{
  if(is_busy_)
    QApplication::restoreOverrideCursor();
  saveRepos();
  delete ui;
}

QString RepositoriesDialog::repoKey(const RepoConfig& config)
{
  // Identify a repository by URL + user; this stays stable when repos are added/removed (unlike
  // the list index) so it is a safe key for the package cache.
  return config.url + "\n" + config.user;
}

void RepositoriesDialog::setBusy(bool busy)
{
  // Only push/pop the override cursor on an actual transition so the cursor stack stays balanced.
  if(busy && !is_busy_)
    QApplication::setOverrideCursor(Qt::WaitCursor);
  else if(!busy && is_busy_)
    QApplication::restoreOverrideCursor();
  is_busy_ = busy;

  // Disabling the repo list while a request is in flight also prevents overlapping fetches.
  ui->add_repo_button->setEnabled(!busy);
  ui->remove_repo_button->setEnabled(!busy);
  ui->refresh_button->setEnabled(!busy);
  ui->repo_list->setEnabled(!busy);
  if(busy)
    ui->install_button->setEnabled(false);
  ui->package_label->setText(busy ? tr("Packages (loading…)") : tr("Packages"));
}

void RepositoriesDialog::populatePackageTree()
{
  ui->package_tree->clear();
  for(const remote::RemotePackage& package : current_packages_)
  {
    auto* item = new QTreeWidgetItem(ui->package_tree);
    item->setText(0, QString::fromStdString(package.name));
    item->setText(1, QString::fromStdString(package.version));
    item->setText(2, QString::fromStdString(package.category));
  }
  ui->install_button->setEnabled(!current_packages_.empty());
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
    // Current format: the password is AES-256-GCM encrypted into an opaque token. Older configs
    // stored it base64-obfuscated under "password"; read that as a fallback and it will be
    // re-saved encrypted on the next change (migration).
    const QString token = obj.value("password_enc").toString();
    if(!token.isEmpty())
    {
      if(const auto decrypted = cryptography::decryptFromToken(token.toStdString()))
        config.password = QString::fromStdString(*decrypted);
    }
    else
    {
      const QString encoded = obj.value("password").toString();
      if(!encoded.isEmpty())
        config.password = QString::fromUtf8(QByteArray::fromBase64(encoded.toUtf8()));
    }
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
    {
      try
      {
        // Store the password AES-256-GCM encrypted (per-installation key) rather than as
        // reversible base64; the legacy "password" field is intentionally not written.
        obj.insert("password_enc",
                   QString::fromStdString(
                     cryptography::encryptToToken(config.password.toStdString())));
      }
      catch(const CryptographyError& error)
      {
        Log::error(std::string("Could not encrypt repository password; it was not saved: ") +
                   error.what());
      }
    }
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
                              tr("The password will be stored encrypted (AES-256-GCM) using a "
                                 "per-installation key. It is protected against casual reading of "
                                 "the configuration file, but someone with access to both this "
                                 "file and your Limo key file could still recover it. Save it to "
                                 "disk?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);
      config.save_password = answer == QMessageBox::Yes;
    }
  }

  // Try to connect to obtain a friendly title; fall back to the URL. The connect happens off the
  // UI thread (audit F080); onConnectFetched() finalises the add when it returns.
  pending_add_config_ = config;
  setBusy(true);
  const std::string repo_url = config.url.toStdString();
  const std::string repo_user = config.user.toStdString();
  const std::string repo_password = config.password.toStdString();
  connect_watcher_.setFuture(QtConcurrent::run(
    [repo_url, repo_user, repo_password]() -> std::optional<std::string>
    {
      remote::OmmRepository repo(repo_url, repo_user, repo_password);
      if(repo.connect())
        return repo.title();
      return std::nullopt;
    }));
}

void RepositoriesDialog::onConnectFetched()
{
  setBusy(false);
  RepoConfig config = pending_add_config_;
  const std::optional<std::string> title = connect_watcher_.result();
  if(title)
    config.name = QString::fromStdString(*title);
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
  package_cache_.erase(repoKey(repos_[index]));
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
  if(index < 0)
    return;
  // Refresh forces a fresh network fetch by dropping any cached listing for this repo.
  package_cache_.erase(repoKey(repos_[index]));
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
  const QString key = repoKey(config);

  // F163: serve a previously fetched listing from the cache instead of hitting the network on
  // every selection change. A manual Refresh drops the cache entry to force a re-fetch.
  const auto cached = package_cache_.find(key);
  if(cached != package_cache_.end())
  {
    current_packages_ = cached->second;
    populatePackageTree();
    return;
  }

  // F080: run the listing off the UI thread so a slow/unreachable repository does not freeze the
  // GUI. onPackagesFetched() applies the result. The lambda captures plain copies so it stays
  // valid even if the dialog is closed mid-request.
  pending_packages_key_ = key;
  setBusy(true);
  const std::string url = config.url.toStdString();
  const std::string user = config.user.toStdString();
  const std::string password = config.password.toStdString();
  packages_watcher_.setFuture(QtConcurrent::run(
    [url, user, password]()
    {
      remote::OmmRepository repo(url, user, password);
      return repo.listPackages();
    }));
}

void RepositoriesDialog::onPackagesFetched()
{
  setBusy(false);
  std::vector<remote::RemotePackage> packages = packages_watcher_.result();
  // Cache only non-empty listings, under the repo they were requested for (even if the user has
  // since navigated away). An empty result can mean a transient failure or unreachable host, so it
  // is left uncached to allow a retry on the next visit rather than sticking until a manual refresh.
  if(!packages.empty())
    package_cache_[pending_packages_key_] = packages;

  // Only display it if the current selection still matches the request that produced it.
  const int index = selectedRepoIndex();
  if(index < 0 || repoKey(repos_[index]) != pending_packages_key_)
    return;

  current_packages_ = std::move(packages);
  if(current_packages_.empty())
  {
    QMessageBox::information(this,
                             tr("No Packages"),
                             tr("No packages were found for this repository (it may be "
                                "unreachable or empty)."));
    return;
  }
  populatePackageTree();
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

  // Stash the package metadata so onResolveFetched() can build the download info once the URL
  // resolution (a network call, run off the UI thread per audit F080) completes.
  pending_install_info_ = remote::RemoteDownloadInfo{};
  pending_install_info_.package_name = package.name;
  pending_install_info_.version = package.version;
  pending_install_info_.file_name =
    package.files.empty() ? package.name : package.files.front().file_name;

  setBusy(true);
  const std::string url = config.url.toStdString();
  const std::string user = config.user.toStdString();
  const std::string password = config.password.toStdString();
  const remote::RemotePackage package_copy = package;
  resolve_watcher_.setFuture(QtConcurrent::run(
    [url, user, password, package_copy]()
    {
      remote::OmmRepository repo(url, user, password);
      return repo.resolveDownloadUrl(package_copy);
    }));
}

void RepositoriesDialog::onResolveFetched()
{
  setBusy(false);
  const std::optional<std::string> download_url = resolve_watcher_.result();
  // Re-enable the install button to match the current selection now that the request finished.
  ui->install_button->setEnabled(ui->package_tree->currentItem() != nullptr);
  if(!download_url)
  {
    QMessageBox::warning(this,
                         tr("Install Failed"),
                         tr("Could not resolve a download URL for the selected package."));
    return;
  }

  pending_install_info_.download_url = *download_url;
  emit installPackageRequested(pending_install_info_);
}
