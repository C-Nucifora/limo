#include "mainwindow.h"
#include "../core/deployerfactory.h"
#include "../core/importers/mo2importer.h"
#include "../core/log.h"
#ifdef LIMO_WITH_LOOT
#include "../core/lootdeployer.h"
#endif
#include "./ui_mainwindow.h"
#include "addappdialog.h"
#include "adddeployerdialog.h"
#include "addmoddialog.h"
#include "addprofiledialog.h"
#include "addtodeployerdialog.h"
#include "addtooldialog.h"
#include "backuplistview.h"
#include "colors.h"
#include "core/consts.h"
#include "core/cryptography.h"
#include "core/deployerfactory.h"
#include "core/installer.h"
#include "deployerlistview.h"
#include "deploypreviewdialog.h" // fork feature #49: deploy dry-run / preview
#include "deployverifydialog.h" // fork #53
#include "deployedfilestreedialog.h" // fork #11
#include "healthcheckdialog.h" // fork #50
#include "editmanualtagsdialog.h"
#include "enterapipwdialog.h"
#ifdef LIMO_WITH_LOOT
#include "lootuserlistdialog.h" // fork #31
#endif
#include "modlistproxymodel.h"
#include "movemoddialog.h"
#include "settingsdialog.h"
#include "tablepushbutton.h"
#include "versionboxdelegate.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QInputDialog>
#include <QFile>
#include <QFileDialog>
#include <QDockWidget> // fork #8: host the Downloads panel
#include <QMessageBox>
#include <QMetaType>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QColorDialog> // fork #199
#include <QDragEnterEvent> // fork #16
#include <QDropEvent> // fork #16
#include <QMimeData> // fork #16
#include <QLabel> // fork #25
#include <QVBoxLayout> // fork #25
#include <QScrollBar>
#include <QSettings>
#include <QTextStream>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <numeric>
#include <ranges>
#include <regex>
#include <unordered_map>

#include <iostream>

namespace str = std::ranges;
namespace stv = std::views;
namespace sfs = std::filesystem;


Q_DECLARE_METATYPE(std::vector<ModInfo>);
Q_DECLARE_METATYPE(std::filesystem::path);
Q_DECLARE_METATYPE(std::string);
Q_DECLARE_METATYPE(DeployerInfo);
Q_DECLARE_METATYPE(std::vector<ConflictInfo>);
Q_DECLARE_METATYPE(AppInfo);
Q_DECLARE_METATYPE(std::unordered_set<int>);
Q_DECLARE_METATYPE(QList<QList<QString>>);
Q_DECLARE_METATYPE(EditApplicationInfo);
Q_DECLARE_METATYPE(EditDeployerInfo);
Q_DECLARE_METATYPE(std::vector<bool>);
Q_DECLARE_METATYPE(Log::LogLevel);
Q_DECLARE_METATYPE(std::vector<int>);
Q_DECLARE_METATYPE(std::vector<BackupTarget>);
Q_DECLARE_METATYPE(std::vector<EditManualTagAction>);
Q_DECLARE_METATYPE(std::vector<EditAutoTagAction>);
Q_DECLARE_METATYPE(EditProfileInfo);
Q_DECLARE_METATYPE(nexus::Page);
Q_DECLARE_METATYPE(ExternalChangesInfo);
Q_DECLARE_METATYPE(FileChangeChoices);
Q_DECLARE_METATYPE(Tool);
Q_DECLARE_METATYPE(ImportModInfo);
Q_DECLARE_METATYPE(std::vector<ModRule>);
Q_DECLARE_METATYPE(std::vector<std::string>);
Q_DECLARE_METATYPE(std::vector<std::vector<int>>);
// fork #8: queued connections carry the download queue snapshot across threads
Q_DECLARE_METATYPE(std::vector<DownloadQueueItem>);


MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  setupLog();
  updateOutdatedSettings();
  setupProgressBar();
  app_manager_ = new ApplicationManager();
  checkForContainers();
  setupLists();
  setupButtons();
  setupMenus();
  setupDialogs();
  setupFilters();
  setupConnections();
  app_manager_->init();
  worker_thread_ = new QThread(this);
  app_manager_->moveToThread(worker_thread_);
  worker_thread_->start();
  loadSettings();
  setTabWidgetStyleSheet();
  setupIcons();
  setupIpcServer();
  addAction(ui->actionSelect_All);
  // fork #24: add the save-game manager as a new tab in the main tab widget.
  save_manager_widget_ = new SaveManagerWidget(this);
  ui->app_tab_widget->addTab(save_manager_widget_, "Saves");
  setWindowTitle("Limo");

  // ---- fork #8: persistent download queue panel -----------------------------
  // Host the DownloadsWidget in a dockable panel and wire it to the (worker-thread)
  // ApplicationManager via queued connections. All queue mutations happen in the
  // ApplicationManager; the widget is a pure view refreshed by downloadQueueChanged.
  downloads_widget_ = new DownloadsWidget(this);
  auto* downloads_dock = new QDockWidget("Downloads", this);
  downloads_dock->setObjectName("downloads_dock");
  downloads_dock->setWidget(downloads_widget_);
  addDockWidget(Qt::BottomDockWidgetArea, downloads_dock);
  downloads_dock->hide(); // hidden by default; user can show it via the dock toggle
  qRegisterMetaType<std::vector<DownloadQueueItem>>("std::vector<DownloadQueueItem>");
  connect(app_manager_,
          &ApplicationManager::downloadQueueChanged,
          downloads_widget_,
          &DownloadsWidget::onDownloadQueueChanged);
  connect(downloads_widget_,
          &DownloadsWidget::requestDownloadQueue,
          app_manager_,
          &ApplicationManager::requestDownloadQueue);
  connect(downloads_widget_,
          &DownloadsWidget::cancelDownload,
          app_manager_,
          &ApplicationManager::cancelDownload);
  connect(downloads_widget_,
          &DownloadsWidget::retryDownload,
          app_manager_,
          &ApplicationManager::retryDownload);
  connect(downloads_widget_,
          &DownloadsWidget::removeDownload,
          app_manager_,
          &ApplicationManager::removeDownload);
  // request the initial queue snapshot (marshalled onto the worker thread)
  QMetaObject::invokeMethod(
    app_manager_, &ApplicationManager::requestDownloadQueue, Qt::QueuedConnection);
  // ---- end fork #8 ----------------------------------------------------------

  setupEmptyStateOverlay(); // fork #25

  Log::info("Startup complete");
}

MainWindow::~MainWindow()
{
  worker_thread_->quit();
  worker_thread_->wait(5000);
  if(worker_thread_->isRunning())
    worker_thread_->terminate();
  delete worker_thread_;
  delete ui;
  delete app_manager_;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  QSettings settings = QSettings(QCoreApplication::applicationName());
  settings.setValue("main/geometry", saveGeometry());
  settings.setValue("main/state", saveState());
  settings.setValue("current_tab", ui->app_tab_widget->currentIndex());
  // Save the real app ID so it can be restored correctly regardless of sort order.
  settings.setValue("current_app", currentApp());
  settings.setValue("ask_remove_from_deployer", ask_remove_from_deployer_);
  settings.setValue("ask_remove_mod", ask_remove_mod_);
  settings.setValue("deployer_list_slider_pos",
                    ui->deployer_list->verticalScrollBar()->sliderPosition());
  settings.setValue("mod_list_slider_pos", ui->mod_list->verticalScrollBar()->sliderPosition());
  settings.setValue("ask_remove_profile", ask_remove_profile_);
  settings.setValue("ask_remove_backup_target", ask_remove_backup_target_);
  settings.setValue("ask_remove_tool", ask_remove_tool_);
  settings.setValue("mod_list_sort_column", ui->mod_list->header()->sortIndicatorSection());
  settings.setValue("mod_list_sort_order", ui->mod_list->header()->sortIndicatorOrder());
  settings.setValue("sort_apps_alphabetically", sort_apps_alphabetically_);
  // Persist column widths and sort indicator for both lists (fork #142 / Vortex#23247).
  settings.setValue("mod_list_header_state", ui->mod_list->header()->saveState());
  settings.setValue("deployer_list_header_state", ui->deployer_list->header()->saveState());
  ipc_server_->shutdown();
  event->accept();
}

// fork #25: build the first-run / no-application empty-state overlay.
void MainWindow::setupEmptyStateOverlay()
{
  empty_state_overlay_ = new QFrame(centralWidget());
  empty_state_overlay_->setObjectName("empty_state_overlay");
  empty_state_overlay_->setFrameShape(QFrame::NoFrame);
  // Semi-opaque backdrop so the overlay reads as a distinct empty state.
  empty_state_overlay_->setAutoFillBackground(true);
  empty_state_overlay_->setStyleSheet(
    "#empty_state_overlay { background-color: palette(window); }");

  auto* layout = new QVBoxLayout(empty_state_overlay_);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(16);

  auto* title = new QLabel(tr("No applications yet"), empty_state_overlay_);
  QFont title_font = title->font();
  title_font.setPointSizeF(title_font.pointSizeF() * 1.6);
  title_font.setBold(true);
  title->setFont(title_font);
  title->setAlignment(Qt::AlignCenter);

  auto* hint = new QLabel(
    tr("Click \"+ Add application\" or import an existing setup to get started."),
    empty_state_overlay_);
  hint->setAlignment(Qt::AlignCenter);
  hint->setWordWrap(true);

  empty_state_button_ = new QPushButton(tr("Add application"), empty_state_overlay_);
  empty_state_button_->setIcon(QIcon::fromTheme("list-add"));
  empty_state_button_->setCursor(Qt::PointingHandCursor);
  empty_state_button_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  // Reuse the existing add-application action (app_tool_button's CTA target).
  connect(
    empty_state_button_, &QPushButton::clicked, this, &MainWindow::onAddAppButtonClicked);

  layout->addStretch();
  layout->addWidget(title, 0, Qt::AlignCenter);
  layout->addWidget(hint, 0, Qt::AlignCenter);
  layout->addWidget(empty_state_button_, 0, Qt::AlignCenter);
  layout->addStretch();

  empty_state_overlay_->hide();
  empty_state_overlay_->raise();
  updateEmptyStateOverlayGeometry();
}

// fork #25: keep the overlay covering the whole central area.
void MainWindow::updateEmptyStateOverlayGeometry()
{
  if(empty_state_overlay_ && centralWidget())
    empty_state_overlay_->setGeometry(centralWidget()->rect());
}

// fork #25
void MainWindow::resizeEvent(QResizeEvent* event)
{
  QMainWindow::resizeEvent(event);
  updateEmptyStateOverlayGeometry();
}

// fork #16: returns the local archive files among the dropped URLs (by extension allowlist).
static QList<QUrl> archiveUrlsFromMime(const QMimeData* mime)
{
  QList<QUrl> archives;
  if(!mime || !mime->hasUrls())
    return archives;
  static const QStringList kExtensions{ ".zip", ".7z",  ".rar", ".tar", ".gz",
                                        ".bz2", ".xz",  ".tgz", ".tbz", ".tbz2",
                                        ".lzma", ".zst", ".archive", ".fomod" };
  for(const QUrl& url : mime->urls())
  {
    if(!url.isLocalFile())
      continue;
    const QString lower = url.toLocalFile().toLower();
    for(const QString& ext : kExtensions)
    {
      if(lower.endsWith(ext))
      {
        archives.append(url);
        break;
      }
    }
  }
  return archives;
}

// fork #16: accept drags that contain at least one local archive file, but only when an
// application is selected so the import target is well defined.
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  if(currentApp() >= 0 && !archiveUrlsFromMime(event->mimeData()).isEmpty())
    event->acceptProposedAction();
  else
    QMainWindow::dragEnterEvent(event);
}

// fork #16: install the dropped archive(s) via the normal local-import path.
void MainWindow::dropEvent(QDropEvent* event)
{
  const QList<QUrl> archives = archiveUrlsFromMime(event->mimeData());
  if(currentApp() < 0 || archives.isEmpty())
  {
    QMainWindow::dropEvent(event);
    return;
  }
  event->acceptProposedAction();
  onModAdded(archives);
}

void MainWindow::setCmdArgument(std::string argument)
{
  if(argument.starts_with('\"'))
    argument.erase(0, 1);
  if(argument.ends_with('\"'))
    argument.erase(argument.size() - 1, 1);

  if(nexus::Api::nxmUrlIsValid(argument))
  {
    Log::debug("Received download request for \"" + argument + "\".");
    ImportModInfo info;
    info.app_id = currentApp();
    info.action_type = ImportModInfo::download;
    info.remote_request_url = argument;
    mod_import_queue_.push(info);
  }
}

void MainWindow::setDebugMode(bool enabled)
{
  debug_mode_ = enabled;
  if(enabled)
    Log::log_level = Log::LOG_DEBUG;
  Log::debug(std::format("Debug mode {}", enabled ? "enabled" : "disabled"));
}

void MainWindow::initChangelog()
{
  changelog_dialog_ = std::make_unique<ChangelogDialog>(is_a_flatpak_, this);
  if(changelog_dialog_->hasChanges() && APP_VERSION != previous_app_version_)
  {
    changelog_dialog_->show();
    changelog_dialog_->activateWindow();
    changelog_dialog_->raise();
  }
}

// clang-format off
void MainWindow::setupConnections()
{
  qRegisterMetaType<std::vector<ModInfo>>();
  qRegisterMetaType<std::filesystem::path>();
  qRegisterMetaType<std::string>();
  qRegisterMetaType<DeployerInfo>();
  qRegisterMetaType<std::vector<ConflictInfo>>();
  qRegisterMetaType<AppInfo>();
  qRegisterMetaType<std::unordered_set<int>>();
  qRegisterMetaType<QList<QList<QString>>>();
  qRegisterMetaType<EditApplicationInfo>();
  qRegisterMetaType<EditDeployerInfo>();
  qRegisterMetaType<std::vector<bool>>();
  qRegisterMetaType<Log::LogLevel>();
  qRegisterMetaType<std::vector<int>>();
  qRegisterMetaType<std::vector<BackupTarget>>();
  qRegisterMetaType<std::vector<EditManualTagAction>>();
  qRegisterMetaType<std::vector<EditAutoTagAction>>();
  qRegisterMetaType<EditProfileInfo>();
  qRegisterMetaType<nexus::Page>();
  qRegisterMetaType<ExternalChangesInfo>();
  qRegisterMetaType<FileChangeChoices>();
  qRegisterMetaType<Tool>();
  qRegisterMetaType<ImportModInfo>();
  qRegisterMetaType<std::vector<PrunableArchive>>(); // fork #145
  qRegisterMetaType<std::vector<std::filesystem::path>>(); // fork #145/#208
  qRegisterMetaType<std::vector<ModRule>>();
  qRegisterMetaType<std::vector<std::string>>();
  qRegisterMetaType<std::vector<std::vector<int>>>();

  connect(this, &MainWindow::getModInfo,
          app_manager_, &ApplicationManager::getModInfo);
  connect(app_manager_, &ApplicationManager::sendModInfo,
          this, &MainWindow::onGetModInfo);
  connect(this, &MainWindow::getDeployerInfo,
          app_manager_, &ApplicationManager::getDeployerInfo);
  connect(app_manager_, &ApplicationManager::sendDeployerInfo,
          this, &MainWindow::onGetDeployerInfo);
  connect(this, &MainWindow::installMod,
          app_manager_, &ApplicationManager::installMod);
  connect(this, &MainWindow::updateModDeployers,
          app_manager_, &ApplicationManager::updateModDeployers);
  connect(this, &MainWindow::uninstallMods,
          app_manager_, &ApplicationManager::uninstallMods);
  connect(this, &MainWindow::setModStatus,
          app_manager_, &ApplicationManager::setModStatus);
  connect(this, &MainWindow::commitChanges,
          app_manager_, &ApplicationManager::commitChanges);
  connect(this, &MainWindow::deployMods,
          app_manager_, &ApplicationManager::deployMods);
  connect(this, &MainWindow::forceRedeployMods, // fork #208
          app_manager_, &ApplicationManager::forceRedeployMods);
  connect(this, &MainWindow::requestPrunableArchives, // fork #145
          app_manager_, &ApplicationManager::requestPrunableArchives);
  connect(this, &MainWindow::pruneArchives, // fork #145
          app_manager_, &ApplicationManager::pruneArchives);
  connect(app_manager_, &ApplicationManager::sendPrunableArchives, // fork #145
          this, &MainWindow::onPrunableArchives);
  connect(this, &MainWindow::addApplication,
          app_manager_, &ApplicationManager::addApplication);
  connect(this, &MainWindow::addDeployer,
          app_manager_, &ApplicationManager::addDeployer);
  connect(this, &MainWindow::getApplicationNames,
          app_manager_, &ApplicationManager::getApplicationNames);
  connect(app_manager_, &ApplicationManager::sendApplicationNames,
          this, &MainWindow::onGetApplicationNames);
  connect(this, &MainWindow::getDeployerNames,
          app_manager_, &ApplicationManager::getDeployerNames);
  connect(app_manager_, &ApplicationManager::sendDeployerNames,
          this, &MainWindow::onGetDeployerNames);
  connect(this, &MainWindow::removeDeployer,
          app_manager_, &ApplicationManager::removeDeployer);
  connect(this, &MainWindow::removeApplication,
          app_manager_, &ApplicationManager::removeApplication);
  connect(this, &MainWindow::removeModFromDeployer,
          app_manager_, &ApplicationManager::removeNodeFromDeployer);
  connect(app_manager_, &ApplicationManager::completedOperations,
          this, &MainWindow::onCompletedOperations);
  connect(this, &MainWindow::changeModName,
          app_manager_, &ApplicationManager::changeModName);
  connect(this, &MainWindow::getFileConflicts,
          app_manager_, &ApplicationManager::getFileConflicts);
  connect(app_manager_, &ApplicationManager::sendFileConflicts,
          this, &MainWindow::onGetFileConflicts);
  connect(this, &MainWindow::getAppInfo,
          app_manager_, &ApplicationManager::getAppInfo);
  connect(app_manager_, &ApplicationManager::sendAppInfo,
          this, &MainWindow::onGetAppInfo);
  connect(this, &MainWindow::addTool,
          app_manager_, &ApplicationManager::addTool);
  connect(this, &MainWindow::removeTool,
          app_manager_, &ApplicationManager::removeTool);
  connect(this, &MainWindow::editApplication,
          app_manager_, &ApplicationManager::editApplication);
  connect(this, &MainWindow::editDeployer,
          app_manager_, &ApplicationManager::editDeployer);
  connect(this, &MainWindow::getModConflicts,
          app_manager_, &ApplicationManager::getModConflicts);
  connect(app_manager_, &ApplicationManager::sendModConflicts,
          this, &MainWindow::onGetModConflicts);
  connect(this, &MainWindow::setProfile,
          app_manager_, &ApplicationManager::setProfile);
  connect(this, &MainWindow::addProfile,
          app_manager_, &ApplicationManager::addProfile);
  connect(this, &MainWindow::removeProfile,
          app_manager_, &ApplicationManager::removeProfile);
  connect(this, &MainWindow::getProfileNames,
          app_manager_, &ApplicationManager::getProfileNames);
  connect(app_manager_, &ApplicationManager::sendProfileNames,
          this, &MainWindow::onGetProfileNames);
  connect(this, &MainWindow::editProfile,
          app_manager_, &ApplicationManager::editProfile);
  connect(app_manager_, &ApplicationManager::sendError,
          this, &MainWindow::onReceiveError);
  connect(this, &MainWindow::editTool,
          app_manager_, &ApplicationManager::editTool);
  connect(this, &MainWindow::addModToGroup,
          app_manager_, &ApplicationManager::addModToGroup);
  connect(this, &MainWindow::removeModFromGroup,
          app_manager_, &ApplicationManager::removeModFromGroup);
  connect(this, &MainWindow::createGroup,
          app_manager_, &ApplicationManager::createGroup);
  connect(this, &MainWindow::changeActiveGroupMember,
          app_manager_, &ApplicationManager::changeActiveGroupMember);
  connect(this, &MainWindow::changeModVersion,
          app_manager_, &ApplicationManager::changeModVersion);
  connect(this, &MainWindow::sortModsByConflicts,
          app_manager_, &ApplicationManager::sortModsByConflicts);
  connect(this, &MainWindow::setModNote,
          app_manager_, &ApplicationManager::setModNote);
  connect(this, &MainWindow::setModColor, // fork #199
          app_manager_, &ApplicationManager::setModColor);
  connect(this, &MainWindow::setModPinned,
          app_manager_, &ApplicationManager::setModPinned);
  connect(this, &MainWindow::getModRulesFor,
          app_manager_, &ApplicationManager::getModRulesFor);
  connect(app_manager_, &ApplicationManager::sendModRules,
          this, &MainWindow::onGetModRules);
  connect(this, &MainWindow::setModRulesFor,
          app_manager_, &ApplicationManager::setModRulesFor);
  connect(this, &MainWindow::getGroupData,
          app_manager_, &ApplicationManager::getGroupData);
  connect(app_manager_, &ApplicationManager::sendGroupData,
          this, &MainWindow::onGetGroupData);
  connect(this, &MainWindow::setGroupName,
          app_manager_, &ApplicationManager::setGroupName);
  connect(this, &MainWindow::setGroupNotes,
          app_manager_, &ApplicationManager::setGroupNotes);
  connect(this, &MainWindow::dissolveGroup,
          app_manager_, &ApplicationManager::dissolveGroup);
  connect(this, &MainWindow::mergeTw3Scripts,
          app_manager_, &ApplicationManager::mergeTw3Scripts);
  connect(this, &MainWindow::mergeTw3Config,
          app_manager_, &ApplicationManager::mergeTw3Config);
  connect(this, &MainWindow::getCyberpunkSetupInfo,
          app_manager_, &ApplicationManager::getCyberpunkSetupInfo);
  connect(this, &MainWindow::deployRedMods,
          app_manager_, &ApplicationManager::deployRedMods);
  connect(app_manager_, &ApplicationManager::sendGameToolResult,
          this, &MainWindow::onGameToolResult);
  connect(app_manager_, &ApplicationManager::sendRunCommand,
          this, &MainWindow::onRunGameCommand);
  connect(ui->deployer_list, &DeployerListView::modMoved,
          this, &MainWindow::onModMoved);
  connect(this, &MainWindow::extractArchive,
          app_manager_, &ApplicationManager::extractArchive);
  connect(app_manager_, &ApplicationManager::extractionComplete,
          this, &MainWindow::onExtractionComplete);
  connect(app_manager_, &ApplicationManager::logMessage,
          this, &MainWindow::onReceiveLogMessage);
  connect(ui->deployer_list, &ModListView::modAdded,
          this, &MainWindow::onModAdded);
  connect(ui->deployer_list, &ModListView::modStatusChanged,
          this, &MainWindow::onDeployerBoxChange);
  connect(version_deledate_, &VersionBoxDelegate::modVersionChanged,
          this, &MainWindow::onModVersionEdited);
  connect(version_deledate_, &VersionBoxDelegate::activeGroupMemberChanged,
          this, &MainWindow::onActiveGroupMemberChanged);
  connect(mod_name_delegate_, &ModNameDelegate::modNameChanged,
          this, &MainWindow::onModNameChanged);
  connect(ui->mod_list, &ModListView::modRemoved,
          this, &MainWindow::onModRemoved);
  connect(ui->mod_list, &ModListView::modAdded,
          this, &MainWindow::onModAdded);
  connect(this, &MainWindow::deployModsFor,
          app_manager_, &ApplicationManager::deployModsFor);
  connect(this, &MainWindow::getBackupInfo,
          app_manager_, &ApplicationManager::getBackupTargets);
  connect(app_manager_, &ApplicationManager::sendBackupTargets,
          this, &MainWindow::onGetBackupInfo);
  connect(this, &MainWindow::addBackupTarget,
          app_manager_, &ApplicationManager::addBackupTarget);
  connect(this, &MainWindow::removeBackupTarget,
          app_manager_, &ApplicationManager::removeBackupTarget);
  connect(this, &MainWindow::addBackup,
          app_manager_, &ApplicationManager::addBackup);
  connect(this, &MainWindow::removeBackup,
          app_manager_, &ApplicationManager::removeBackup);
  connect(this, &MainWindow::setActiveBackup,
          app_manager_, &ApplicationManager::setActiveBackup);
  connect(this, &MainWindow::setBackupName,
          app_manager_, &ApplicationManager::setBackupName);
  connect(backup_delegate_, &VersionBoxDelegate::backupNameEdited,
          this, &MainWindow::onBackupNameEdited);
  connect(backup_delegate_, &VersionBoxDelegate::activeBackupChanged,
          this, &MainWindow::onActiveBackupChanged);
  connect(ui->backup_list, &BackupListView::addBackupTargetClicked,
          this, &MainWindow::onAddBackupTargetClicked);
  connect(ui->backup_list, &BackupListView::backupTargetRemoved,
          this, &MainWindow::onBackupTargetRemoveClicked);
  connect(this, &MainWindow::setBackupTargetName,
          app_manager_, &ApplicationManager::setBackupTargetName);
  connect(backup_target_name_delegate_, &BackupNameDelegate::backupTargetNameChanged,
          this, &MainWindow::onBackupTargetNameEdited);
  connect(this, &MainWindow::overwriteBackup,
          app_manager_, &ApplicationManager::overwriteBackup);
  connect(this, &MainWindow::scrollLists,
          app_manager_, &ApplicationManager::onScrollLists);
  connect(app_manager_, &ApplicationManager::scrollLists,
          this, &MainWindow::onScrollLists);
  connect(app_manager_, &ApplicationManager::updateProgress,
          this, &MainWindow::updateProgress);
  connect(this, &MainWindow::uninstallGroupMembers,
          app_manager_, &ApplicationManager::uninstallGroupMembers);
  connect(add_app_dialog_.get(), &AddModDialog::finished,
          this, &MainWindow::onAddAppDialogFinished);
  connect(add_deployer_dialog_.get(), &AddDeployerDialog::finished,
          this, &MainWindow::onAddDeployerDialogFinished);
  connect(add_backup_target_dialog_.get(), &AddBackupTargetDialog::finished,
          this, &MainWindow::onAddBackupTargetDialogFinished);
  connect(add_backup_dialog_.get(), &AddBackupDialog::finished,
          this, &MainWindow::onAddBackupTargetDialogFinished);
  connect(add_to_group_dialog_.get(), &AddToGroupDialog::finished,
          this, &MainWindow::onBusyDialogAborted);
  connect(add_to_deployer_dialog_.get(), &AddToDeployerDialog::rejected,
          this, &MainWindow::onBusyDialogAborted);
  connect(add_profile_dialog_.get(), &AddProfileDialog::finished,
          this, &MainWindow::onAddProfileDialogFinished);
  connect(this, &MainWindow::editManualTags,
          app_manager_, &ApplicationManager::editManualTags);
  connect(this, &MainWindow::setTagsForMods,
          app_manager_, &ApplicationManager::setTagsForMods);
  connect(this, &MainWindow::addTagsToMods,
          app_manager_, &ApplicationManager::addTagsToMods);
  connect(this, &MainWindow::removeTagsFromMods,
          app_manager_, &ApplicationManager::removeTagsFromMods);
  connect(this, &MainWindow::editAutoTags,
          app_manager_, &ApplicationManager::editAutoTags);
  connect(this, &MainWindow::reapplyAutoTags,
          app_manager_, &ApplicationManager::reapplyAutoTags);
  connect(this, &MainWindow::updateAutoTags,
          app_manager_, &ApplicationManager::updateAutoTags);
  connect(this, &MainWindow::editModSources,
          app_manager_, &ApplicationManager::editModSources);
  connect(this, &MainWindow::getNexusPage,
          app_manager_, &ApplicationManager::getNexusPage);
  connect(app_manager_, &ApplicationManager::sendNexusPage,
          this, &MainWindow::onGetNexusPage);
  connect(this, &MainWindow::downloadMod,
          app_manager_, &ApplicationManager::downloadMod);
  connect(app_manager_, &ApplicationManager::downloadComplete,
          this, &MainWindow::onDownloadComplete);
  connect(app_manager_, &ApplicationManager::downloadFailed,
          this, &MainWindow::onDownloadFailed);
  connect(this, &MainWindow::checkForModUpdates,
          app_manager_, &ApplicationManager::checkForModUpdates);
  connect(this, &MainWindow::checkModsForUpdates,
          app_manager_, &ApplicationManager::checkModsForUpdates);
  connect(this, &MainWindow::suppressUpdateNotification,
          app_manager_, &ApplicationManager::suppressUpdateNotification);
  connect(app_manager_, &ApplicationManager::modInstallationComplete,
          this, &MainWindow::onModInstallationComplete);
  connect(this, &MainWindow::getExternalChanges,
          app_manager_, &ApplicationManager::getExternalChanges);
  connect(this, &MainWindow::keepOrRevertFileModifications,
          app_manager_, &ApplicationManager::keepOrRevertFileModifications);
  connect(app_manager_, &ApplicationManager::sendExternalChangesInfo,
          this, &MainWindow::onGetExternalChangesInfo);
  connect(app_manager_, &ApplicationManager::externalChangesHandled,
          this, &MainWindow::onExternalChangesHandled);
  connect(this, &MainWindow::exportAppConfiguration,
          app_manager_, &ApplicationManager::exportAppConfiguration);
  connect(this, &MainWindow::unDeployMods,
          app_manager_, &ApplicationManager::unDeployMods);
  connect(this, &MainWindow::unDeployModsFor,
          app_manager_, &ApplicationManager::unDeployModsFor);
  connect(add_deployer_dialog_.get(), &AddDeployerDialog::updateIgnoredFiles,
          this, &MainWindow::onUpdateIgnoredFiles);
  connect(this, &MainWindow::updateIgnoredFiles,
          app_manager_, &ApplicationManager::updateIgnoredFiles);
  connect(this, &MainWindow::addModToIgnoreList,
          app_manager_, &ApplicationManager::addModToIgnoreList);
  connect(this, &MainWindow::applyModAction,
          app_manager_, &ApplicationManager::applyModAction);
}
// clang-format on

void MainWindow::setupLists()
{
  // mod list
  this->setAcceptDrops(true);
  ui->mod_list->setStyleSheet("QTableView{margin-top:6}");
  ui->mod_list->setAcceptDrops(true);
  ui->mod_list->setDropIndicatorShown(true);
  mod_list_proxy_ = new ModListProxyModel(ui->mod_list_row_count_label, this);
  version_deledate_ = new VersionBoxDelegate(mod_list_proxy_, ui->mod_list);
  mod_name_delegate_ = new ModNameDelegate(mod_list_proxy_, ui->mod_list);
  mod_list_cell_delegate_ = new TableCellDelegate(mod_list_proxy_, ui->mod_list);
  mod_list_model_ = new ModListModel(mod_list_proxy_, ui->mod_list);
  ui->mod_list->setItemDelegateForColumn(ModListModel::version_col, version_deledate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::name_col, mod_name_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::deployers_col, mod_list_cell_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::id_col, mod_list_cell_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::time_col, mod_list_cell_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::size_col, mod_list_cell_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::action_col, mod_list_cell_delegate_);
  ui->mod_list->setItemDelegateForColumn(ModListModel::tags_col, mod_list_cell_delegate_);
  mod_list_proxy_->setSourceModel(mod_list_model_);
  ui->mod_list->setModel(mod_list_proxy_);
  // Highlight mods that conflict with the currently selected one (limo-app/limo#143).
  connect(ui->mod_list->selectionModel(),
          &QItemSelectionModel::currentRowChanged,
          this,
          [this](const QModelIndex& current, const QModelIndex&)
          {
            if(!current.isValid())
            {
              mod_list_model_->clearConflictHighlight();
              return;
            }
            const auto src = mod_list_proxy_->mapToSource(current);
            const int mod_id = mod_list_model_->data(src, ModListModel::mod_id_role).toInt();
            const auto iter = mod_conflict_groups_.find(mod_id);
            if(iter == mod_conflict_groups_.end())
              mod_list_model_->setConflictHighlight(mod_id, {});
            else
              mod_list_model_->setConflictHighlight(mod_id, iter->second);
          });
  ui->mod_list->setColumnWidth(ModListModel::action_col, 55);
  ui->mod_list->setColumnWidth(ModListModel::id_col, 50);
  mod_list_proxy_->setFilterKeyColumn(ModListModel::name_col);
  mod_list_proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  mod_list_proxy_->setFilterRole(Qt::DisplayRole);
  mod_list_proxy_->setSortRole(ModListModel::sort_role);
  mod_list_proxy_->setSortCaseSensitivity(Qt::CaseInsensitive);
  ui->mod_list->sortByColumn(ModListModel::time_col, Qt::SortOrder::DescendingOrder);
  // Make all mod list columns interactively resizable (fork #142 / Vortex#23247).
  // The name column keeps its natural width from resizeColumnToContents; the user
  // can drag any header border to override it.  Column widths are persisted via
  // QHeaderView::saveState / restoreState in closeEvent / loadSettings.
  ui->mod_list->header()->setSectionResizeMode(QHeaderView::Interactive);

  // deployer list
  deployer_model_ = new DeployerListModel(this);
  deployer_list_proxy_ = new DeployerListProxyModel(ui->deployer_list_row_count_label, this);
  deployer_list_cell_delegate_ = new TableCellDelegate(deployer_list_proxy_, ui->deployer_list);
  ui->deployer_list->setItemDelegateForColumn(DeployerListModel::name_col,
                                              deployer_list_cell_delegate_);
  ui->deployer_list->setItemDelegateForColumn(DeployerListModel::id_col,
                                              deployer_list_cell_delegate_);
  ui->deployer_list->setItemDelegateForColumn(DeployerListModel::tags_col,
                                              deployer_list_cell_delegate_);
  deployer_list_proxy_->setSourceModel(deployer_model_);
  ui->deployer_list->setModel(deployer_list_proxy_);
  deployer_list_proxy_->setFilterKeyColumn(DeployerListModel::name_col);
  deployer_list_proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  deployer_list_proxy_->setFilterRole(Qt::DisplayRole);
  ui->deployer_list->setAcceptDrops(true);
  ui->deployer_list->setDragEnabled(true);
  ui->deployer_list->setDropIndicatorShown(true);
  ui->deployer_list->setEnableDragReorder(true);
  // Make deployer list columns interactively resizable (fork #142 / Vortex#23247).
  ui->deployer_list->header()->setSectionResizeMode(QHeaderView::Interactive);

  // backup list
  ui->backup_list->setStyleSheet("QTableView{margin-top:6}");
  backup_list_cell_delegate_ = new TableCellDelegate(nullptr, ui->backup_list);
  backup_list_model_ = new BackupListModel(ui->backup_list);
  ui->backup_list->setItemDelegateForColumn(BackupListModel::target_col,
                                            backup_list_cell_delegate_);
  ui->backup_list->setItemDelegateForColumn(BackupListModel::path_col, backup_list_cell_delegate_);
  ui->backup_list->setItemDelegateForColumn(BackupListModel::action_col,
                                            backup_list_cell_delegate_);
  ui->backup_list->setModel(backup_list_model_);
  backup_delegate_ = new VersionBoxDelegate(nullptr, ui->backup_list);
  backup_delegate_->setIsBackupDelegate(true);
  ui->backup_list->setItemDelegateForColumn(BackupListModel::backup_col, backup_delegate_);
  ui->backup_list->setAcceptDrops(false);
  backup_target_name_delegate_ = new BackupNameDelegate(nullptr, ui->backup_list);
  ui->backup_list->setItemDelegateForColumn(BackupListModel::target_col,
                                            backup_target_name_delegate_);

  // conflicts list
  conflicts_model_ = new ConflictsModel(this);
  conflicts_window_ = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout();
  conflicts_window_->setLayout(layout);
  conflicts_list_ = new QTableView(conflicts_window_);
  layout->addWidget(conflicts_list_);
  conflicts_list_->setModel(conflicts_model_);
  conflicts_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  conflicts_list_->setSelectionMode(QAbstractItemView::NoSelection);
  conflicts_list_->setSelectionBehavior(QAbstractItemView::SelectRows);
  conflicts_list_->horizontalHeader()->setStretchLastSection(true);
  conflicts_list_->setAlternatingRowColors(true);
  conflicts_list_->verticalHeader()->setVisible(false);
  conflicts_window_->resize(1200, 600);

  // tools list
  ui->info_tool_list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  ui->info_tool_list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  // ui->info_tool_list->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  ui->info_tool_list->setColumnWidth(0, 50);
  ui->info_tool_list->setColumnWidth(1, 50);
}

void MainWindow::setupMenus()
{
  auto sort_actions = [](QAction* a, QAction* b) { return a->text() < b->text(); };

  ui->mod_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->mod_list,
          &ModListView::customContextMenuRequested,
          this,
          &MainWindow::onModListContextMenu);
  mod_list_menu_ = new QMenu(this);
  edit_note_action_ = new QAction("Edit Note...", this);
  edit_note_action_->setToolTip("Attach a free-text note to this mod");
  connect(edit_note_action_, &QAction::triggered, this, &MainWindow::onEditModNote);
  pin_version_action_ = new QAction("Pin Version", this);
  pin_version_action_->setToolTip("Keep this mod at its current version and skip it in update checks");
  connect(pin_version_action_, &QAction::triggered, this, &MainWindow::onPinModVersion);
  unpin_version_action_ = new QAction("Unpin Version", this);
  unpin_version_action_->setToolTip("Allow this mod to be updated again");
  connect(unpin_version_action_, &QAction::triggered, this, &MainWindow::onUnpinModVersion);
  mod_rules_action_ = new QAction("Mod Rules...", this);
  mod_rules_action_->setToolTip("Edit dependency and conflict rules for this mod");
  connect(mod_rules_action_, &QAction::triggered, this, &MainWindow::onEditModRules);
  manage_groups_action_ = new QAction("Manage Groups...", this);
  manage_groups_action_->setToolTip("View and edit all version groups for this app");
  connect(manage_groups_action_, &QAction::triggered, this, &MainWindow::onManageGroups);
  set_color_action_ = new QAction("Set Colour...", this); // fork #199
  set_color_action_->setToolTip("Assign a highlight colour to the selected mod(s)");
  connect(set_color_action_, &QAction::triggered, this, &MainWindow::onSetModColor);
  clear_color_action_ = new QAction("Clear Colour", this); // fork #199
  clear_color_action_->setToolTip("Remove the highlight colour from the selected mod(s)");
  connect(clear_color_action_, &QAction::triggered, this, &MainWindow::onClearModColor);
  edit_config_action_ = new QAction("Edit Config...", this); // fork #200
  edit_config_action_->setToolTip("Edit configuration files shipped by this mod");
  connect(edit_config_action_, &QAction::triggered, this, &MainWindow::onEditModConfig);
  QList<QAction*> mod_list_actions{ ui->actionadd_to_deployer,      ui->actionAdd_to_Group,
                                    ui->actionbrowse_mod_files,     ui->actionRemove_from_Group,
                                    ui->actionRemove_Mods,          ui->actionRemove_Other_Versions,
                                    ui->actionEdit_Tags_for_mods,   ui->actionUpdate_Tags,
                                    ui->actionEdit_Mod_Sources,     ui->actionShow_Nexus_Page,
                                    ui->actionReinstall_From_Local, ui->actionCheck_For_Updates,
                                    ui->actionSuppress_Update,       edit_note_action_,
                                    pin_version_action_,            unpin_version_action_,
                                    mod_rules_action_,               manage_groups_action_,
                                    set_color_action_,               clear_color_action_,
                                    edit_config_action_ };
  std::sort(mod_list_actions.begin(), mod_list_actions.end(), sort_actions);
  mod_list_menu_->addActions(mod_list_actions);

  bulk_enable_action_ = new QAction("Enable selected mods", this);
  bulk_enable_action_->setIcon(QIcon::fromTheme("checkbox"));
  connect(bulk_enable_action_, &QAction::triggered, this, &MainWindow::on_actionBulk_Enable_triggered);
  bulk_disable_action_ = new QAction("Disable selected mods", this);
  connect(
    bulk_disable_action_, &QAction::triggered, this, &MainWindow::on_actionBulk_Disable_triggered);
  bulk_add_tag_action_ = new QAction("Add tag to selected mods", this);
  bulk_add_tag_action_->setIcon(QIcon::fromTheme("tag"));
  connect(
    bulk_add_tag_action_, &QAction::triggered, this, &MainWindow::on_actionBulk_Add_Tag_triggered);
  bulk_remove_tag_action_ = new QAction("Remove tag from selected mods", this);
  connect(bulk_remove_tag_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_actionBulk_Remove_Tag_triggered);
  QList<QAction*> bulk_actions{
    bulk_enable_action_, bulk_disable_action_, bulk_add_tag_action_, bulk_remove_tag_action_
  };
  mod_list_menu_->addSeparator();
  mod_list_menu_->addActions(bulk_actions);

  ui->deployer_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->deployer_list,
          &ModListView::customContextMenuRequested,
          this,
          &MainWindow::onDeployerListContextMenu);
  deployer_list_menu_ = new QMenu(this);
  conflict_detail_action_ = new QAction("Conflict Details...", this);
  conflict_detail_action_->setToolTip("Show which files this mod wins and loses against other mods");
  connect(conflict_detail_action_, &QAction::triggered, this, &MainWindow::onConflictDetails);
  merge_tw3_scripts_action_ = new QAction("Merge Witcher 3 Scripts (experimental)...", this);
  merge_tw3_scripts_action_->setToolTip(
    "Merge conflicting WitcherScript (.ws) files across the enabled mods of this deployer");
  connect(merge_tw3_scripts_action_, &QAction::triggered, this, &MainWindow::onMergeTw3Scripts);
  merge_tw3_config_action_ = new QAction("Merge Witcher 3 Config (experimental)...", this);
  merge_tw3_config_action_->setToolTip(
    "Merge each enabled mod's input.xml fragment into the game's shared input.xml");
  connect(merge_tw3_config_action_, &QAction::triggered, this, &MainWindow::onMergeTw3Config);
  cyberpunk_setup_action_ = new QAction("Cyberpunk 2077 Setup...", this);
  cyberpunk_setup_action_->setToolTip(
    "Show the Cyberpunk 2077 Proton setup checklist and a deploy-mode safety check");
  connect(cyberpunk_setup_action_, &QAction::triggered, this, &MainWindow::onCyberpunkSetup);
  deploy_redmods_action_ = new QAction("Deploy REDmods (experimental)...", this);
  deploy_redmods_action_->setToolTip(
    "Lay out enabled REDmods and run redMod.exe deploy under Proton");
  connect(deploy_redmods_action_, &QAction::triggered, this, &MainWindow::onDeployRedmods);
  QList<QAction*> deployer_list_actions{
    ui->actionremove_from_deployer, ui->actionget_file_conflicts,
    ui->actionget_mod_conflicts,    ui->actionmove_mod,
    ui->actionbrowse_mod_files,     ui->actionSort_Mods,
    ui->actionAdd_to_Ignore_List,   conflict_detail_action_,
    merge_tw3_scripts_action_,      merge_tw3_config_action_,
    cyberpunk_setup_action_,        deploy_redmods_action_,
    ui->actionExport_Mod_List
  };
  std::sort(deployer_list_actions.begin(), deployer_list_actions.end(), sort_actions);
  deployer_list_menu_->addActions(deployer_list_actions);

  ui->backup_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->backup_list,
          &ModListView::customContextMenuRequested,
          this,
          &MainWindow::onBackupListContextMenu);
  backup_list_menu_ = new QMenu(this);
  QList<QAction*> backup_list_actions{ ui->actionAdd_Backup,
                                       ui->actionRemove_Backup,
                                       ui->actionBrowse_backup_files,
                                       ui->actionOverwrite_Backup };
  std::sort(backup_list_actions.begin(), backup_list_actions.end(), sort_actions);
  backup_list_menu_->addActions(backup_list_actions);

  ui->actionEdit_Tags_for_mods->setIcon(QIcon::fromTheme("tag"));

  // fork #114: add a "Mod Repositories" entry to the (otherwise empty) menu bar
  // that opens the OMM network repository dialog.
  QMenu* tools_menu = menuBar()->addMenu(tr("Tools"));
  QAction* repositories_action = tools_menu->addAction(tr("Mod Repositories"));
  connect(
    repositories_action, &QAction::triggered, this, &MainWindow::onOpenRepositoriesDialog);
  // fork #203: instance dashboard / overview.
  QAction* dashboard_action = tools_menu->addAction(tr("Instance Dashboard"));
  connect(dashboard_action, &QAction::triggered, this, &MainWindow::onShowInstanceDashboard);
  // fork #208: force a clean purge + redeploy of all deployers.
  QAction* redeploy_action = tools_menu->addAction(tr("Force Redeploy (purge && rebuild)"));
  connect(redeploy_action, &QAction::triggered, this, &MainWindow::onForceRedeploy);
  // fork #145: bulk-remove outdated downloaded archive versions.
  QAction* prune_action = tools_menu->addAction(tr("Remove Old Archive Versions"));
  connect(prune_action, &QAction::triggered, this, &MainWindow::onPruneArchives);
  // fork #1/#2: Add a "Collections" menu with Import/Export actions.
  QMenu* collections_menu = menuBar()->addMenu("Collections");
  QAction* import_collection_action = collections_menu->addAction("Import Collection");
  QAction* export_collection_action = collections_menu->addAction("Export Collection");
  connect(import_collection_action, &QAction::triggered, this, &MainWindow::onImportCollection);
  connect(export_collection_action, &QAction::triggered, this, &MainWindow::onExportCollection);
}

void MainWindow::setupDialogs()
{
  add_app_dialog_ = std::make_unique<AddAppDialog>(is_a_flatpak_);
  connect(add_app_dialog_.get(),
          &AddAppDialog::applicationEdited,
          this,
          &MainWindow::onApplicationEdited);
  connect(add_app_dialog_.get(),
          &AddAppDialog::applicationAdded,
          this,
          &MainWindow::onAddAppDialogComplete);

  add_deployer_dialog_ = std::make_unique<AddDeployerDialog>();
  connect(add_deployer_dialog_.get(),
          &AddDeployerDialog::deployerEdited,
          this,
          &MainWindow::onDeployerEdited);
  connect(add_deployer_dialog_.get(),
          &AddDeployerDialog::deployerAdded,
          this,
          &MainWindow::onAddDeployerDialogComplete);

  add_mod_dialog_ = std::make_unique<AddModDialog>(mod_list_model_, deployer_model_);
  connect(
    add_mod_dialog_.get(), &AddModDialog::addModAccepted, this, &MainWindow::onAddModDialogAccept);
  connect(add_mod_dialog_.get(), &AddModDialog::addModAborted, this, &MainWindow::onAddModAborted);

  add_profile_dialog_ = std::make_unique<AddProfileDialog>();
  connect(
    add_profile_dialog_.get(), &AddProfileDialog::profileAdded, this, &MainWindow::onProfileAdded);
  connect(add_profile_dialog_.get(),
          &AddProfileDialog::profileEdited,
          this,
          &MainWindow::onProfileEdited);

  add_to_deployer_dialog_ = std::make_unique<AddToDeployerDialog>();
  connect(add_to_deployer_dialog_.get(),
          &AddToDeployerDialog::modDeployersUpdated,
          this,
          &MainWindow::onAddToDeployerAccept);

  add_tool_dialog_ = std::make_unique<AddToolDialog>();
  connect(add_tool_dialog_.get(), &AddToolDialog::toolAdded, this, &MainWindow::onToolAdded);
  connect(add_tool_dialog_.get(), &AddToolDialog::toolEdited, this, &MainWindow::onToolEdited);

  message_box_ = std::make_unique<QMessageBox>(
    QMessageBox::NoIcon, "Confirm Removal", "", QMessageBox::Yes | QMessageBox::No);
  message_box_->setDefaultButton(QMessageBox::No);
  QCheckBox* check_box = new QCheckBox(message_box_.get());
  check_box->setHidden(true);
  message_box_->setCheckBox(check_box);

  add_to_group_dialog_ = std::make_unique<AddToGroupDialog>();
  connect(add_to_group_dialog_.get(),
          &AddToGroupDialog::modAddedToGroup,
          this,
          &MainWindow::onModAddedToGroup);
  settings_dialog_ = std::make_unique<SettingsDialog>();
  connect(settings_dialog_.get(),
          &SettingsDialog::settingsDialogAccepted,
          this,
          &MainWindow::onSettingsDialogComplete);

  add_backup_target_dialog_ = std::make_unique<AddBackupTargetDialog>();
  connect(add_backup_target_dialog_.get(),
          &AddBackupTargetDialog::backupTargetAdded,
          this,
          &MainWindow::onBackupTargetAdded);

  add_backup_dialog_ = std::make_unique<AddBackupDialog>();
  connect(add_backup_dialog_.get(),
          &AddBackupDialog::addBackupDialogAccepted,
          this,
          &MainWindow::onBackupAdded);

  overwrite_backup_dialog_ = std::make_unique<OverwriteBackupDialog>();
  connect(overwrite_backup_dialog_.get(),
          &OverwriteBackupDialog::backupOverwritten,
          this,
          &MainWindow::onBackupOverwritten);

  edit_manual_tags_dialog_ = std::make_unique<EditManualTagsDialog>();
  connect(edit_manual_tags_dialog_.get(),
          &EditManualTagsDialog::manualTagsEdited,
          this,
          &MainWindow::onManualTagsEdited);
  connect(edit_manual_tags_dialog_.get(),
          &EditManualTagsDialog::dialogClosed,
          this,
          &MainWindow::onBusyDialogAborted);

  manage_mod_tags_dialog_ = std::make_unique<ManageModTagsDialog>();
  connect(manage_mod_tags_dialog_.get(),
          &ManageModTagsDialog::modTagsUpdated,
          this,
          &MainWindow::onManualModTagsUpdated);
  connect(manage_mod_tags_dialog_.get(),
          &ManageModTagsDialog::dialogClosed,
          this,
          &MainWindow::onBusyDialogAborted);

  manage_mod_rules_dialog_ = std::make_unique<ManageModRulesDialog>();
  connect(manage_mod_rules_dialog_.get(),
          &ManageModRulesDialog::rulesChanged,
          this,
          &MainWindow::onModRulesChanged);

  manage_groups_dialog_ = std::make_unique<ManageGroupsDialog>();
  connect(manage_groups_dialog_.get(),
          &ManageGroupsDialog::groupRenamed,
          this,
          &MainWindow::onGroupRenamed);
  connect(manage_groups_dialog_.get(),
          &ManageGroupsDialog::groupNotesChanged,
          this,
          &MainWindow::onGroupNotesChanged);
  connect(manage_groups_dialog_.get(),
          &ManageGroupsDialog::groupDissolved,
          this,
          &MainWindow::onGroupDissolved);
  connect(manage_groups_dialog_.get(),
          &ManageGroupsDialog::activeGroupMemberChanged,
          this,
          [this](int app_id, int group, int mod_id)
          {
            emit changeActiveGroupMember(app_id, group, mod_id);
            if(app_id == currentApp())
              emit getModInfo(app_id);
          });

  edit_auto_tags_dialog_ = std::make_unique<EditAutoTagsDialog>();
  connect(edit_auto_tags_dialog_.get(),
          &EditAutoTagsDialog::tagsEdited,
          this,
          &MainWindow::onAutoTagsEdited);
  connect(edit_auto_tags_dialog_.get(),
          &EditAutoTagsDialog::dialogClosed,
          this,
          &MainWindow::onBusyDialogAborted);

  edit_mod_sources_dialog_ = std::make_unique<EditModSourcesDialog>();
  connect(edit_mod_sources_dialog_.get(),
          &EditModSourcesDialog::modSourcesEdited,
          this,
          &MainWindow::onModSourcesEdited);
  connect(edit_mod_sources_dialog_.get(),
          &EditModSourcesDialog::dialogClosed,
          this,
          &MainWindow::onBusyDialogAborted);

  nexus_mod_dialog_ = std::make_unique<NexusModDialog>();
  connect(nexus_mod_dialog_.get(),
          &NexusModDialog::modDownloadRequested,
          this,
          &MainWindow::onModDownloadRequested);

  // BEGIN feature #33: in-app NexusMods browsing/search.
  // Route the browser's install signal through the existing download/import slot.
  nexus_browser_dialog_ = std::make_unique<NexusBrowserDialog>();
  connect(nexus_browser_dialog_.get(),
          &NexusBrowserDialog::installModRequested,
          this,
          &MainWindow::onModDownloadRequested);
  // END feature #33.

  external_changes_dialog_ = std::make_unique<ExternalChangesDialog>();
  connect(external_changes_dialog_.get(),
          &ExternalChangesDialog::externalChangesDialogCompleted,
          this,
          &MainWindow::onExternalChangesDialogCompleted);
  connect(external_changes_dialog_.get(),
          &ExternalChangesDialog::externalChangesDialogAborted,
          this,
          &MainWindow::onExternalChangesDialogAborted);

  export_app_config_dialog_ = std::make_unique<ExportAppConfigDialog>();
  connect(export_app_config_dialog_.get(),
          &ExportAppConfigDialog::appConfigExported,
          this,
          &MainWindow::onExportAppConfigDialogComplete);
  connect(export_app_config_dialog_.get(),
          &ExportAppConfigDialog::dialogClosed,
          this,
          &MainWindow::onBusyDialogAborted);

  // fork #45: MO2 import dialog
  import_mo2_dialog_ = std::make_unique<ImportMo2Dialog>(this);
  connect(import_mo2_dialog_.get(),
          &ImportMo2Dialog::importAccepted,
          this,
          &MainWindow::onImportMo2DialogAccepted);
  // fork #1/#2: Nexus Collection import/export dialog. Import reuses the same per-mod
  // download slot as the NexusMods browser (#33).
  collection_dialog_ = std::make_unique<CollectionDialog>(this);
  connect(collection_dialog_.get(),
          &CollectionDialog::modDownloadRequested,
          this,
          &MainWindow::onModDownloadRequested);
  connect(collection_dialog_.get(),
          &CollectionDialog::collectionError,
          this,
          &MainWindow::onReceiveError);
}

void MainWindow::updateModList(const std::vector<ModInfo>& mod_info)
{
  mod_list_model_->setModInfo(mod_info);
  resizeModListColumns();
}

void MainWindow::updateDeployerList(const DeployerInfo& depl_info)
{
  deployer_model_->setDeployerInfo(depl_info);
  resizeDeployerListColumns();
  ui->deployer_list->update();
  emit getModInfo(currentApp());
}

int MainWindow::currentApp()
{
  const int display_idx = ui->app_selection_box->currentIndex();
  if(display_idx < 0 || display_idx >= static_cast<int>(app_combo_id_map_.size()))
    return display_idx; // fallback: identity mapping when map is not yet populated
  return app_combo_id_map_[display_idx];
}

int MainWindow::currentDeployer()
{
  return ui->deployer_selection_box->currentIndex();
}

int MainWindow::currentProfile()
{
  return ui->profile_selection_box->currentIndex();
}

void MainWindow::filterModList()
{
  mod_list_proxy_->setFilterString(search_term_);
  mod_list_proxy_->updateRowCountLabel();
}

void MainWindow::filterDeployerList()
{
  deployer_list_proxy_->setFilterString(search_term_);
  deployer_list_proxy_->updateFilter(false);
  // deployer_list_proxy_->updateRowCountLabel();
}

void MainWindow::setupButtons()
{
  run_app_action_ = new QAction(this);
  run_app_action_->setToolTip("Launch Application");
  run_app_action_->setText("Launch");
  run_app_action_->setIcon(QIcon::fromTheme("system-run"));
  connect(run_app_action_, &QAction::triggered, this, &MainWindow::onLaunchAppButtonClicked);
  add_app_action_ = new QAction(this);
  add_app_action_->setToolTip("New Application");
  add_app_action_->setText("New");
  add_app_action_->setIcon(QIcon::fromTheme("list-add"));
  connect(add_app_action_, &QAction::triggered, this, &MainWindow::onAddAppButtonClicked);
  remove_app_action_ = new QAction(this);
  remove_app_action_->setToolTip("Remove Application");
  remove_app_action_->setText("Remove");
  remove_app_action_->setIcon(QIcon::fromTheme("user-trash"));
  connect(remove_app_action_, &QAction::triggered, this, &MainWindow::onRemoveAppButtonClicked);
  edit_app_action_ = new QAction(this);
  edit_app_action_->setToolTip("Edit Application");
  edit_app_action_->setText("Edit");
  edit_app_action_->setIcon(QIcon::fromTheme("editor"));
  connect(edit_app_action_, &QAction::triggered, this, &MainWindow::on_edit_app_button_clicked);
  // Sort applications alphabetically toggle (limo-app/limo#226).
  sort_apps_alpha_action_ = new QAction(this);
  sort_apps_alpha_action_->setToolTip("Sort applications alphabetically by name");
  sort_apps_alpha_action_->setText("Sort alphabetically");
  sort_apps_alpha_action_->setCheckable(true);
  sort_apps_alpha_action_->setIcon(QIcon::fromTheme("view-sort-ascending"));
  connect(sort_apps_alpha_action_, &QAction::toggled, this, &MainWindow::onSortAppsAlphaToggled);
  // fork #45: Import Mod Organizer 2 setup action
  import_mo2_action_ = new QAction(this);
  import_mo2_action_->setToolTip("Import a Mod Organizer 2 setup into Limo");
  import_mo2_action_->setText("Import MO2");
  import_mo2_action_->setIcon(QIcon::fromTheme("document-import"));
  connect(import_mo2_action_, &QAction::triggered, this, &MainWindow::onImportMo2ActionTriggered);
  // BEGIN feature #33: action opening the in-app NexusMods browser for the current app.
  browse_nexus_action_ = new QAction(this);
  browse_nexus_action_->setToolTip("Browse and search mods on NexusMods");
  browse_nexus_action_->setText("Browse NexusMods");
  browse_nexus_action_->setIcon(QIcon::fromTheme("globe"));
  connect(browse_nexus_action_, &QAction::triggered, this, &MainWindow::onBrowseNexusTriggered);
  // END feature #33.
  QMenu* app_menu = new QMenu(this);
  app_menu->addActions(QList<QAction*>{ run_app_action_,
                                        add_app_action_,
                                        remove_app_action_,
                                        edit_app_action_,
                                        sort_apps_alpha_action_,
                                        import_mo2_action_,
                                        browse_nexus_action_ });
  ui->app_tool_button->setDefaultAction(run_app_action_);
  ui->app_tool_button->setMenu(app_menu);

  add_deployer_action_ = new QAction(this);
  add_deployer_action_->setToolTip("New Deployer");
  add_deployer_action_->setText("New");
  add_deployer_action_->setIcon(QIcon::fromTheme("list-add"));
  connect(add_deployer_action_, &QAction::triggered, this, &MainWindow::onAddDeployerButtonClicked);
  remove_deployer_action_ = new QAction(this);
  remove_deployer_action_->setToolTip("Remove Deployer");
  remove_deployer_action_->setText("Remove");
  remove_deployer_action_->setIcon(QIcon::fromTheme("user-trash"));
  connect(
    remove_deployer_action_, &QAction::triggered, this, &MainWindow::onRemoveDeployerButtonClicked);
  edit_deployer_action_ = new QAction(this);
  edit_deployer_action_->setToolTip("Edit Deployer");
  edit_deployer_action_->setText("Edit");
  edit_deployer_action_->setIcon(QIcon::fromTheme("editor"));
  connect(edit_deployer_action_, &QAction::triggered, this, &MainWindow::onEditDeployerMenuClicked);
  // fork #53: deployment integrity verification action.
  verify_deployer_action_ = new QAction(this);
  verify_deployer_action_->setToolTip("Verify deployed files against staging");
  verify_deployer_action_->setText("Verify Deployment");
  verify_deployer_action_->setIcon(QIcon::fromTheme("emblem-checked"));
  connect(
    verify_deployer_action_, &QAction::triggered, this, &MainWindow::onVerifyDeployerMenuClicked);
  // fork #11: action showing the deployed file tree with per-file mod origin.
  deployed_files_tree_action_ = new QAction(this);
  deployed_files_tree_action_->setToolTip("Show deployed files and their origin mod");
  deployed_files_tree_action_->setText("Deployed Files");
  deployed_files_tree_action_->setIcon(QIcon::fromTheme("view-list-tree"));
  connect(deployed_files_tree_action_,
          &QAction::triggered,
          this,
          &MainWindow::onDeployedFilesTreeMenuClicked);
  // fork #50: health-check action.
  health_check_deployer_action_ = new QAction(this);
  health_check_deployer_action_->setToolTip("Check the deployment for problems");
  health_check_deployer_action_->setText("Health Check");
  health_check_deployer_action_->setIcon(QIcon::fromTheme("emblem-important"));
  connect(health_check_deployer_action_,
          &QAction::triggered,
          this,
          &MainWindow::onHealthCheckDeployerMenuClicked);
  // fork #149: load-order bisect action.
  bisect_deployer_action_ = new QAction(this);
  bisect_deployer_action_->setToolTip("Bisect Load Order");
  bisect_deployer_action_->setText("Bisect");
  bisect_deployer_action_->setIcon(QIcon::fromTheme("edit-find"));
  connect(
    bisect_deployer_action_, &QAction::triggered, this, &MainWindow::onBisectDeployerMenuClicked);
#ifdef LIMO_WITH_LOOT
  // fork #31: action to open the LOOT user-metadata (userlist.yaml) editor.
  edit_loot_userlist_action_ = new QAction(this);
  edit_loot_userlist_action_->setToolTip("Edit LOOT user metadata (groups, load-after rules)");
  edit_loot_userlist_action_->setText("Edit LOOT user metadata");
  edit_loot_userlist_action_->setIcon(QIcon::fromTheme("editor"));
  connect(edit_loot_userlist_action_,
          &QAction::triggered,
          this,
          &MainWindow::onEditLootUserlistMenuClicked);
#endif
  QMenu* deployer_menu = new QMenu(this);
  deployer_menu->addActions(QList<QAction*>{ add_deployer_action_,
                                             remove_deployer_action_,
                                             edit_deployer_action_,
                                             verify_deployer_action_,
                                             deployed_files_tree_action_, // fork #11
                                             health_check_deployer_action_, // fork #50
                                             bisect_deployer_action_, // fork #149
                                             ui->actionbrowse_deployer_files });
#ifdef LIMO_WITH_LOOT
  deployer_menu->addAction(edit_loot_userlist_action_); // fork #31
#endif
  ui->deployer_tool_button->setDefaultAction(add_deployer_action_);
  ui->deployer_tool_button->setMenu(deployer_menu);

  add_profile_action_ = new QAction(this);
  add_profile_action_->setToolTip("New profile");
  add_profile_action_->setText("New");
  add_profile_action_->setIcon(QIcon::fromTheme("list-add"));
  connect(add_profile_action_, &QAction::triggered, this, &MainWindow::onAddProfileButtonClicked);
  remove_profile_action_ = new QAction(this);
  remove_profile_action_->setToolTip("Remove profile");
  remove_profile_action_->setText("Remove");
  remove_profile_action_->setIcon(QIcon::fromTheme("user-trash"));
  connect(
    remove_profile_action_, &QAction::triggered, this, &MainWindow::onRemoveProfileButtonClicked);
  edit_profile_action_ = new QAction(this);
  edit_profile_action_->setToolTip("Edit profile");
  edit_profile_action_->setText("Edit");
  edit_profile_action_->setIcon(QIcon::fromTheme("editor"));
  connect(edit_profile_action_, &QAction::triggered, this, &MainWindow::onEditProfileButtonClicked);
  QMenu* profile_menu = new QMenu(this);
  profile_menu->addActions(
    QList<QAction*>{ add_profile_action_, remove_profile_action_, edit_profile_action_ });
  ui->profile_tool_button->setDefaultAction(add_profile_action_);
  ui->profile_tool_button->setMenu(profile_menu);

  ui->reset_filter_button->setHidden(true);

  ui->export_app_config_button->setIcon(QIcon::fromTheme("document-export"));
}

void MainWindow::showEditDeployerDialog(int deployer)
{
  QString deploy_mode_string =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Mode"))->text();
  Deployer::DeployMode deploy_mode = Deployer::hard_link;
  if(deploy_mode_string == deploy_mode_sym_link)
    deploy_mode = Deployer::sym_link;
  else if(deploy_mode_string == deploy_mode_copy)
    deploy_mode = Deployer::copy;
  add_deployer_dialog_->setEditMode(
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Type"))->text(),
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Name"))->text(),
    deployer_target_paths_[deployer],
    deployer_source_paths_[deployer],
    deploy_mode,
    currentApp(),
    deployer,
    deployer_model_->usesUnsafeSorting(),
    deployer_model_->hasSeparateDirs(),
    deployer_model_->hasIgnoredFiles());
  setBusyStatus(true, false);
  add_deployer_dialog_->show();
}

void MainWindow::onOpenRepositoriesDialog()
{
  // fork #114: lazily create the dialog and wire its install signal into the
  // existing download/import flow.
  if(!repositories_dialog_)
  {
    repositories_dialog_ = std::make_unique<RepositoriesDialog>(this);
    connect(repositories_dialog_.get(),
            &RepositoriesDialog::installPackageRequested,
            this,
            &MainWindow::onRepositoryInstallRequested);
  }
  repositories_dialog_->show();
  repositories_dialog_->raise();
  repositories_dialog_->activateWindow();
}

void MainWindow::onRepositoryInstallRequested(remote::RemoteDownloadInfo info)
{
  // fork #114: route the already-resolved direct download URL through the
  // existing import queue. The download itself is performed by the established
  // ApplicationManager flow; nothing is re-implemented here.
  ImportModInfo import_info;
  import_info.app_id = currentApp();
  import_info.action_type = ImportModInfo::download;
  import_info.remote_download_url = info.download_url;
  import_info.remote_file_name = info.file_name;
  import_info.remote_file_version = info.version;
  import_info.name_overwrite = info.package_name;
  import_info.version_overwrite = info.version;

  const bool was_empty = mod_import_queue_.empty();
  mod_import_queue_.push(import_info);
  if(was_empty)
    importMod();
}

void MainWindow::importMod()
{
  ImportModInfo info = mod_import_queue_.top();
  setBusyStatus(true);
  if(info.action_type == ImportModInfo::download)
  {
    if(!initNexusApiKey())
    {
      mod_import_queue_.pop();
      setBusyStatus(false);
      if(!mod_import_queue_.empty())
        importMod();
      return;
    }
    setStatusMessage("Downloading mod");
    emit downloadMod(info);
  }
  else if(info.action_type == ImportModInfo::extract)
  {
    if(std::filesystem::exists(info.target_path))
    {
      try
      {
        std::filesystem::remove_all(info.target_path);
      }
      catch(std::filesystem::filesystem_error& error)
      {
        onReceiveError(
          "File system error",
          std::format("Error while trying to delete '{}'", info.target_path.string()).c_str());
        setBusyStatus(false);
        return;
      }
    }
    Log::info("Importing mod '" + info.local_source.string() + "'");
    setStatusMessage("Importing mod");
    emit extractArchive(info);
  }
}

void MainWindow::setBusyStatus(bool busy, bool show_progress_bar, bool disable_app_launch)
{
  enableModifyApps(!busy);
  enableModifyBackups(!busy);
  enableModifyDeployers(!busy);
  enableModifyProfiles(!busy);

  if(show_progress_bar | !busy)
  {
    received_progress_ = false;
    last_progress_ = 0.0f;
    last_progress_update_time_ = std::chrono::high_resolution_clock::now();
    progress_bar_->setEnabled(busy);
    progress_bar_->setVisible(busy);
    progress_bar_->setMaximum(0);
    progress_bar_->setMinimum(0);
  }

  if(disable_app_launch | !busy)
    run_app_action_->setEnabled(!busy);
}

int MainWindow::getColumnIndex(QTableWidget* table, QString col_name)
{
  for(int i = 0; i < table->columnCount(); i++)
  {
    if(table->horizontalHeaderItem(i)->text() == col_name)
      return i;
  }
  return -1;
}

void MainWindow::setStatusMessage(QString message, int timeout_ms)
{
  ui->statusbar->showMessage(message, timeout_ms);
}

void MainWindow::setupLog()
{
  // main log
  Log::log_printers.push_back(
    [show_error = &show_log_on_error_,
     show_warning = &show_log_on_warning_,
     log = ui->log_frame,
     log_container = ui->log_container,
     orange = colors::ORANGE,
     red = colors::RED,
     blue = colors::LIGHT_BLUE,
     text_color = palette().color(QPalette::Text)](std::string message, Log::LogLevel level)
    {
      QColor color = text_color;
      if(level == Log::LOG_WARNING)
        color = orange;
      else if(level == Log::LOG_ERROR)
        color = red;
      else if(level == Log::LOG_DEBUG)
        color = blue;
      log->moveCursor(QTextCursor::End);
      log->appendHtml(QString("<p style='color: " + color.name(QColor::HexRgb) + "'>") +
                      QString(message.c_str()).toHtmlEscaped().replace("\n", "<br/>") + "</p>");
      if(*show_error && level <= Log::LOG_ERROR || *show_warning && level <= Log::LOG_WARNING)
        log_container->setVisible(true);
    });
  // tool log
  Log::log_printers.push_back(
    [show_error = &show_log_on_error_,
     show_warning = &show_log_on_warning_,
     log = ui->tool_log_frame,
     log_container = ui->log_container,
     orange = colors::ORANGE,
     red = colors::RED,
     blue = colors::LIGHT_BLUE,
     text_color = palette().color(QPalette::Text)](std::string message, Log::LogLevel level)
    {
      QColor color = text_color;
      if(level == Log::LOG_WARNING)
        color = orange;
      else if(level == Log::LOG_ERROR)
        color = red;
      else if(level == Log::LOG_DEBUG)
        color = blue;
      log->moveCursor(QTextCursor::End);
      log->appendHtml(QString("<p style='color: " + color.name(QColor::HexRgb) + "'>") +
                      QString(message.c_str()).toHtmlEscaped().replace("\n", "<br/>") + "</p>");
      if(*show_error && level <= Log::LOG_ERROR || *show_warning && level <= Log::LOG_WARNING)
        log_container->setVisible(true);
    });

  QStringList config_paths = QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
  if(!config_paths.empty())
    Log::init(std::filesystem::path(config_paths[0].toStdString()) / "logs");
  ui->log_container->setVisible(false);
  ui->log_frame->setMaximumBlockCount(1000);
  auto button = new QPushButton(this);
  button->setText("Log");
  button->setStyleSheet("margin:0;padding:0");
  button->setFlat(true);
  ui->statusbar->addPermanentWidget(button);
  connect(button, &QPushButton::pressed, this, &MainWindow::onLogButtonPressed);
}

QPair<QString, int> MainWindow::runCommand(QString command, bool ignore_flatpak)
{
  QString output;
  std::array<char, 128> buffer;
  if(is_a_flatpak_ && !ignore_flatpak)
    command = "flatpak-spawn --host " + command;
  command += "  2>&1";
  auto pipe = popen(command.toStdString().c_str(), "r");
  if(pipe == nullptr)
  {
    Log::error("Failed to run command: Pipe could not be opened");
    return { "Error: Failed to open pipe!", -1 };
  }
  while(!feof(pipe))
  {
    if(fgets(buffer.data(), buffer.size(), pipe) != nullptr)
      output += buffer.data();
  }
  int ret_code = pclose(pipe) / 256;
  return { output, ret_code };
}

void MainWindow::runConcurrent(QString command, QString name, QString type, bool ignore_flatpak)
{
  Log::info(
    ("Running " + type.toLower() + " '" + name + "' with command '" + command + "'").toStdString());
  auto watcher = new QFutureWatcher<QPair<QString, int>>;
  connect(
    watcher,
    &QFutureWatcher<QPair<QString, int>>::finished,
    [watcher, name, type]()
    {
      auto result = watcher->result();
      if(!result.first.isEmpty())
        Log::info((type + " '" + name + "' output: \n" + result.first).toStdString(), LOG_TOOLS);
      Log::info((type + " '" + name + "' exited with return code " + QString::number(result.second))
                  .toStdString());
      delete watcher;
    });
  auto future = QtConcurrent::run(
    [this, command, ignore_flatpak]() { return runCommand(command, ignore_flatpak); });
  watcher->setFuture(future);
}

void MainWindow::loadSettings()
{
  QSettings settings = QSettings(QCoreApplication::applicationName());
  restoreGeometry(settings.value("main/geometry").toByteArray());
  restoreState(settings.value("main/state").toByteArray());
  int tab = settings.value("current_tab", 0).toInt();
  if(ui->app_tab_widget->count() > tab)
    ui->app_tab_widget->setCurrentIndex(tab);
  ask_remove_from_deployer_ = settings.value("ask_remove_from_deployer", true).toBool();
  ask_remove_mod_ = settings.value("ask_remove_mod", true).toBool();
  ask_remove_profile_ = settings.value("ask_remove_profile", true).toBool();
  mod_list_slider_pos_ = settings.value("mod_list_slider_pos", 0).toInt();
  deployer_list_slider_pos_ = settings.value("deployer_list_slider_pos", 0).toInt();
#ifdef LIMO_WITH_LOOT
  LootDeployer::LIST_URLS[loot::GameType::fo3] =
    settings.value("fo3_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::fo3).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::fo4] =
    settings.value("fo4_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::fo4).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::fo4vr] =
    settings.value("fo4vr_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::fo4vr).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::fonv] =
    settings.value("fonv_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::fonv).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::starfield] =
    settings
      .value("starfield_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::starfield).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::tes3] =
    settings.value("tes3_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::tes3).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::tes4] =
    settings.value("tes4_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::tes4).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::tes5] =
    settings.value("tes5_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::tes5).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::tes5se] =
    settings.value("tes5se_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::tes5se).c_str())
      .toString()
      .toStdString();
  LootDeployer::LIST_URLS[loot::GameType::tes5vr] =
    settings.value("tes5vr_url", LootDeployer::DEFAULT_LIST_URLS.at(loot::GameType::tes5vr).c_str())
      .toString()
      .toStdString();
  LootDeployer::PRELUDE_URL =
    settings.value("prelude_url", LootDeployer::DEFAULT_PRELUDE_URL.c_str())
      .toString()
      .toStdString();
#endif
  deploy_for_all_ = settings.value("deploy_for_all", true).toBool();
  show_log_on_error_ = settings.value("log_on_error", true).toBool();
  show_log_on_warning_ = settings.value("log_on_warning", true).toBool();
  Log::log_level =
    static_cast<Log::LogLevel>(settings.value("log_level", Log::LogLevel::LOG_INFO).toInt());
  if(debug_mode_)
    Log::log_level = Log::LOG_DEBUG;
  ask_remove_backup_target_ = settings.value("ask_remove_backup_target", true).toBool();
  ask_remove_backup_ = settings.value("ask_remove_backup", true).toBool();
  ask_remove_tool_ = settings.value("ask_remove_tool", true).toBool();
  settings.beginGroup("nexus");
  const bool has_nexus_account = settings.value("info_is_valid", false).toBool();
  ui->check_mod_updates_button->setVisible(has_nexus_account);
  settings.endGroup();
  // Restore column widths for both lists (fork #142 / Vortex#23247).
  // saveState / restoreState also encodes the sort indicator, so prefer it;
  // fall back to the legacy mod_list_sort_column/order keys only when no
  // header state has been saved yet.
  const QByteArray mod_list_header_state =
    settings.value("mod_list_header_state").toByteArray();
  if(!mod_list_header_state.isEmpty())
  {
    ui->mod_list->header()->restoreState(mod_list_header_state);
  }
  else
  {
    // Legacy fallback: explicit sort column / order (pre-#142 settings).
    const int mod_list_sort_column =
      settings.value("mod_list_sort_column", ModListModel::time_col).toInt();
    const int mod_list_sort_order =
      settings.value("mod_list_sort_order", Qt::SortOrder::DescendingOrder).toInt();
    if(mod_list_sort_column >= 0 && mod_list_sort_column < mod_list_model_->columnCount() &&
       (mod_list_sort_order == Qt::SortOrder::DescendingOrder ||
        mod_list_sort_order == Qt::SortOrder::AscendingOrder))
    {
      ui->mod_list->sortByColumn(mod_list_sort_column,
                                 static_cast<Qt::SortOrder>(mod_list_sort_order));
    }
  }
  const QByteArray deployer_list_header_state =
    settings.value("deployer_list_header_state").toByteArray();
  if(!deployer_list_header_state.isEmpty())
    ui->deployer_list->header()->restoreState(deployer_list_header_state);
  sort_apps_alphabetically_ = settings.value("sort_apps_alphabetically", false).toBool();
  sort_apps_alpha_action_->setChecked(sort_apps_alphabetically_);
}

void MainWindow::setTabWidgetStyleSheet()
{
  const auto WINDOW_COLOR = QPalette().color(QPalette::ColorRole::Window);
  const auto TEXT_COLOR = QPalette().color(QPalette::ColorRole::Text);
  constexpr float BG_FACTOR = 0.75f;
  const QColor BORDER_COLOR{
    (int)std::round(BG_FACTOR * WINDOW_COLOR.red() + (1 - BG_FACTOR) * TEXT_COLOR.red()),
    (int)std::round(BG_FACTOR * WINDOW_COLOR.green() + (1 - BG_FACTOR) * TEXT_COLOR.green()),
    (int)std::round(BG_FACTOR * WINDOW_COLOR.blue() + (1 - BG_FACTOR) * TEXT_COLOR.blue())
  };
  ui->app_tab_widget->setStyleSheet("QTabWidget::pane {margin: 0 1 0 1; border-top: 1 solid " +
                                    BORDER_COLOR.name(QColor::HexRgb) +
                                    "; border-radius: 0; padding: -6}");
}

std::vector<bool> MainWindow::getAutonomousDeployers()
{
  std::vector<bool> auto_deployers;
  for(int i = 0; i < ui->info_deployer_list->rowCount(); i++)
  {
    auto_deployers.push_back(DeployerFactory::AUTONOMOUS_DEPLOYERS.at(
      ui->info_deployer_list->item(i, getColumnIndex(ui->info_deployer_list, "Type"))
        ->text()
        .toStdString()));
  }
  return auto_deployers;
}

void MainWindow::enableModifyApps(bool enabled)
{
  const auto actions = mod_list_menu_->actions();
  for(auto action : actions)
    if(action != ui->actionbrowse_mod_files || enabled)
      action->setEnabled(enabled);

  add_app_action_->setEnabled(enabled);
  edit_app_action_->setEnabled(enabled);
  remove_app_action_->setEnabled(enabled);
  mod_list_model_->setIsEditable(enabled);
  ui->mod_list->setEnableButtons(enabled);
  ui->edit_app_button->setEnabled(enabled);
  ui->check_mod_updates_button->setEnabled(enabled);
  ui->settings_button->setEnabled(enabled);
}

void MainWindow::enableModifyDeployers(bool enabled)
{
  const auto actions = deployer_list_menu_->actions();
  for(auto action : actions)
    if(action != ui->actionbrowse_mod_files || enabled)
      action->setEnabled(enabled);

  ui->deploy_button->setEnabled(enabled);
  ui->undeploy_button->setEnabled(enabled);
  ui->info_deployer_list->setEnabled(enabled);
  ui->deployer_list->setEnableButtons(enabled);
  ui->deployer_list->setEnableDragReorder(enabled);
  add_deployer_action_->setEnabled(enabled);
  edit_deployer_action_->setEnabled(enabled);
  remove_deployer_action_->setEnabled(enabled);
  if(!enabled)
    ui->deployer_tool_button->setDefaultAction(ui->actionbrowse_deployer_files);
  else
    ui->deployer_tool_button->setDefaultAction(add_deployer_action_);
}

void MainWindow::enableModifyBackups(bool enabled)
{
  const auto actions = backup_list_menu_->actions();
  for(auto action : actions)
    action->setEnabled(enabled);

  backup_list_model_->setIsEditable(enabled);
  ui->backup_list->setEnableButtons(enabled);
}

void MainWindow::enableModifyProfiles(bool enabled)
{
  remove_profile_action_->setEnabled(enabled && ui->profile_selection_box->count() > 1);
  add_profile_action_->setEnabled(enabled);
  edit_profile_action_->setEnabled(enabled);
  ui->profile_selection_box->setEnabled(enabled);
}

bool MainWindow::initNexusApiKey()
{
  if(nexus::Api::isInitialized())
    return true;

  auto result = settings_dialog_->getNexusApiKeyDetails();
  if(!result)
  {
    const QString message = "Could not find an API key. Please enter one in the settings dialog.";
    Log::error(message.toStdString());
    QMessageBox error_box(QMessageBox::Critical, "Error", message, QMessageBox::Ok);
    error_box.exec();
    return false;
  }
  const auto [cipher, nonce, tag, is_default_pw] = *result;

  std::string pw = cryptography::default_key;
  if(!is_default_pw)
  {
    EnterApiPwDialog dialog(cipher, nonce, tag, this);
    dialog.exec();
    if(!dialog.wasSuccessful())
      return false;
    nexus::Api::setApiKey(dialog.getApiKey());
    return true;
  }
  std::string api_key;
  try
  {
    api_key = cryptography::decrypt(cipher, pw, nonce, tag);
  }
  catch(CryptographyError& e)
  {
    const QString message = "Error during key decryption.";
    Log::error(message.toStdString());
    QMessageBox error_box(QMessageBox::Critical, "Error", message, QMessageBox::Ok);
    error_box.exec();
  }
  nexus::Api::setApiKey(api_key);
  return true;
}

void MainWindow::setupIpcServer()
{
  ipc_server_ = std::make_unique<IpcServer>();
  ipc_server_->setup();
  connect(ipc_server_.get(), &IpcServer::receivedMessage, this, &MainWindow::onReceiveIpcMessage);
}

void MainWindow::initUiWithoutApps(bool has_apps)
{
  // lists
  ui->deployer_list->setEnabled(has_apps);
  ui->mod_list->setEnabled(has_apps);
  ui->info_deployer_list->setEnabled(has_apps);
  ui->info_tool_list->setEnabled(has_apps);
  // buttons
  ui->check_mod_updates_button->setEnabled(has_apps);
  ui->edit_app_button->setEnabled(has_apps);
  ui->filters_button->setEnabled(has_apps);
  ui->deployer_tool_button->setEnabled(has_apps);
  ui->profile_tool_button->setEnabled(has_apps);
  ui->deploy_button->setEnabled(has_apps);
  ui->undeploy_button->setEnabled(has_apps);
  // combo boxes
  ui->deployer_selection_box->setEnabled(has_apps);
  ui->app_selection_box->setEnabled(has_apps);
  ui->profile_selection_box->setEnabled(has_apps);
  // app tool box
  run_app_action_->setEnabled(has_apps);
  remove_app_action_->setEnabled(has_apps);
  edit_app_action_->setEnabled(has_apps);
  ui->app_tool_button->setDefaultAction(has_apps ? run_app_action_ : add_app_action_);
  // other
  ui->app_tab_widget->setEnabled(has_apps);
  ui->search_field->setEnabled(has_apps);
  // fork #25: show the empty-state overlay when no application is configured.
  if(empty_state_overlay_)
  {
    if(has_apps)
      empty_state_overlay_->hide();
    else
    {
      updateEmptyStateOverlayGeometry();
      empty_state_overlay_->raise();
      empty_state_overlay_->show();
    }
  }
}

void MainWindow::checkForContainers()
{
  if(getenv("container"))
    is_a_flatpak_ = getenv("container") == std::string("flatpak");
  Installer::setIsAFlatpak(is_a_flatpak_);
  Log::debug(is_a_flatpak_ ? "Running as a flatpak" : "Running natively");
}

void MainWindow::updateOutdatedSettings()
{
  auto settings = QSettings(QCoreApplication::applicationName());
  previous_app_version_ = settings.value("app_version", "1.0.4").toString();

#ifdef LIMO_WITH_LOOT
  if(versionIsLessOrEqual(previous_app_version_, "1.0.4"))
  {
    const std::map<std::string, std::string> old_urls = {
      { "fo3_url", "https://raw.githubusercontent.com/loot/fallout3/master/masterlist.yaml" },
      { "fo4_url", "https://raw.githubusercontent.com/loot/fallout4/master/masterlist.yaml" },
      { "fo4vr_url", "https://raw.githubusercontent.com/loot/fallout4vr/master/masterlist.yaml" },
      { "fonv_url", "https://raw.githubusercontent.com/loot/falloutnv/master/masterlist.yaml" },
      { "starfield_url",
        "https://raw.githubusercontent.com/loot/starfield/master/masterlist.yaml" },
      { "tes3_url", "https://raw.githubusercontent.com/loot/morrowind/master/masterlist.yaml" },
      { "tes4_url", "https://raw.githubusercontent.com/loot/oblivion/master/masterlist.yaml" },
      { "tes5_url", "https://raw.githubusercontent.com/loot/skyrim/master/masterlist.yaml" },
      { "tes5se_url", "https://raw.githubusercontent.com/loot/skyrimse/master/masterlist.yaml" },
      { "tes5vr_url", "https://raw.githubusercontent.com/loot/skyrimvr/master/masterlist.yaml" }
    };
    // clang-format off
    const std::map<std::string, loot::GameType> game_types = {
       { "fo3_url", loot::GameType::fo3 },
       { "fo4_url", loot::GameType::fo4 },
       { "fo4vr_url", loot::GameType::fo4vr},
       { "fonv_url", loot::GameType::fonv },
       { "starfield_url", loot::GameType::starfield },
       { "tes3_url", loot::GameType::tes3 },
       { "tes4_url", loot::GameType::tes4 },
       { "tes5_url", loot::GameType::tes5 },
       { "tes5se_url", loot::GameType::tes5se },
       { "tes5vr_url", loot::GameType::tes5vr }
    };
    // clang-format on
    for(const auto& [key, old_url] : old_urls)
    {
      if(settings.value(key.c_str()).toString().toStdString() == old_url)
        settings.setValue(key.c_str(),
                          LootDeployer::DEFAULT_LIST_URLS.at(game_types.at(key)).c_str());
    }
    Log::info("Default LOOT masterlist URLs have been updated");
  }
#endif

  settings.setValue("app_version", QString(APP_VERSION));
}

bool MainWindow::versionIsLessOrEqual(QString current_version, QString target_version)
{
  std::regex regex(R"([^\d\.])");
  if(std::regex_search(current_version.toStdString(), regex))
    return true;
  if(std::regex_search(target_version.toStdString(), regex))
    return false;

  for(const auto& [cur_sub, cur_target] :
      stv::zip(current_version.split("."), target_version.split(".")))
  {
    if(cur_sub.isEmpty() || cur_target.isEmpty())
      continue;
    if(cur_sub.toInt() > cur_target.toInt())
      return false;
  }
  return true;
}

void MainWindow::initRootLevelConditions()
{
  root_level_conditions_.clear();
  if(app_info_.steam_app_id == -1)
    return;

  sfs::path config_path =
    sfs::path(is_a_flatpak_ ? "/app" : APP_INSTALL_PREFIX) / "share/limo/steam_app_configs";
  // Overwrite for local build
  if(!is_a_flatpak_ && sfs::exists("steam_app_configs"))
    config_path = "steam_app_configs";
  Log::debug("Config path: " + config_path.string());
  if(!sfs::exists(config_path))
  {
    Log::debug(
      "Could not find \"steam_app_configs\" directory. " "Make sure Limo is installed correctly");
    return;
  }

  config_path /= (std::to_string(app_info_.steam_app_id) + ".json");
  if(!sfs::exists(config_path))
  {
    return;
  }

  Json::Value json;
  std::ifstream file(config_path, std::fstream::binary);
  if(!file.is_open())
  {
    Log::debug("Failed to open app settings file at: " + config_path.string());
    return;
  }
  try
  {
    file >> json;
  }
  catch(Json::Exception& e)
  {
    Log::debug("Failed to read from app settings file at: " + config_path.string() +
               ". Error was: " + e.what());
    return;
  }
  catch(...)
  {
    Log::debug("Failed to read from app settings file at: " + config_path.string());
    return;
  }

  if(!json.isMember(JSON_ROOT_LEVEL_KEY))
    return;
  for(int i = 0; i < json[JSON_ROOT_LEVEL_KEY].size(); i++)
  {
    try
    {
      root_level_conditions_.emplace_back(json[JSON_ROOT_LEVEL_KEY][i]);
    }
    catch(std::runtime_error& e)
    {
      Log::debug(std::format("Failed to parse root level config for app {}. \nError: {}",
                             app_info_.steam_app_id,
                             e.what()));
    }
    catch(Json::Exception& e)
    {
      Log::debug(std::format("Failed to parse root level config for app {}. \nError: {}",
                             app_info_.steam_app_id,
                             e.what()));
    }
    catch(...)
    {
      Log::debug(std::format("Unknown error while parsing root level config for app {}.",
                             app_info_.steam_app_id));
    }
  }
}

void MainWindow::onModAdded(QList<QUrl> paths)
{
  const bool was_empty = mod_import_queue_.empty();
  for(const QUrl& url : paths)
  {
    ImportModInfo info;
    info.app_id = currentApp();
    info.action_type = ImportModInfo::extract;
    info.local_source = url.path().toStdString();
    info.target_path = ui->info_sdir_label->text().toStdString();
    info.target_path /= temp_dir_.toStdString();
    mod_import_queue_.push(info);
  }
  if(was_empty)
    importMod();
}

void MainWindow::onAddModDialogAccept(int app_id, ImportModInfo info)
{
  setBusyStatus(true);
  setStatusMessage(QString("Installing \"") + info.name.c_str() + "\"");
  Log::info("Installing mod '" + info.name + "'");
  emit installMod(app_id, info);
  emit getDeployerInfo(app_id, currentDeployer());
}

void MainWindow::onDeployerBoxChange(int mod_id, bool status)
{
  emit setModStatus(currentApp(), currentDeployer(), mod_id, status);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onGetModInfo(std::vector<ModInfo> mod_info)
{
  updateModList(mod_info);
  mod_list_proxy_->updateRowCountLabel();

  // fork #45: if we have a pending MO2 import and the new app is now selected,
  // kick off the first mod installation.
  if(!mo2_pending_mods_.empty() && mo2_pending_app_id_ >= 0 &&
     currentApp() == mo2_pending_app_id_)
  {
    installNextMo2Mod();
    return;
  }

  if(!is_initialized_ && !mod_import_queue_.empty())
  {
    ImportModInfo info = mod_import_queue_.top();
    mod_import_queue_.pop();
    info.app_id = currentApp();
    info.queue_time = std::chrono::high_resolution_clock::now();
    mod_import_queue_.push(info);
    importMod();
  }
  is_initialized_ = true;
}

void MainWindow::onGetDeployerInfo(DeployerInfo depl_info)
{
  // fork #45: second pass — apply enabled/disabled states after MO2 import
  if(mo2_finalising_ && mo2_app_id_finalising_ >= 0)
  {
    mo2_finalising_ = false;
    const int app_id = mo2_app_id_finalising_;
    mo2_app_id_finalising_ = -1;
    // Map each imported mod's name to its id via the (now populated) mod list, then disable any
    // mod that was disabled in the source MO2 profile. The simple deployer's traversal items don't
    // carry mod names, so the mod list is the reliable name->id source.
    for(const auto& info : mod_list_model_->getModInfo())
    {
      auto it = mo2_mod_enabled_map_.find(info.mod.name);
      if(it != mo2_mod_enabled_map_.end() && !it->second)
        emit setModStatus(app_id, 0, info.mod.id, false);
    }
    mo2_mod_enabled_map_.clear();
    onCompletedOperations("MO2 import complete");
    emit getModInfo(app_id);
    emit getDeployerInfo(app_id, 0);
    setBusyStatus(false);
    return;
  }

  setWindowTitle(ui->app_selection_box->currentText() + " - Limo");
  ui->actionremove_from_deployer->setVisible(!depl_info.is_autonomous);
  ui->actionget_file_conflicts->setVisible(depl_info.supports_file_conflicts);
  ui->actionget_mod_conflicts->setVisible(depl_info.supports_mod_conflicts);
  ui->actionbrowse_mod_files->setVisible(ui->app_tab_widget->currentIndex() != deployer_tab_idx ||
                                         depl_info.supports_file_browsing);
  ui->actionSort_Mods->setVisible(depl_info.supports_sorting);
  ui->actionmove_mod->setVisible(depl_info.supports_reordering);
  ui->actionAdd_to_Ignore_List->setVisible(depl_info.type == DeployerFactory::REVERSEDEPLOYER);
  ui->deployer_list->setEnableDragReorder(depl_info.supports_reordering);

  for(QAction* action : deployer_mod_actions_)
  {
    deployer_list_menu_->removeAction(action);
    delete action;
  }
  deployer_mod_actions_.clear();
  for(const auto& [i, pair] : str::enumerate_view(depl_info.mod_actions))
  {
    const auto& [name, icon] = pair;
    deployer_mod_actions_.push_back(new ListAction((int)i));
    deployer_mod_actions_.back()->setText(name.c_str());
    connect(deployer_mod_actions_.back(),
            &ListAction::triggeredAt,
            this,
            &MainWindow::onModActionTriggered);
    if(!icon.empty())
      deployer_mod_actions_.back()->setIcon(QIcon::fromTheme(icon.c_str()));
    bool action_inserted = false;
    const auto actions = deployer_list_menu_->actions();
    if(actions.empty())
    {
      deployer_list_menu_->addAction(deployer_mod_actions_.back());
      action_inserted = true;
    }
    else
    {
      for(int i = 0; i < deployer_list_menu_->actions().size(); i++)
      {
        if(actions[i]->text().compare(name.c_str(), Qt::CaseInsensitive) > 0)
        {
          deployer_list_menu_->insertAction(actions[i], deployer_mod_actions_.back());
          action_inserted = true;
          break;
        }
      }
    }
    if(!action_inserted)
      deployer_list_menu_->addAction(deployer_mod_actions_.back());
  }

  for(auto cb : depl_tag_cbs_)
    delete cb;
  depl_tag_cbs_.clear();
  const auto tag_filters = deployer_list_proxy_->getTagFilters();
  for(const auto& [tag, num_mods] : depl_info.mods_per_tag)
  {
    auto cb = new TagCheckBox(tag.c_str(), num_mods);
    cb->setToolTip(QString("Filter for mods with tag '") + tag.c_str() + "'");
    auto iter = str::find_if(tag_filters, [tag](auto& pair) { return pair.first == tag.c_str(); });
    if(iter != str::end(tag_filters))
      cb->setCheckState(iter->second ? Qt::PartiallyChecked : Qt::Checked);
    connect(cb, &TagCheckBox::tagBoxChecked, this, &MainWindow::onDeplTagFilterChanged);
    depl_tag_cbs_.push_back(cb);
    ui->deployer_tags_widget->layout()->addWidget(cb);
  }

  updateDeployerList(depl_info);
  deployer_list_proxy_->setConflictGroups(depl_info.conflict_groups);
  // Rebuild the mod-id -> conflicting-mod-ids map used to highlight conflicts on selection (#143).
  mod_conflict_groups_.clear();
  for(const auto& group : depl_info.conflict_groups)
  {
    for(int id : group)
    {
      std::set<int> others(group.begin(), group.end());
      others.erase(id);
      mod_conflict_groups_[id] = others;
    }
  }
  emit getAppInfo(currentApp());
}

void MainWindow::onAddAppDialogComplete(EditApplicationInfo info)
{
  Log::info("Adding new application '" + info.name + "'");
  setBusyStatus(true);
  setStatusMessage("Adding application");
  emit addApplication(info);
  emit getApplicationNames(true);
}

void MainWindow::onAddDeployerDialogComplete(EditDeployerInfo info, int app_id)
{
  Log::info("Adding new deployer '" + info.name + "'");
  setBusyStatus(true);
  setStatusMessage("Adding deployer");
  emit addDeployer(app_id, info);
  if(currentApp() == app_id)
    emit getDeployerNames(app_id, true);
}

void MainWindow::onGetApplicationNames(QStringList names, QStringList icon_paths, bool is_new)
{
  initUiWithoutApps(!names.isEmpty());
  if(names.isEmpty())
  {
    app_combo_id_map_.clear();
    ui->info_name_label->setText("");
    ui->info_version_label->setText("");
    ui->info_sdir_label->setText("");
    ui->info_mods_label->setText("");
    ui->info_command_label->setText("");
    ui->info_deployer_list->setRowCount(0);
    ui->info_tool_list->setRowCount(0);
    if(!is_initialized_)
      onAddAppButtonClicked();
    return;
  }

  // Remember which real app_id was selected before rebuilding the combo.
  const int prev_real_id = currentApp();

  // Build a sorted or identity index mapping (display index -> real app ID).
  // The sort only reorders how entries appear; the underlying IDs stay the same.
  // Implements limo-app/limo#226.
  const int n = names.size();
  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  if(sort_apps_alphabetically_)
  {
    std::stable_sort(order.begin(), order.end(), [&names](int a, int b) {
      return names[a].toLower() < names[b].toLower();
    });
  }
  app_combo_id_map_.resize(n);
  for(int display_idx = 0; display_idx < n; display_idx++)
    app_combo_id_map_[display_idx] = order[display_idx];

  bool block = ui->app_selection_box->signalsBlocked();
  ui->app_selection_box->blockSignals(true);
  ui->app_selection_box->clear();
  for(int display_idx = 0; display_idx < n; display_idx++)
  {
    const int real_id = order[display_idx];
    if(icon_paths[real_id].isEmpty())
      ui->app_selection_box->addItem(names[real_id]);
    else
      ui->app_selection_box->addItem(QIcon(icon_paths[real_id]), names[real_id]);
    ui->app_selection_box->setItemData(display_idx, icon_paths[real_id], Qt::UserRole);
  }

  // Determine which display index to select.
  if(is_new)
  {
    // A new app was just added; it is at the last real index (n-1).
    // Find its display position.
    for(int display_idx = 0; display_idx < n; display_idx++)
    {
      if(app_combo_id_map_[display_idx] == n - 1)
      {
        ui->app_selection_box->setCurrentIndex(display_idx);
        break;
      }
    }
  }
  else if(!is_initialized_)
  {
    // On first load restore by the saved real app ID.
    const int saved_id =
      QSettings(QCoreApplication::applicationName()).value("current_app", 0).toInt();
    bool found = false;
    for(int display_idx = 0; display_idx < n; display_idx++)
    {
      if(app_combo_id_map_[display_idx] == saved_id)
      {
        ui->app_selection_box->setCurrentIndex(display_idx);
        found = true;
        break;
      }
    }
    if(!found && n > 0)
      ui->app_selection_box->setCurrentIndex(0);
  }
  else
  {
    // Normal refresh: keep the same real app selected.
    bool found = false;
    for(int display_idx = 0; display_idx < n; display_idx++)
    {
      if(app_combo_id_map_[display_idx] == prev_real_id)
      {
        ui->app_selection_box->setCurrentIndex(display_idx);
        found = true;
        break;
      }
    }
    if(!found && n > 0)
      ui->app_selection_box->setCurrentIndex(0);
  }

  ui->app_selection_box->blockSignals(block);

  emit getDeployerNames(currentApp(), is_new);
}

void MainWindow::onGetDeployerNames(QStringList names, bool is_new)
{
  if(names.size() == 0)
  {
    remove_deployer_action_->setEnabled(false);
    edit_deployer_action_->setEnabled(false);
    ui->actionbrowse_deployer_files->setEnabled(false);
    ui->deploy_button->setEnabled(false);
    ui->undeploy_button->setEnabled(false);
  }
  else
  {
    remove_deployer_action_->setEnabled(true);
    edit_deployer_action_->setEnabled(true);
    ui->actionbrowse_deployer_files->setEnabled(true);
    ui->deployer_add_separator_button->setEnabled(true);
    ui->deploy_button->setEnabled(true);
    ui->undeploy_button->setEnabled(true);
  }
  auto settings = QSettings(QCoreApplication::applicationName());
  settings.beginGroup(QString::number(currentApp()));
  int cur_deployer = settings.value("current_deployer", 0).toInt();
  settings.endGroup();
  bool block = ui->deployer_selection_box->signalsBlocked();
  ui->deployer_selection_box->blockSignals(true);
  ui->deployer_selection_box->clear();
  for(const auto& name : names)
    ui->deployer_selection_box->addItem(name);
  if(is_new)
    ui->deployer_selection_box->setCurrentIndex(ui->deployer_selection_box->count() - 1);
  else if(cur_deployer < ui->deployer_selection_box->count() && cur_deployer >= 0)
    ui->deployer_selection_box->setCurrentIndex(cur_deployer);
  ui->deployer_selection_box->blockSignals(block);
  emit getBackupInfo(currentApp());
  emit getProfileNames(currentApp(), false);
}

void MainWindow::onModListContextMenu(QPoint pos)
{
  auto idx = mod_list_proxy_->mapToSource(ui->mod_list->indexAt(pos));

  if(idx.row() < 0)
    return;

  if(ui->mod_list->getNumSelectedRows() > 1)
  {
    ui->actionAdd_to_Group->setVisible(false);
    ui->actionRemove_from_Group->setVisible(false);
    ui->actionbrowse_mod_files->setVisible(false);
    ui->actionRemove_Mods->setVisible(true);
    bool contains_groups = false;
    const auto indices = ui->mod_list->getSelectedRowIndices();
    for(const auto& index : indices)
    {
      if(index.data(ModListModel::mod_group_role).toInt() > -1)
      {
        contains_groups = true;
        break;
      }
    }
    ui->actionRemove_Other_Versions->setVisible(contains_groups);
    ui->actionEdit_Mod_Sources->setVisible(false);
    ui->actionShow_Nexus_Page->setVisible(false);
    ui->actionReinstall_From_Local->setVisible(false);
    ui->actionCheck_For_Updates->setVisible(true);
    ui->actionSuppress_Update->setVisible(true);
    edit_note_action_->setVisible(false);
    pin_version_action_->setVisible(false);
    unpin_version_action_->setVisible(false);
    mod_rules_action_->setVisible(false);
  }
  else
  {
    const int mod_id = mod_list_model_->data(idx, ModListModel::mod_id_role).toInt();
    if(mod_list_model_->getGroupMap().contains(mod_id))
    {
      ui->actionAdd_to_Group->setVisible(false);
      ui->actionRemove_from_Group->setVisible(true);
    }
    else
    {
      ui->actionAdd_to_Group->setVisible(true);
      ui->actionRemove_from_Group->setVisible(false);
    }
    ui->actionbrowse_mod_files->setVisible(true);
    ui->actionRemove_Mods->setVisible(false);
    ui->actionRemove_Other_Versions->setVisible(idx.data(ModListModel::mod_group_role).toInt() >
                                                -1);
    ui->actionEdit_Mod_Sources->setVisible(true);
    const auto url = idx.data(ModListModel::remote_source_role).toString().toStdString();
    const bool is_valid_remote = nexus::Api::modUrlIsValid(url);
    ui->actionShow_Nexus_Page->setVisible(is_valid_remote);
    ui->actionCheck_For_Updates->setVisible(is_valid_remote);
    const auto local_path = idx.data(ModListModel::local_source_role).toString().toStdString();
    ui->actionReinstall_From_Local->setVisible(!local_path.empty() &&
                                               std::filesystem::exists(local_path));
    ui->actionSuppress_Update->setVisible(idx.data(ModListModel::has_update_role).toBool());
    edit_note_action_->setVisible(true);
    mod_rules_action_->setVisible(true);
    const bool is_pinned = idx.data(ModListModel::mod_pinned_role).toBool();
    pin_version_action_->setVisible(!is_pinned);
    unpin_version_action_->setVisible(is_pinned);
  }
  mod_list_menu_->exec(ui->mod_list->mapToGlobal(pos));
}

void MainWindow::onDeployerListContextMenu(QPoint pos)
{
  for(const auto& [i, action] : str::enumerate_view(deployer_mod_actions_))
  {
    auto valid_actions = ui->deployer_list->currentIndex()
                           .data(DeployerListModel::valid_mod_actions_role)
                           .value<std::vector<int>>();
    if(str::find(valid_actions, (int)i) != valid_actions.end())
      action->setVisible(true);
    else
      action->setVisible(false);
  }

  // Game-specific tool actions: only show them for the matching deployer type.
  const int depl = currentDeployer();
  const std::string depl_type =
    (depl >= 0 && depl < static_cast<int>(app_info_.deployer_types.size()))
      ? app_info_.deployer_types[depl]
      : std::string();
  const bool is_tw3 = depl_type == DeployerFactory::WITCHER3DEPLOYER;
  const bool is_cp = depl_type == DeployerFactory::CYBERPUNKDEPLOYER;
  merge_tw3_scripts_action_->setVisible(is_tw3);
  merge_tw3_config_action_->setVisible(is_tw3);
  cyberpunk_setup_action_->setVisible(is_cp);
  deploy_redmods_action_->setVisible(is_cp);

  bool has_visible_actions = false;
  for(auto&& action : deployer_list_menu_->actions())
  {
    if(action->isVisible())
    {
      has_visible_actions = true;
      break;
    }
  }
  if(!has_visible_actions)
    return;
  auto idx = ui->deployer_list->indexAt(pos);
  // Show the context menu if a row is selected, or if the deployer list has at least one entry
  // (so "Export Mod List" is still accessible when clicking empty space) (fork #7).
  if(idx.row() >= 0 || deployer_model_->rowCount() > 0)
    deployer_list_menu_->exec(ui->deployer_list->mapToGlobal(pos));
}

void MainWindow::onAddToDeployerAccept(std::vector<int>& mod_ids, std::vector<bool> deployers)
{
  if(mod_ids.size() == 1)
    Log::info("Changing deployers for mod with id '" + std::to_string(mod_ids[0]) + "'");
  else
    Log::info("Changing deployers for " + std::to_string(mod_ids.size()) + " mods");
  setStatusMessage("Updating mod deployers");
  setBusyStatus(true);
  emit updateModDeployers(currentApp(), mod_ids, deployers);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onCompletedOperations(QString message)
{
  setStatusMessage(message, 3000);
  if(!message.isEmpty())
    Log::info(message.toStdString());
  setBusyStatus(false);
}

void MainWindow::onGetFileConflicts(std::vector<ConflictInfo> conflicts)
{
  if(conflict_detail_mod_id_ >= 0)
  {
    const int mod_id = conflict_detail_mod_id_;
    const QString mod_name = conflict_detail_mod_name_;
    conflict_detail_mod_id_ = -1;
    ConflictDetailDialog dialog(mod_id, mod_name, conflicts, this);
    dialog.exec();
    return;
  }
  if(deployer_model_->rowCount() == 0)
    return;
  auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  conflicts_model_->setConflicts(conflicts,
                                 deployer_model_->data(index, ModListModel::mod_id_role).toInt());
  conflicts_list_->resizeColumnToContents(0);
  conflicts_list_->resizeColumnToContents(1);
  conflicts_list_->resizeColumnToContents(2);
  conflicts_window_->setWindowTitle(
    "File conflicts for \"" + deployer_model_->data(index, ModListModel::mod_name_role).toString() +
    "\"");
  conflicts_window_->show();
}

void MainWindow::onGetAppInfo(AppInfo app_info)
{
  ignore_tool_changes_ = true;
  app_info_ = app_info;

  num_mods_per_manual_tag_ = app_info.num_mods_per_manual_tag;
  ui->actionEdit_Tags_for_mods->setVisible(!num_mods_per_manual_tag_.empty());
  for(auto cb : manual_tag_cbs_)
    delete cb;
  manual_tag_cbs_.clear();
  const auto tag_filters = mod_list_proxy_->getTagFilters();
  for(const auto& [tag, num_mods] : app_info.num_mods_per_manual_tag)
  {
    auto cb = new TagCheckBox(tag.c_str(), num_mods);
    cb->setToolTip(QString("Filter for mods with tag '") + tag.c_str() + "'");
    auto iter = str::find_if(tag_filters, [tag](auto& pair) { return pair.first == tag.c_str(); });
    if(iter != str::end(tag_filters))
      cb->setCheckState(iter->second ? Qt::PartiallyChecked : Qt::Checked);
    connect(cb, &TagCheckBox::tagBoxChecked, this, &MainWindow::onModManualTagFilterChanged);
    manual_tag_cbs_.push_back(cb);
    ui->manual_tags_widget->layout()->addWidget(cb);
  }

  num_mods_per_auto_tag_ = app_info.num_mods_per_auto_tag;
  auto_tags_ = app_info.auto_tags;
  for(auto cb : auto_tag_cbs_)
    delete cb;
  auto_tag_cbs_.clear();
  for(const auto& [tag, num_mods] : app_info.num_mods_per_auto_tag)
  {
    auto cb = new TagCheckBox(tag.c_str(), num_mods);
    cb->setToolTip(QString("Filter for mods with tag '") + tag.c_str() + "'");
    auto iter = str::find_if(tag_filters, [tag](auto& pair) { return pair.first == tag.c_str(); });
    if(iter != str::end(tag_filters))
      cb->setCheckState(iter->second ? Qt::PartiallyChecked : Qt::Checked);
    connect(cb, &TagCheckBox::tagBoxChecked, this, &MainWindow::onModManualTagFilterChanged);
    auto_tag_cbs_.push_back(cb);
    ui->auto_tags_widget->layout()->addWidget(cb);
  }

  // app info
  ui->info_name_label->setText(app_info.name.c_str());
  ui->info_version_label->setText(app_info.app_version.c_str());
  ui->info_sdir_label->setText(app_info.staging_dir.c_str());
  ui->info_mods_label->setText(QString::number(app_info.num_mods));
  ui->info_command_label->setText(app_info.command.c_str());
  ui->info_deployer_list->setRowCount(0);
  deployer_source_paths_.clear();
  deployer_target_paths_.clear();
  for(int i = 0; i < app_info.deployers.size(); i++)
  {
    deployer_source_paths_.push_back(app_info.deployer_source_dirs[i].c_str());
    deployer_target_paths_.push_back(app_info.target_dirs[i].c_str());
    ui->info_deployer_list->setRowCount(i + 1);
    QPushButton* button = new QPushButton();
    button->setIcon(QIcon::fromTheme("editor"));
    button->setToolTip("Edit Deployer");
    button->adjustSize();
    connect(button, &QPushButton::clicked, this, &MainWindow::onEditDeployerPressed);
    ui->info_deployer_list->setCellWidget(i, 0, button);
    ui->info_deployer_list->setItem(i, 1, new QTableWidgetItem(app_info.deployers[i].c_str()));
    ui->info_deployer_list->setItem(i, 2, new QTableWidgetItem(app_info.deployer_types[i].c_str()));
    ui->info_deployer_list->setItem(
      i, 3, new QTableWidgetItem(QString::number(app_info.deployer_mods[i])));
    QString deploy_mode = deploy_mode_hard_link;
    if(app_info.deploy_modes[i] == Deployer::sym_link)
      deploy_mode = deploy_mode_sym_link;
    else if(app_info.deploy_modes[i] == Deployer::copy)
      deploy_mode = deploy_mode_copy;
    ui->info_deployer_list->setItem(i, 4, new QTableWidgetItem(deploy_mode));
    ui->info_deployer_list->setItem(i, 5, new QTableWidgetItem(app_info.target_dirs[i].c_str()));
  }
  ui->info_deployer_list->setColumnWidth(0, 50);
  ui->info_deployer_list->resizeColumnToContents(1);
  ui->info_deployer_list->resizeColumnToContents(2);
  ui->info_deployer_list->resizeColumnToContents(3);
  ui->info_deployer_list->resizeColumnToContents(4);
  ui->info_deployer_list->resizeColumnToContents(5);
  ui->info_deployer_list->horizontalHeaderItem(5)->setTextAlignment(Qt::AlignLeft);

  // tools
  tools_ = app_info.tools;
  ui->info_tool_list->setRowCount(0);
  ui->info_tool_list->setRowCount(app_info.tools.size() + 1);
  for(int i = 0; i < app_info.tools.size(); i++)
  {
    const QString name(app_info.tools[i].getName().c_str());

    auto remove_button = new TablePushButton(i, 0);
    remove_button->setIcon(QIcon::fromTheme("user-trash"));
    remove_button->setToolTip("Remove " + name);
    connect(
      remove_button, &TablePushButton::clickedAt, this, &MainWindow::onRemoveToolButtonPressed);
    ui->info_tool_list->setCellWidget(i, 0, remove_button);

    auto edit_button = new TablePushButton(i, 1);
    edit_button->setIcon(QIcon::fromTheme("editor"));
    edit_button->setToolTip("Edit " + name);
    connect(edit_button, &TablePushButton::clickedAt, this, &MainWindow::onEditToolButtonPressed);
    ui->info_tool_list->setCellWidget(i, 1, edit_button);

    auto launch_button = new TablePushButton(i, 2);
    launch_button->setText(name);
    const QString icon_path(app_info.tools[i].getIconPath().c_str());
    const QIcon icon(icon_path);
    launch_button->setIcon(icon_path.isEmpty() || icon.availableSizes().size() <= 0
                             ? QIcon::fromTheme("system-run")
                             : icon);
    launch_button->setToolTip("Launch " + name);
    connect(
      launch_button, &TablePushButton::clickedAt, this, &MainWindow::onLaunchToolButtonPressed);
    ui->info_tool_list->setCellWidget(i, 2, launch_button);
  }
  QPushButton* button = new QPushButton();
  button->setIcon(QIcon::fromTheme("list-add"));
  button->setToolTip("Add Tool");
  button->adjustSize();
  connect(button, &QPushButton::clicked, this, &MainWindow::onAddToolClicked);
  ui->info_tool_list->setCellWidget(ui->info_tool_list->rowCount() - 1, 0, button);
  ignore_tool_changes_ = false;

  // fork #24: keep the save-game manager pointed at the current app, suggesting its staging dir.
  if(save_manager_widget_)
    save_manager_widget_->setAppContext(currentApp(), app_info.staging_dir.c_str());

  initRootLevelConditions();
}

void MainWindow::onApplicationEdited(EditApplicationInfo info, int app_id)
{
  emit editApplication(info, app_id);
  emit getApplicationNames(false);
}

void MainWindow::onDeployerEdited(EditDeployerInfo info, int app_id, int deployer)
{
  emit editDeployer(info, app_id, deployer);
  if(currentApp() == app_id)
    emit getDeployerNames(app_id, false);
}

void MainWindow::onGetModConflicts(std::unordered_set<int> conflicts)
{
  deployer_list_proxy_->setConflicts(conflicts);
  deployer_list_proxy_->addFilter(DeployerListProxyModel::filter_conflicts);
  ui->reset_filter_button->setHidden(false);
}

void MainWindow::onModMoved()
{
  deployer_list_slider_pos_ = ui->deployer_list->verticalScrollBar()->sliderPosition();
  emit commitChanges(currentApp(), currentDeployer());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onGetProfileNames(QStringList names, bool is_new)
{
  if(names.size() == 0)
    edit_profile_action_->setEnabled(false);
  else
    edit_profile_action_->setEnabled(true);
  if(names.size() <= 1)
    remove_profile_action_->setEnabled(false);
  else
    remove_profile_action_->setEnabled(true);
  auto settings = QSettings(QCoreApplication::applicationName());
  settings.beginGroup(QString::number(currentApp()));
  int saved_index = settings.value("current_profile", -2).toInt();
  settings.endGroup();
  int index = currentProfile();
  bool block = ui->profile_selection_box->signalsBlocked();
  ui->profile_selection_box->blockSignals(true);
  ui->profile_selection_box->clear();
  ui->profile_selection_box->addItems(names);
  if(is_new)
    ui->profile_selection_box->setCurrentIndex(ui->profile_selection_box->count() - 1);
  else if(saved_index >= 0 && saved_index < ui->profile_selection_box->count())
    ui->profile_selection_box->setCurrentIndex(saved_index);
  else if(index < ui->profile_selection_box->count() && index >= 0)
    ui->profile_selection_box->setCurrentIndex(index);
  if(ui->profile_selection_box->count() == 1)
    remove_profile_action_->setEnabled(false);
  else if(ui->profile_selection_box->count() > 1)
    remove_profile_action_->setEnabled(true);
  ui->profile_selection_box->blockSignals(block);
  emit setProfile(currentApp(), currentProfile());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onProfileAdded(int app_id, EditProfileInfo info)
{
  Log::info("Adding profile '" + info.name + "'");
  emit addProfile(app_id, info);
  if(app_id == currentApp())
    emit getProfileNames(app_id, true);
}

void MainWindow::onProfileEdited(int app_id, int profile, EditProfileInfo info)
{
  emit editProfile(app_id, profile, info);
  if(app_id == currentApp())
    emit getProfileNames(app_id, false);
}

void MainWindow::onProfileRemoved()
{
  if(ui->profile_selection_box->count() > 1)
  {
    Log::info("Removing profile '" + ui->profile_selection_box->currentText().toStdString() + "'");
    emit removeProfile(currentApp(), currentProfile());
  }
  emit getProfileNames(currentApp(), false);
}

void MainWindow::onModAddedToGroup(int mod_id, int target_id)
{
  const auto& group_map = mod_list_model_->getGroupMap();
  Log::info(std::format("Adding mod to group"));
  setStatusMessage("Adding mod to group");
  setBusyStatus(true);
  if(group_map.contains(target_id))
    emit addModToGroup(currentApp(), mod_id, group_map.at(target_id));
  else
    emit createGroup(currentApp(), mod_id, target_id);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onAddModAborted(QString temp_dir)
{
  Log::info("Mod installation aborted");
  bool abort = true;
  if(!mod_import_queue_.empty())
  {
    QMessageBox box;
    if(mod_import_queue_.size() > 1)
    {
      box.setText(
        std::format("There are {} mod import actions pending. Do you want to cancel them?",
                    mod_import_queue_.size())
          .c_str());
      box.setWindowTitle("Additional Imports");
    }
    else
    {
      box.setText("There is an additional mod import action pending. Do you wish to cancel it?");
      box.setWindowTitle("Additional Import");
    }
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    int answer = box.exec();
    if(answer == QMessageBox::Yes)
      mod_import_queue_ = std::priority_queue<ImportModInfo>();
    else
      abort = false;
  }

  if(abort)
  {
    ui->mod_list->setAcceptDrops(true);
    ui->deployer_list->setAcceptDrops(true);
    setBusyStatus(false);
  }
  else
    importMod();
  try
  {
    if(std::filesystem::exists(temp_dir.toStdString()))
      std::filesystem::remove_all(temp_dir.toStdString());
  }
  catch(std::exception& e)
  {
    Log::error("Filesystem error: Failed to delete temporary extraction directory.");
  }
}

void MainWindow::onModMovedTo(int from, int to)
{
  emit changeLoadorder(currentApp(), currentDeployer(), from, to);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onExtractionComplete(ImportModInfo info)
{
  setBusyStatus(false);
  mod_import_queue_.pop();
  if(!info.last_action_was_successful)
  {
    if(!mod_import_queue_.empty())
      importMod();
    setStatusMessage("Import failed", 3000);
    Log::error("Failed to import mod \"" + info.local_source.string() + "\"");
    return;
  }
  setStatusMessage("Mod imported", 3000);
  Log::info("Mod imported");
  QStringList deployers;
  for(int i = 0; i < ui->deployer_selection_box->count(); i++)
    deployers << ui->deployer_selection_box->itemText(i);
  int deployer =
    ui->app_tab_widget->currentIndex() == 2 ? ui->deployer_selection_box->currentIndex() : -1;
  const std::vector<bool> auto_deployers = getAutonomousDeployers();
  QStringList deployer_paths;
  for(const auto& path : app_info_.target_dirs)
    deployer_paths.append(path.c_str());
  info.action_type = ImportModInfo::ActionType::install_dialog;
  bool was_successful = add_mod_dialog_->setupDialog(deployers,
                                                     deployer,
                                                     deployer_paths,
                                                     auto_deployers,
                                                     app_info_.deployer_is_case_invariant,
                                                     ui->info_version_label->text(),
                                                     info,
                                                     root_level_conditions_);
  if(was_successful)
  {
    setBusyStatus(true, false);
    add_mod_dialog_->show();
  }
  else
    onReceiveError("Error",
                   ("Failed to import mod from \"" + info.local_source.string() + "\"").c_str());
}

void MainWindow::onSettingsDialogComplete()
{
  ask_remove_from_deployer_ = settings_dialog_->askRemoveFromDeployer();
  ask_remove_mod_ = settings_dialog_->askRemoveMod();
  ask_remove_profile_ = settings_dialog_->askRemoveProfile();
  deploy_for_all_ = settings_dialog_->deployAll();
  show_log_on_error_ = settings_dialog_->logOnError();
  show_log_on_warning_ = settings_dialog_->logOnWarning();
  ask_remove_backup_target_ = settings_dialog_->askRemoveBackupTarget();
  ask_remove_backup_ = settings_dialog_->askRemoveBackup();
  ask_remove_tool_ = settings_dialog_->askRemoveTool();
  if(debug_mode_)
    Log::log_level = Log::LOG_DEBUG;
}

void MainWindow::onGetBackupInfo(std::vector<BackupTarget> backups)
{
  backup_list_model_->setBackupTargets(backups);
  ui->backup_list->setColumnWidth(BackupListModel::action_col, 55);
  ui->backup_list->resizeColumnToContents(BackupListModel::target_col);
  ui->backup_list->resizeColumnToContents(BackupListModel::backup_col);
  ui->backup_list->setColumnWidth(BackupListModel::backup_col,
                                  ui->backup_list->columnWidth(BackupListModel::backup_col) + 10);
}

void MainWindow::onBackupTargetAdded(int app_id,
                                     QString name,
                                     QString path,
                                     QString default_backup,
                                     QString first_backup)
{
  emit addBackupTarget(app_id, path, name, default_backup, first_backup);
  setStatusMessage("Adding backup target");
  setBusyStatus(true);
  if(app_id == currentApp())
    emit getBackupInfo(app_id);
}

void MainWindow::onBackupListContextMenu(QPoint pos)
{
  auto idx = ui->backup_list->indexAt(pos);
  // pos.setY(pos.y() + ui->backup_list->verticalHeader()->sizeHint().height() + 6);
  if(idx.row() >= 0 && idx.row() < backup_list_model_->rowCount() - 1)
  {
    const QString target_name = idx.data(BackupListModel::target_name_role).toString();
    ui->actionAdd_Backup->setToolTip("Add backup to '" + target_name + "'");
    ui->actionRemove_Backup->setToolTip("Remove backup from '" + target_name + "'");
    if(idx.data(BackupListModel::num_backups_role).toInt() < 2)
    {
      ui->actionRemove_Backup->setVisible(false);
      ui->actionOverwrite_Backup->setVisible(false);
    }
    else
    {
      ui->actionRemove_Backup->setVisible(true);
      ui->actionOverwrite_Backup->setVisible(true);
    }
    backup_list_menu_->exec(ui->backup_list->mapToGlobal(pos));
  }
}

void MainWindow::onBackupAdded(int app_id,
                               int target,
                               QString name,
                               QString target_name,
                               int source)
{
  Log::info(
    std::format("Adding backup '{}' to '{}'", name.toStdString(), target_name.toStdString()));
  setStatusMessage("Creating backup");
  setBusyStatus(true);
  emit addBackup(app_id, target, name, source);
  if(app_id == currentApp())
    emit getBackupInfo(app_id);
}

void MainWindow::resizeModListColumns()
{
  ui->mod_list->resizeColumnToContents(ModListModel::name_col);
  ui->mod_list->resizeColumnToContents(ModListModel::version_col);
  ui->mod_list->setColumnWidth(ModListModel::version_col,
                               ui->mod_list->columnWidth(ModListModel::version_col) + 10);
  ui->mod_list->resizeColumnToContents(ModListModel::time_col);
  ui->mod_list->resizeColumnToContents(ModListModel::size_col);
  ui->mod_list->resizeColumnToContents(ModListModel::deployers_col);
  ui->mod_list->resizeColumnToContents(ModListModel::tags_col);
}

void MainWindow::resizeDeployerListColumns()
{
  ui->deployer_list->resizeColumnToContents(DeployerListModel::name_col);
  ui->deployer_list->resizeColumnToContents(DeployerListModel::id_col);
}

void MainWindow::setupProgressBar()
{
  progress_bar_ = new QProgressBar();
  progress_bar_->setMaximum(0);
  progress_bar_->setMinimum(0);
  progress_bar_->setValue(0);
  auto container = new QWidget();
  auto layout = new QHBoxLayout();
  layout->insertSpacing(0, 375);
  layout->addWidget(progress_bar_);
  layout->setSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignCenter);
  container->setLayout(layout);
  container->setMaximumHeight(15);
  ui->statusbar->insertPermanentWidget(0, container);
  progress_bar_->setVisible(false);
}

void MainWindow::setupFilters()
{
  ui->mod_filter_scroll_area->setVisible(false);
  ui->deployer_filter_scroll_area->setVisible(false);

  ui->filter_group_mods_cb->setStyleSheet(TagCheckBox::style_sheet);
  ui->filter_active_mods_cb->setStyleSheet(TagCheckBox::style_sheet);
  ui->filter_mods_with_updates_cb->setStyleSheet(TagCheckBox::style_sheet);
  ui->filter_active_mods_depl_cb->setStyleSheet(TagCheckBox::style_sheet);

  auto manual_layout = new QVBoxLayout();
  auto margins = manual_layout->contentsMargins();
  margins.setLeft(0);
  manual_layout->setContentsMargins(margins);
  ui->manual_tags_widget->setLayout(manual_layout);
  auto auto_layout = new QVBoxLayout();
  auto_layout->setContentsMargins(margins);
  ui->auto_tags_widget->setLayout(auto_layout);
  auto deployer_layout = new QVBoxLayout();
  deployer_layout->setContentsMargins(margins);
  ui->deployer_tags_widget->setLayout(deployer_layout);
}

void MainWindow::setupIcons()
{
  QIcon edit_tag_icon = QIcon::fromTheme("tag-edit");
  if(edit_tag_icon.isNull())
    edit_tag_icon = QIcon::fromTheme("editor");
  ui->edit_manual_tags_button->setIcon(edit_tag_icon);
  ui->edit_auto_tags_button->setIcon(edit_tag_icon);
  ui->settings_button->setIcon(QIcon::fromTheme("configure"));
  ui->edit_app_button->setIcon(QIcon::fromTheme("editor"));
  ui->filters_button->setIcon(QIcon::fromTheme("view-filter"));
  ui->actionadd_to_deployer->setIcon(QIcon::fromTheme("editor"));
  ui->actionmove_mod->setIcon(QIcon::fromTheme("adjustrow"));
  ui->actionShow_Nexus_Page->setIcon(QIcon::fromTheme("globe"));
  ui->actionCheck_For_Updates->setIcon(QIcon::fromTheme("update-none"));
  ui->check_mod_updates_button->setIcon(QIcon::fromTheme("update-none"));
  ui->actionget_file_conflicts->setIcon(QIcon::fromTheme("document-duplicate"));
  ui->actionget_mod_conflicts->setIcon(QIcon::fromTheme("project_show"));
  ui->actionOverwrite_Backup->setIcon(QIcon::fromTheme("document-revert"));
  ui->actionReinstall_From_Local->setIcon(QIcon::fromTheme("document-revert"));
  ui->actionSuppress_Update->setIcon(QIcon::fromTheme("edit-clear-all"));
}

void MainWindow::on_deploy_button_clicked()
{
  setStatusMessage("Checking for external changes");
  setBusyStatus(true, true, true);
  if(deploy_for_all_)
    emit getExternalChanges(currentApp(), 0, true);
  else
    emit getExternalChanges(currentApp(), currentDeployer(), true);
}

void MainWindow::on_deployer_add_separator_button_clicked()
{
  deployer_model_->addSeparator();
  emit commitChanges(currentApp(), currentDeployer());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onAddAppButtonClicked()
{
  add_app_dialog_->setAddMode();
  setBusyStatus(true, false);
  add_app_dialog_->show();
}


void MainWindow::on_app_selection_box_currentIndexChanged(int index)
{
  mod_list_proxy_->removeFilter(ModListProxyModel::filter_tags);
  deployer_list_proxy_->removeFilter(DeployerListProxyModel::filter_tags);
  emit getDeployerNames(currentApp(), false);
  on_reset_filter_button_clicked();
}


void MainWindow::onAddDeployerButtonClicked()
{
  if(ui->app_selection_box->count() == 0)
    return;
  add_deployer_dialog_->setAddMode(currentApp());
  setBusyStatus(true, false);
  add_deployer_dialog_->show();
}


void MainWindow::on_deployer_selection_box_currentIndexChanged(int index)
{
  emit getDeployerInfo(currentApp(), index);
  auto settings = QSettings(QCoreApplication::applicationName());
  settings.beginGroup(QString::number(currentApp()));
  settings.setValue("current_deployer", index);
  on_reset_filter_button_clicked();
}

void MainWindow::onRemoveDeployerButtonClicked()
{
  if(currentDeployer() == -1)
    return;
  message_box_->setText("Are you sure you want to remove \"" +
                        ui->deployer_selection_box->currentText() + "\"?");
  QCheckBox* checkbox = message_box_->checkBox();
  checkbox->setChecked(true);
  checkbox->setHidden(false);
  checkbox->setText("Cleanup deployed mods");
  int answer = message_box_->exec();
  if(answer == QMessageBox::No)
    return;
  Log::info("Removing deployer '" + ui->deployer_selection_box->currentText().toStdString() + "'");
  auto settings = QSettings(QCoreApplication::applicationName());
  settings.beginGroup(QString::number(currentApp()));
  std::vector<int> selected_deployers;
  int size = settings.beginReadArray("selected_deployers");
  for(int i = 0; i < size; i++)
  {
    settings.setArrayIndex(i);
    int deployer = settings.value("selected").toInt();
    if(deployer != currentDeployer())
      selected_deployers.push_back(deployer);
  }
  settings.endArray();
  settings.beginWriteArray("selected_deployers");
  for(int i = 0; i < selected_deployers.size(); i++)
  {
    settings.setArrayIndex(i);
    settings.setValue("selected", selected_deployers[i]);
  }
  settings.endArray();
  settings.endGroup();
  emit removeDeployer(currentApp(), currentDeployer(), checkbox->checkState() == Qt::Checked);
  emit getDeployerNames(currentApp(), false);
}

void MainWindow::onRemoveAppButtonClicked()
{
  if(currentApp() == -1)
    return;
  message_box_->setText("Are you sure you want to remove \"" +
                        ui->app_selection_box->currentText() + "\"?");
  auto* check_box = message_box_->checkBox();
  check_box->setHidden(false);
  check_box->setChecked(Qt::Unchecked);
  check_box->setText("Delete all installed mods and backups");
  int answer = message_box_->exec();
  if(answer == QMessageBox::No)
    return;
  Log::info("Removing application '" + ui->app_selection_box->currentText().toStdString() + "'");
  QSettings settings{ "Limo" };
  settings.remove(QString::number(currentApp()));
  auto groups = settings.childGroups();
  for(int i = currentApp() + 1; i < ui->app_selection_box->count(); i++)
  {
    if(!groups.contains(QString::number(i)))
      continue;
    settings.beginGroup(QString::number(i));
    std::map<QString, QVariant> group;
    for(const auto& key : static_cast<const QStringList>(settings.allKeys()))
      group[key] = settings.value(key);
    settings.endGroup();
    settings.remove(QString::number(i));
    settings.beginGroup(QString::number(i - 1));
    for(const auto& [key, value] : group)
      settings.setValue(key, value);
    settings.endGroup();
  }
  emit removeApplication(currentApp(), check_box->checkState() == Qt::Checked);
  emit getApplicationNames(false);
}

void MainWindow::on_actionadd_to_deployer_triggered()
{
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;
  QStringList deployer_names;
  for(int i = 0; i < ui->deployer_selection_box->count(); i++)
    deployer_names.append(ui->deployer_selection_box->itemText(i));
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  auto mod_ids = ui->mod_list->getSelectedModIds();
  const auto deployer_ids =
    mod_list_model_->data(index, ModListModel::deployer_ids_role).value<std::vector<int>>();
  add_to_deployer_dialog_->setupDialog(
    deployer_names,
    mod_list_model_->data(index, ModListModel::mod_name_role).toString(),
    mod_ids,
    deployer_ids,
    getAutonomousDeployers());
  setBusyStatus(true, false);
  add_to_deployer_dialog_->show();
}

void MainWindow::on_actionremove_from_deployer_triggered()
{
  if(deployer_model_->rowCount() == 0)
    return;
  auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  if(ask_remove_from_deployer_)
  {
    message_box_->setText("Are you sure you want to remove \"" +
                          deployer_model_->data(index, ModListModel::mod_name_role).toString() +
                          "\"?");
    auto check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_from_deployer_ = check_box->checkState() != Qt::Checked;
    if(answer == QMessageBox::No)
      return;
  }
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;

  setStatusMessage("Removing mod from deployer");
  Log::info(
    std::format("Removing mod '{}' from deployer '{}'",
                deployer_model_->data(index, ModListModel::mod_name_role).toString().toStdString(),
                ui->deployer_selection_box->currentText().toStdString()));
  setBusyStatus(true);
  emit removeModFromDeployer(currentApp(),
                             currentDeployer(),
                             index.internalPointer());
                             // deployer_model_->data(index, ModListModel::mod_id_role).toInt());
  // emit deployer_list_proxy_->dataChanged(index, index);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionget_file_conflicts_triggered()
{
  if(deployer_model_->rowCount() == 0)
    return;
  setStatusMessage("Finding file conflicts");
  setBusyStatus(true);
  auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  emit getFileConflicts(currentApp(),
                        currentDeployer(),
                        deployer_model_->data(index, ModListModel::mod_id_role).toInt(),
                        false);
}

void MainWindow::onEditDeployerPressed()
{
  showEditDeployerDialog(ui->info_deployer_list->currentRow());
}

void MainWindow::onAddToolClicked()
{
  // Pass the current app's Steam App ID so the dialog can default the field (limo-app/limo#69).
  add_tool_dialog_->setAddMode(currentApp(), app_info_.steam_app_id);
  add_tool_dialog_->exec();
}

void MainWindow::onLaunchAppButtonClicked()
{
  auto name = ui->app_selection_box->currentText();
  auto command = ui->info_command_label->text();
  if(command.isEmpty())
  {
    Log::error(("Command for application '" + name + "' is empty").toStdString());
    QMessageBox::warning(
      this,
      "No launch command configured",
      "No launch command is configured for application '" + name +
        "'.\n\nUse the edit button to set a command before launching.");
    return;
  }
  runConcurrent(command, name, "Application");
}

void MainWindow::on_edit_app_button_clicked()
{
  add_app_dialog_->setEditMode(ui->info_name_label->text(),
                               ui->info_version_label->text(),
                               ui->info_sdir_label->text(),
                               ui->info_command_label->text(),
                               ui->app_selection_box->currentData(Qt::UserRole).toString(),
                               currentApp(),
                               app_info_.steam_app_id);
  setBusyStatus(true, false);
  add_app_dialog_->show();
}

void MainWindow::on_search_field_textEdited(const QString& text)
{
  search_term_ = text;
  if(text.isEmpty())
  {
    filterModList();
    filterDeployerList();
    resizeModListColumns();
    resizeDeployerListColumns();
    emit scrollLists();
    return;
  }
  filterDeployerList();
  filterModList();
}

void MainWindow::on_actionget_mod_conflicts_triggered()
{
  if(deployer_model_->rowCount() == 0)
    return;
  setBusyStatus(true);
  setStatusMessage("Finding conflicts");
  auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  emit getModConflicts(currentApp(),
                       currentDeployer(),
                       deployer_model_->data(index, ModListModel::mod_id_role).toInt());
}


void MainWindow::on_reset_filter_button_clicked()
{
  deployer_list_proxy_->removeFilter(DeployerListProxyModel::filter_conflicts);
  resizeDeployerListColumns();
  ui->reset_filter_button->setHidden(true);
  emit scrollLists();
}

void MainWindow::on_actionmove_mod_triggered()
{
  if(deployer_model_->rowCount() == 0)
    return;
  auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  MoveModDialog dialog =
    MoveModDialog(deployer_model_->data(index, ModListModel::mod_name_role).toString(),
                  index.row(),
                  deployer_model_->rowCount());
  connect(&dialog, &MoveModDialog::modMovedTo, this, &MainWindow::onModMovedTo);
  dialog.exec();
}

void MainWindow::onEditDeployerMenuClicked()
{
  showEditDeployerDialog(currentDeployer());
}

// fork #53: run deployment integrity verification for the current deployer and show the result.
void MainWindow::onVerifyDeployerMenuClicked()
{
  const int deployer = currentDeployer();
  if(deployer < 0 || deployer >= static_cast<int>(deployer_source_paths_.size()) ||
     deployer >= static_cast<int>(deployer_target_paths_.size()))
    return;

  const QString name =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Name"))->text();
  const QString deploy_mode_string =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Mode"))->text();
  Deployer::DeployMode deploy_mode = Deployer::hard_link;
  if(deploy_mode_string == deploy_mode_sym_link)
    deploy_mode = Deployer::sym_link;
  else if(deploy_mode_string == deploy_mode_copy)
    deploy_mode = Deployer::copy;

  // verifyDeployment is read-only (no disk writes), so it is safe to run on a transient Deployer
  // built from the displayed source/target paths and deploy mode.
  try
  {
    Deployer verifier(deployer_source_paths_[deployer].toStdString(),
                      deployer_target_paths_[deployer].toStdString(),
                      name.toStdString(),
                      deploy_mode);
    const Deployer::VerificationResult result = verifier.verifyDeployment();
    DeployVerifyDialog dialog(name, result, this);
    dialog.exec();
  }
  catch(const std::exception& error)
  {
    onReceiveError("Error", QString("Could not verify deployment: ") + error.what());
  }
}

// fork #11: show the deployed file tree with per-file mod origin for the current deployer.
// Mirrors onVerifyDeployerMenuClicked: builds a transient Deployer from the displayed
// source/target paths and deploy mode. getDeployedFileOrigins performs no disk writes.
void MainWindow::onDeployedFilesTreeMenuClicked()
{
  const int deployer = currentDeployer();
  if(deployer < 0 || deployer >= static_cast<int>(deployer_source_paths_.size()) ||
     deployer >= static_cast<int>(deployer_target_paths_.size()))
    return;

  const QString name =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Name"))->text();
  const QString deploy_mode_string =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Mode"))->text();
  Deployer::DeployMode deploy_mode = Deployer::hard_link;
  if(deploy_mode_string == deploy_mode_sym_link)
    deploy_mode = Deployer::sym_link;
  else if(deploy_mode_string == deploy_mode_copy)
    deploy_mode = Deployer::copy;

  // Build an id -> name map from the currently displayed deployer mod list so the tree can show
  // human readable origins. Falls back to the bare id when no name is available.
  std::map<int, QString> mod_names;
  for(int row = 0; row < deployer_model_->rowCount(); row++)
  {
    const QVariant id_value = deployer_model_->data(
      deployer_model_->index(row, DeployerListModel::id_col, QModelIndex()));
    bool ok = false;
    const int id = id_value.toInt(&ok);
    if(!ok)
      continue;
    mod_names[id] = deployer_model_
                      ->data(deployer_model_->index(row, DeployerListModel::name_col, QModelIndex()))
                      .toString();
  }

  try
  {
    Deployer reader(deployer_source_paths_[deployer].toStdString(),
                    deployer_target_paths_[deployer].toStdString(),
                    name.toStdString(),
                    deploy_mode);
    const std::vector<Deployer::FileOrigin> origins = reader.getDeployedFileOrigins(true);
    DeployedFilesTreeDialog dialog(name, origins, mod_names, this);
    dialog.exec();
  }
  catch(const std::exception& error)
  {
    onReceiveError("Error", QString("Could not list deployed files: ") + error.what());
  }
}

// fork #50: run a read-only health check for the current deployer and show the aggregated problems.
// Mirrors onVerifyDeployerMenuClicked: builds a transient Deployer from the displayed
// source/target paths and deploy mode. runHealthCheck performs no disk writes.
void MainWindow::onHealthCheckDeployerMenuClicked()
{
  const int deployer = currentDeployer();
  if(deployer < 0 || deployer >= static_cast<int>(deployer_source_paths_.size()) ||
     deployer >= static_cast<int>(deployer_target_paths_.size()))
    return;

  const QString name =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Name"))->text();
  const QString deploy_mode_string =
    ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Mode"))->text();
  Deployer::DeployMode deploy_mode = Deployer::hard_link;
  if(deploy_mode_string == deploy_mode_sym_link)
    deploy_mode = Deployer::sym_link;
  else if(deploy_mode_string == deploy_mode_copy)
    deploy_mode = Deployer::copy;

  try
  {
    Deployer checker(deployer_source_paths_[deployer].toStdString(),
                     deployer_target_paths_[deployer].toStdString(),
                     name.toStdString(),
                     deploy_mode);
    const Deployer::HealthCheckResult result = checker.runHealthCheck();
    HealthCheckDialog dialog(result, this);
    dialog.exec();
  }
  catch(const std::exception& error)
  {
    onReceiveError("Error", QString("Could not run health check: ") + error.what());
  }
}

// fork #31: opens the LOOT user-metadata editor for the current LOOT deployer.
// The actual deployers live on a worker thread (ApplicationManager) and the GUI
// thread has no access to their source/target paths, so we ask the user for the
// two directories the LootDeployer needs: the plugin source directory and the
// target directory that holds plugins.txt/loadorder.txt and userlist.yaml.
#ifdef LIMO_WITH_LOOT
void MainWindow::onEditLootUserlistMenuClicked()
{
  const QString source_dir = QFileDialog::getExistingDirectory(
    this, "Select the LOOT deployer's plugin source directory");
  if(source_dir.isEmpty())
    return;
  const QString dest_dir = QFileDialog::getExistingDirectory(
    this, "Select the LOOT deployer's target directory (contains userlist.yaml)");
  if(dest_dir.isEmpty())
    return;
  try
  {
    LootUserlistDialog dialog(source_dir.toStdString(), dest_dir.toStdString(), this);
    dialog.exec();
  }
  catch(const std::exception& e)
  {
    QMessageBox::critical(
      this, "Error", QString("Could not open the LOOT user metadata editor:\n") + e.what());
  }
}
#endif

// fork #149
void MainWindow::onBisectDeployerMenuClicked()
{
  std::vector<LoadOrderBisectDialog::Entry> entries;
  const int rows = deployer_model_->rowCount();
  entries.reserve(rows);
  for(int row = 0; row < rows; row++)
  {
    const QModelIndex idx =
      deployer_model_->index(row, DeployerListModel::name_col, QModelIndex());
    const QString name = deployer_model_->data(idx, Qt::DisplayRole).toString();
    const bool enabled =
      deployer_model_->data(idx, DeployerListModel::mod_status_role).toBool();
    entries.push_back({ name.toStdString(), enabled });
  }
  auto* dialog = new LoadOrderBisectDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setEntries(entries);
  dialog->show();
}

void MainWindow::on_profile_selection_box_currentIndexChanged(int index)
{
  auto settings = QSettings(QCoreApplication::applicationName());
  settings.beginGroup(QString::number(currentApp()));
  settings.setValue("current_profile", index);
  settings.endGroup();
  emit setProfile(currentApp(), index);
  emit getDeployerInfo(currentApp(), currentDeployer());
  emit getBackupInfo(currentApp());
  on_reset_filter_button_clicked();
}

void MainWindow::onAddProfileButtonClicked()
{
  QStringList names;
  for(int i = 0; i < ui->profile_selection_box->count(); i++)
    names << ui->profile_selection_box->itemText(i);
  add_profile_dialog_->setAddMode(currentApp(), names);
  setBusyStatus(true, false);
  add_profile_dialog_->show();
}

void MainWindow::onEditProfileButtonClicked()
{
  add_profile_dialog_->setEditMode(currentApp(),
                                   currentProfile(),
                                   ui->profile_selection_box->currentText(),
                                   ui->info_version_label->text());
  setBusyStatus(true, false);
  add_profile_dialog_->show();
}

void MainWindow::onRemoveProfileButtonClicked()
{
  if(ui->profile_selection_box->count() < 2)
    return;
  if(ask_remove_profile_)
  {
    message_box_->setText("Are you sure you want to remove \"" +
                          ui->profile_selection_box->currentText() + "\"?");
    message_box_->checkBox()->setHidden(true);
    auto check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_profile_ = !check_box->isChecked();
    if(answer == QMessageBox::No)
      return;
  }
  if(ui->profile_selection_box->count() == 2)
    remove_profile_action_->setEnabled(false);
  emit removeProfile(currentApp(), currentProfile());
  emit getProfileNames(currentApp(), false);
}

void MainWindow::onReceiveError(QString title, QString message)
{
  Log::error(message.toStdString());
  QMessageBox error_box(QMessageBox::Critical, title, message, QMessageBox::Ok);
  error_box.exec();
}

void MainWindow::on_actionAdd_to_Group_triggered()
{
  QStringList mod_names;
  std::vector<int> mod_ids;
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  const auto mods = mod_list_model_->getModInfo();
  for(int i = 0; i < mods.size(); i++)
  {
    Mod mod = mods[i].mod;
    if(mod.id != mod_id)
    {
      mod_names << (mod.name + " [" + std::to_string(mod.id) + "]").c_str();
      mod_ids.push_back(mod.id);
    }
  }
  add_to_group_dialog_->setupDialog(
    mod_names,
    mod_ids,
    mod_list_model_->data(index, ModListModel::mod_name_role).toString(),
    mod_id);
  setBusyStatus(true, false);
  add_to_group_dialog_->show();
}

void MainWindow::on_actionRemove_from_Group_triggered()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  setStatusMessage("Removing mod from group");
  Log::info("Removing '" +
            mod_list_model_->data(index, ModListModel::mod_name_role).toString().toStdString() +
            "' from its group");
  setBusyStatus(true);
  emit removeModFromGroup(currentApp(),
                          mod_list_model_->data(index, ModListModel::mod_id_role).toInt());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionbrowse_mod_files_triggered()
{
  QString cur_tab = ui->app_tab_widget->tabText(ui->app_tab_widget->currentIndex());
  int mod_id;
  if(cur_tab == "Mods")
  {
    const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
    mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  }
  else if(cur_tab == "Deployers")
  {
    const auto index =
      deployer_list_proxy_->mapToSource(ui->deployer_list->selectionModel()->currentIndex());
    mod_id = deployer_model_->data(index, ModListModel::mod_id_role).toInt();
  }
  else
    return;
  const QString path = ui->info_sdir_label->text() + "/" + QString::number(mod_id);
  if(!sfs::exists(path.toStdString()))
  {
    Log::error(("Could not browse files: '" + path + "' does not exist").toStdString());
    return;
  }
  if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
    Log::error(("Could not open '" + path +
                "' in a file manager. Check that a file manager is installed and (on Flatpak) that "
                "Limo is allowed to open files.")
                 .toStdString());
}

void MainWindow::on_actionbrowse_deployer_files_triggered()
{
  QString target_dir = "";
  for(int i = 0; i < ui->info_deployer_list->rowCount(); i++)
  {
    if(ui->info_deployer_list->item(i, getColumnIndex(ui->info_deployer_list, "Name"))->text() ==
       ui->deployer_selection_box->currentText())
      target_dir =
        ui->info_deployer_list->item(i, getColumnIndex(ui->info_deployer_list, "Target"))->text();
  }
  if(target_dir == "")
    return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(target_dir));
}

void MainWindow::on_actionSort_Mods_triggered()
{
  setStatusMessage("Sorting mods");
  Log::info("Sorting mods");
  setBusyStatus(true);
  emit sortModsByConflicts(currentApp(), currentDeployer());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onLogButtonPressed()
{
  ui->log_container->setVisible(!ui->log_container->isVisible());
}

void MainWindow::onReceiveLogMessage(Log::LogLevel log_level, QString message)
{
  Log::log(log_level, message.toStdString());
}

void MainWindow::onModVersionEdited(int mod_id, QString version)
{
  emit changeModVersion(currentApp(), mod_id, version);
  emit getModInfo(currentApp());
}

void MainWindow::onActiveGroupMemberChanged(int group, int mod_id)
{
  setStatusMessage("Changing active group member");
  Log::info("Changing active group member");
  setBusyStatus(true);
  emit changeActiveGroupMember(currentApp(), group, mod_id);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onModNameChanged(int mod_id, QString name)
{
  emit changeModName(currentApp(), mod_id, name);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onModRemoved(int mod_id, QString name)
{
  if(ask_remove_mod_)
  {
    message_box_->setText("Are you sure you want to remove \"" + name + "\"?");
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_mod_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  int app_id = currentApp();
  Log::info("Removing mod '" + name.toStdString() + "'");
  setBusyStatus(true);
  setStatusMessage("Removing '" + name + "'");
  emit uninstallMods(app_id, { mod_id }, "");
  emit getDeployerInfo(app_id, currentDeployer());
}

void MainWindow::on_settings_button_clicked()
{
  settings_dialog_->init();
  settings_dialog_->show();
}

void MainWindow::onAddBackupTargetClicked()
{
  add_backup_target_dialog_->resetDialog(currentApp());
  setBusyStatus(true, false);
  add_backup_target_dialog_->show();
}

void MainWindow::onActiveBackupChanged(int target, int backup)
{
  emit setActiveBackup(currentApp(), target, backup);
  emit getBackupInfo(currentApp());
}

void MainWindow::onBackupNameEdited(int target, int backup, QString name)
{
  emit setBackupName(currentApp(), target, backup, name);
  emit getBackupInfo(currentApp());
}

void MainWindow::onBackupTargetRemoveClicked(int target, QString name)
{
  if(ask_remove_backup_target_)
  {
    message_box_->setText("Are you sure you want to remove \"" + name +
                          "\"? This will delete all backups except for the currently active one.");
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_backup_target_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  Log::info("Removing backup target '" + name.toStdString() + "'");
  setStatusMessage("Removing backup target '" + name + "'");
  setBusyStatus(true);
  emit removeBackupTarget(currentApp(), target);
  emit getBackupInfo(currentApp());
}

void MainWindow::onBackupTargetNameEdited(int target, QString name)
{
  emit setBackupTargetName(currentApp(), target, name);
  emit getBackupInfo(currentApp());
}

void MainWindow::on_actionAdd_Backup_triggered()
{
  auto index = ui->backup_list->currentIndex();
  add_backup_dialog_->setupDialog(currentApp(),
                                  index.row(),
                                  index.data(BackupListModel::target_name_role).toString(),
                                  index.data(BackupListModel::backup_list_role).toStringList());
  setBusyStatus(true, false);
  add_backup_dialog_->show();
}

void MainWindow::on_actionRemove_Backup_triggered()
{
  auto index = ui->backup_list->currentIndex();
  const int target_id = index.row();
  const int backup_id = index.data(BackupListModel::active_index_role).toInt();
  const QString target_name = index.data(BackupListModel::target_name_role).toString();
  const QString backup_name = index.data(BackupListModel::backup_name_role).toString();
  if(ask_remove_backup_)
  {
    message_box_->setText(
      QString("Are you sure you want to remove \"%1\" from \"%2\"?").arg(backup_name, target_name));
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_backup_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  const int app_id = currentApp();
  Log::info(std::format(
    "Removing backup '{}' from '{}'", backup_name.toStdString(), target_name.toStdString()));
  emit removeBackup(app_id, target_id, backup_id);
  emit getBackupInfo(app_id);
}


void MainWindow::on_app_tab_widget_currentChanged(int index)
{
  ui->actionbrowse_mod_files->setVisible(
    !(index == deployer_tab_idx && !ui->actionremove_from_deployer->isVisible()));
  //  ui->filters_button->setHidden(index == backup_tab_idx || index == app_tab_idx);
}


void MainWindow::on_actionBrowse_backup_files_triggered()
{
  std::filesystem::path path(ui->backup_list->currentIndex()
                               .data(BackupListModel::target_path_role)
                               .toString()
                               .toStdString());
  if(!std::filesystem::exists(path))
    return;
  if(!std::filesystem::is_directory(path))
    path = path.parent_path();
  QDesktopServices::openUrl(QUrl::fromLocalFile(path.c_str()));
}


void MainWindow::on_actionOverwrite_Backup_triggered()
{
  auto index = ui->backup_list->currentIndex();
  overwrite_backup_dialog_->setupDialog(
    index.data(BackupListModel::backup_list_role).toStringList(),
    index.row(),
    index.data(BackupListModel::active_index_role).toInt());
  overwrite_backup_dialog_->exec();
}

void MainWindow::onBackupOverwritten(int target_id, int source_backup, int dest_backup)
{
  const auto name =
    ui->backup_list->currentIndex().data(BackupListModel::backup_name_role).toString();
  Log::info("Overwriting backup '" + name.toStdString() + "'");
  setStatusMessage("Overwriting backup '" + name + "'");
  setBusyStatus(true);
  emit overwriteBackup(currentApp(), target_id, source_backup, dest_backup);
}

void MainWindow::onScrollLists()
{
  ui->mod_list->scrollTo(ui->mod_list->selectionModel()->currentIndex(),
                         QAbstractItemView::PositionAtCenter);
  ui->deployer_list->scrollTo(ui->deployer_list->selectionModel()->currentIndex(),
                              QAbstractItemView::PositionAtCenter);
}

void MainWindow::updateProgress(float progress)
{
  if(!received_progress_)
  {
    const auto now = std::chrono::high_resolution_clock::now();
    const long duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_update_time_)
        .count();
    if(duration_ms > 100)
      last_progress_update_time_ = now;
    received_progress_ = true;
  }
  progress_bar_->setMaximum(100);
  progress_bar_->setMinimum(0);
  progress_bar_->setValue(static_cast<int>(progress * 100));
  if(progress - last_progress_ >= 0.01f)
  {
    const auto now = std::chrono::high_resolution_clock::now();
    const long msecs_elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_update_time_)
        .count();
    const int remaining_sec =
      (static_cast<double>(msecs_elapsed) * static_cast<double>((1.0 - progress) / progress)) /
      1000.0;
    const int hours = remaining_sec / 3600;
    const int minutes = (remaining_sec / 60) % 60;
    const int seconds = remaining_sec % 60;
    QString duration_str = "";
    if(hours > 0)
      duration_str.append(QString::number(hours) + "h ");
    if(minutes > 0 || hours > 0)
      duration_str.append(QString::number(minutes) + "m ");
    if(remaining_sec > 0)
    {
      duration_str.append(QString::number(seconds) + "s");
      progress_bar_->setFormat("%p% - " + duration_str);
    }
    else
      progress_bar_->resetFormat();
    last_progress_ = progress;
  }
}

void MainWindow::on_actionRemove_Mods_triggered()
{
  const auto mod_ids = ui->mod_list->getSelectedModIds();
  if(ask_remove_mod_)
  {
    message_box_->setText(
      std::format("Are you sure you want to remove {} mods?", mod_ids.size()).c_str());
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_mod_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  int app_id = currentApp();
  const auto message = std::format("Removing {} mods", mod_ids.size());
  Log::info(message);
  setBusyStatus(true);
  setStatusMessage(message.c_str());
  emit uninstallMods(app_id, mod_ids, "");
  emit getDeployerInfo(app_id, currentDeployer());
}

void MainWindow::on_actionRemove_Other_Versions_triggered()
{
  const auto mod_ids = ui->mod_list->getSelectedModIds();
  QString message_suffix;
  if(ask_remove_mod_)
  {
    QString message = "Are you sure you want to remove all group members for ";
    if(mod_ids.size() > 1)
      message_suffix = QString::number(mod_ids.size()) + " mods";
    else
    {
      const auto name = ui->mod_list->currentIndex().data(ModListModel::mod_name_role).toString();
      message_suffix = "'" + name + "'";
    }
    message_box_->setText(message + message_suffix + "?");
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_mod_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  int app_id = currentApp();
  const auto message = std::string("Removing group members for ") + message_suffix.toStdString();
  Log::info(message);
  setBusyStatus(true);
  setStatusMessage(message.c_str());
  emit uninstallGroupMembers(app_id, mod_ids);
  emit getDeployerInfo(app_id, currentDeployer());
}

void MainWindow::onAddAppDialogFinished(int return_code)
{
  if(return_code == QDialog::Accepted || ui->app_selection_box->count() > 0)
    setBusyStatus(false);
}

void MainWindow::onAddDeployerDialogFinished(int return_code)
{
  setBusyStatus(false);
}

void MainWindow::onAddBackupTargetDialogFinished(int return_code)
{
  setBusyStatus(false);
}

void MainWindow::onBusyDialogAborted()
{
  setBusyStatus(false);
}

void MainWindow::onAddProfileDialogFinished(int return_code)
{
  setBusyStatus(false);
}

void MainWindow::on_filters_button_clicked()
{
  const bool visible =
    ui->mod_filter_scroll_area->isVisible() | ui->deployer_filter_scroll_area->isVisible();
  ui->mod_filter_scroll_area->setVisible(!visible);
  ui->deployer_filter_scroll_area->setVisible(!visible);
  if(!visible)
  {
    ui->splitter_2->setSizes({ 500, 125 });
    ui->deployer_filter_splitter->setSizes({ 500, 125 });
  }
}

void MainWindow::on_filter_active_mods_cb_stateChanged(int state)
{
  if(state == Qt::CheckState::Checked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_inactive);
  else if(state == Qt::CheckState::PartiallyChecked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_active);
  else
  {
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_inactive, false);
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_active, true);
  }
}

void MainWindow::on_filter_group_mods_cb_stateChanged(int state)
{
  if(state == Qt::CheckState::Checked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_no_groups);
  else if(state == Qt::CheckState::PartiallyChecked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_groups);
  else
  {
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_no_groups, false);
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_groups, true);
  }
}

void MainWindow::on_filter_active_mods_depl_cb_stateChanged(int state)
{
  if(state == Qt::CheckState::Checked)
    deployer_list_proxy_->addFilter(DeployerListProxyModel::filter_inactive);
  else if(state == Qt::CheckState::PartiallyChecked)
    deployer_list_proxy_->addFilter(DeployerListProxyModel::filter_active);
  else
  {
    deployer_list_proxy_->removeFilter(DeployerListProxyModel::filter_inactive, false);
    deployer_list_proxy_->removeFilter(DeployerListProxyModel::filter_active, true);
  }
}

void MainWindow::on_edit_manual_tags_button_clicked()
{
  QStringList tag_names;
  std::vector<int> num_mods_per_tag;
  for(const auto& [name, _] : num_mods_per_manual_tag_)
    tag_names.append(name.c_str());
  tag_names.sort(Qt::CaseInsensitive);
  for(const auto& tag : tag_names)
    num_mods_per_tag.push_back(num_mods_per_manual_tag_[tag.toStdString()]);
  edit_manual_tags_dialog_->setupDialog(currentApp(), tag_names, num_mods_per_tag);
  setBusyStatus(true, false);
  edit_manual_tags_dialog_->show();
}

void MainWindow::onManualTagsEdited(int app_id, std::vector<EditManualTagAction> actions)
{
  setBusyStatus(false);
  emit editManualTags(app_id, actions);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onManualModTagsUpdated(int app_id,
                                        QStringList tags,
                                        std::vector<int> mod_ids,
                                        int mode)
{
  setBusyStatus(false);
  if(mode == ManageModTagsDialog::add_mode)
    emit addTagsToMods(app_id, tags, mod_ids);
  else if(mode == ManageModTagsDialog::remove_mode)
    emit removeTagsFromMods(app_id, tags, mod_ids);
  else
    emit setTagsForMods(app_id, tags, mod_ids);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionEdit_Tags_for_mods_triggered()
{
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;

  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  const QString mod_name = index.data(ModListModel::mod_name_role).toString();
  const QStringList mod_tags = index.data(ModListModel::manual_tags_role).toStringList();
  QStringList tags;
  for(const auto& [name, _] : num_mods_per_manual_tag_)
    tags.append(name.c_str());

  manage_mod_tags_dialog_->setupDialog(
    currentApp(), tags, mod_tags, mod_name, ui->mod_list->getSelectedModIds());
  setBusyStatus(true, false);
  manage_mod_tags_dialog_->show();
}

void MainWindow::setBulkModStatus(bool status)
{
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;

  const auto indices = ui->mod_list->getSelectedRowIndices();
  if(indices.empty())
    return;

  const int app_id = currentApp();
  for(const auto& index : indices)
  {
    const int mod_id = index.data(ModListModel::mod_id_role).toInt();
    const auto deployer_ids =
      index.data(ModListModel::deployer_ids_role).value<std::vector<int>>();
    for(int deployer : deployer_ids)
      emit setModStatus(app_id, deployer, mod_id, status);
  }
  emit getDeployerInfo(app_id, currentDeployer());
}

void MainWindow::on_actionBulk_Enable_triggered()
{
  setBulkModStatus(true);
}

void MainWindow::on_actionBulk_Disable_triggered()
{
  setBulkModStatus(false);
}

void MainWindow::on_actionBulk_Add_Tag_triggered()
{
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;

  const auto mod_ids = ui->mod_list->getSelectedModIds();
  if(mod_ids.empty())
    return;

  bool ok = false;
  const QString tag =
    QInputDialog::getText(this, "Add Tag", "Tag name:", QLineEdit::Normal, "", &ok);
  if(!ok || tag.isEmpty())
    return;

  emit addTagsToMods(currentApp(), QStringList{ tag }, mod_ids);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionBulk_Remove_Tag_triggered()
{
  if(ui->app_selection_box->count() == 0 || ui->deployer_selection_box->count() == 0)
    return;

  const auto mod_ids = ui->mod_list->getSelectedModIds();
  if(mod_ids.empty())
    return;

  QStringList existing_tags;
  for(const auto& [name, _] : num_mods_per_manual_tag_)
    existing_tags.append(name.c_str());

  bool ok = false;
  QString tag;
  if(existing_tags.empty())
    tag = QInputDialog::getText(this, "Remove Tag", "Tag name:", QLineEdit::Normal, "", &ok);
  else
    tag = QInputDialog::getItem(
      this, "Remove Tag", "Tag name:", existing_tags, 0, true, &ok);
  if(!ok || tag.isEmpty())
    return;

  emit removeTagsFromMods(currentApp(), QStringList{ tag }, mod_ids);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onModManualTagFilterChanged(QString tag, int state)
{
  if(state == Qt::Unchecked)
    mod_list_proxy_->removeTagFilter(tag, true);
  else
  {
    mod_list_proxy_->addFilter(ModListProxyModel::filter_tags, false);
    mod_list_proxy_->addTagFilter(tag, state == Qt::PartiallyChecked, true);
  }
}

void MainWindow::onDeplTagFilterChanged(QString tag, int state)
{
  if(state == Qt::Unchecked)
    deployer_list_proxy_->removeTagFilter(tag, true);
  else
  {
    deployer_list_proxy_->addFilter(DeployerListProxyModel::filter_tags, false);
    deployer_list_proxy_->addTagFilter(tag, state == Qt::PartiallyChecked, true);
  }
}

void MainWindow::on_edit_auto_tags_button_clicked()
{
  edit_auto_tags_dialog_->setupDialog(currentApp(), auto_tags_);
  setBusyStatus(true, false);
  edit_auto_tags_dialog_->show();
}

void MainWindow::onAutoTagsEdited(int app_id, std::vector<EditAutoTagAction> actions)
{
  setStatusMessage("Updating auto tags");
  setBusyStatus(true);
  emit editAutoTags(app_id, actions);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_update_auto_tags_button_clicked()
{
  setStatusMessage("Updating auto tags");
  setBusyStatus(true);
  emit reapplyAutoTags(currentApp());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionUpdate_Tags_triggered()
{
  setStatusMessage("Updating auto tags");
  setBusyStatus(true);
  emit updateAutoTags(currentApp(), ui->mod_list->getSelectedModIds());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionEdit_Mod_Sources_triggered()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  const QString mod_name = index.data(ModListModel::mod_name_role).toString();
  const QString local_source = index.data(ModListModel::local_source_role).toString();
  const QString remote_source = index.data(ModListModel::remote_source_role).toString();
  edit_mod_sources_dialog_->setupDialog(
    currentApp(), mod_id, mod_name, local_source, remote_source);
  setBusyStatus(true, false);
  edit_mod_sources_dialog_->show();
}

void MainWindow::onModSourcesEdited(int app_id,
                                    int mod_id,
                                    QString local_source,
                                    QString remote_source)
{
  setBusyStatus(false);
  emit editModSources(app_id, mod_id, local_source, remote_source);
  if(app_id == currentApp())
    emit getModInfo(app_id);
}

void MainWindow::onEditModNote()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  if(!index.isValid())
    return;
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  const QString mod_name = index.data(ModListModel::mod_name_role).toString();
  const QString current_note = index.data(ModListModel::mod_note_role).toString();
  bool ok = false;
  const QString note = QInputDialog::getMultiLineText(
    this, "Edit note for \"" + mod_name + "\"", "Note:", current_note, &ok);
  if(!ok)
    return;
  emit setModNote(currentApp(), mod_id, note);
  emit getModInfo(currentApp());
}

void MainWindow::onPinModVersion()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  if(!index.isValid())
    return;
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  emit setModPinned(currentApp(), mod_id, true);
  emit getModInfo(currentApp());
}

void MainWindow::onUnpinModVersion()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  if(!index.isValid())
    return;
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  emit setModPinned(currentApp(), mod_id, false);
  emit getModInfo(currentApp());
}

void MainWindow::onSetModColor()
{
  // fork #199: apply a chosen colour to every selected mod (falls back to the current row).
  auto mod_ids = ui->mod_list->getSelectedModIds();
  if(mod_ids.empty())
  {
    const auto index =
      mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
    if(!index.isValid())
      return;
    mod_ids.push_back(mod_list_model_->data(index, ModListModel::mod_id_role).toInt());
  }
  const QColor color = QColorDialog::getColor(Qt::white, this, "Select highlight colour");
  if(!color.isValid())
    return;
  for(int mod_id : mod_ids)
    emit setModColor(currentApp(), mod_id, color.name());
  emit getModInfo(currentApp());
}

void MainWindow::onClearModColor()
{
  // fork #199: clear the colour of every selected mod (falls back to the current row).
  auto mod_ids = ui->mod_list->getSelectedModIds();
  if(mod_ids.empty())
  {
    const auto index =
      mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
    if(!index.isValid())
      return;
    mod_ids.push_back(mod_list_model_->data(index, ModListModel::mod_id_role).toInt());
  }
  for(int mod_id : mod_ids)
    emit setModColor(currentApp(), mod_id, "");
  emit getModInfo(currentApp());
}

void MainWindow::onEditModConfig()
{
  // fork #200: open the per-mod config editor on the selected mod's staging directory.
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  if(!index.isValid())
    return;
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  const QString mod_name = index.data(ModListModel::mod_name_role).toString();
  const QString path = ui->info_sdir_label->text() + "/" + QString::number(mod_id);
  if(!sfs::exists(path.toStdString()))
  {
    Log::error(("Could not edit config: '" + path + "' does not exist").toStdString());
    return;
  }
  auto* dialog = new ModConfigEditorDialog(path, mod_name, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  // A saved config changes staging, so the next deploy must refresh the deployed copy.
  connect(dialog, &ModConfigEditorDialog::configSaved, this,
          [this]() { emit getDeployerInfo(currentApp(), currentDeployer()); });
  dialog->show();
}

void MainWindow::onShowInstanceDashboard()
{
  // fork #203: aggregate already-available mod info into a snapshot overview.
  InstanceDashboardStats stats;
  stats.app_name = ui->app_selection_box->currentText();
  const auto& infos = mod_list_model_->getModInfo();
  stats.total_mods = static_cast<int>(infos.size());
  for(const auto& info : infos)
  {
    const bool enabled =
      std::any_of(info.deployer_statuses.begin(), info.deployer_statuses.end(),
                  [](bool s) { return s; });
    if(enabled)
      stats.enabled_mods++;
    else
      stats.disabled_mods++;
  }
  stats.plugin_count = -1; // not generically available
  stats.last_deploy = "";
  // Total staging size: best-effort recursive scan of the app's staging directory.
  stats.total_staging_bytes = -1;
  const std::string staging = ui->info_sdir_label->text().toStdString();
  if(!staging.empty() && sfs::exists(staging))
  {
    std::error_code ec;
    qint64 total = 0;
    for(auto it = sfs::recursive_directory_iterator(
                    staging, sfs::directory_options::skip_permission_denied, ec);
        !ec && it != sfs::recursive_directory_iterator();
        it.increment(ec))
    {
      std::error_code fec;
      if(it->is_regular_file(fec) && !fec)
        total += static_cast<qint64>(it->file_size(fec));
    }
    if(!ec)
      stats.total_staging_bytes = total;
  }
  stats.status_line = stats.total_mods == 0
                        ? "No mods installed yet"
                        : QString("%1 mods installed (%2 enabled)")
                            .arg(stats.total_mods)
                            .arg(stats.enabled_mods);
  InstanceDashboardDialog dialog(stats, this);
  dialog.exec();
}

void MainWindow::onForceRedeploy()
{
  // fork #208: purge then redeploy all deployers from scratch (repair drift/tampering).
  if(currentApp() < 0)
    return;
  const auto reply = QMessageBox::question(
    this,
    "Force redeploy?",
    "This will undeploy every deployer and then redeploy all enabled mods from scratch.\n\n"
    "Use this to recover from drift or external tampering with the deployed files. Continue?",
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No);
  if(reply != QMessageBox::Yes)
    return;
  setStatusMessage("Redeploying mods");
  setBusyStatus(true);
  emit forceRedeployMods(currentApp());
}

void MainWindow::onPruneArchives()
{
  // fork #145: ask the worker for the list of prunable archives; answered by onPrunableArchives.
  if(currentApp() < 0)
    return;
  setStatusMessage("Scanning for old archive versions");
  setBusyStatus(true);
  emit requestPrunableArchives(currentApp());
}

void MainWindow::onPrunableArchives(std::vector<PrunableArchive> archives,
                                    unsigned long total_size,
                                    int app_id)
{
  // fork #145: show the confirmation dialog and, if accepted, delete the listed archives.
  setBusyStatus(false);
  if(app_id != currentApp())
    return;
  if(archives.empty())
  {
    setStatusMessage("No old archive versions to remove", 3000);
    QMessageBox::information(this,
                             "Remove Old Archive Versions",
                             "No outdated archive versions were found to remove.");
    return;
  }
  std::vector<std::pair<std::filesystem::path, unsigned long>> items;
  items.reserve(archives.size());
  for(const auto& archive : archives)
    items.emplace_back(archive.path, archive.size);
  PruneVersionsDialog dialog(items, total_size, this);
  if(dialog.exec() != QDialog::Accepted)
    return;
  std::vector<std::filesystem::path> paths;
  paths.reserve(archives.size());
  for(const auto& archive : archives)
    paths.push_back(archive.path);
  emit pruneArchives(currentApp(), paths);
  setStatusMessage("Old archive versions removed", 3000);
}

void MainWindow::onEditModRules()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  if(!index.isValid())
    return;
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  emit getModRulesFor(currentApp(), mod_id);
}

void MainWindow::onGetModRules(int app_id, int mod_id, std::vector<ModRule> rules)
{
  if(app_id != currentApp())
    return;
  QString source_name;
  std::vector<std::pair<int, QString>> all_mods;
  for(const auto& mod_info : mod_list_model_->getModInfo())
  {
    if(mod_info.mod.id == mod_id)
      source_name = QString::fromStdString(mod_info.mod.name);
    else
      all_mods.emplace_back(mod_info.mod.id, QString::fromStdString(mod_info.mod.name));
  }
  manage_mod_rules_dialog_->setupDialog(app_id, mod_id, source_name, all_mods, rules);
  manage_mod_rules_dialog_->show();
}

void MainWindow::onModRulesChanged(int app_id, int source_mod_id, std::vector<ModRule> rules)
{
  emit setModRulesFor(app_id, source_mod_id, rules);
}

void MainWindow::onConflictDetails()
{
  if(deployer_model_->rowCount() == 0)
    return;
  const auto index = deployer_list_proxy_->mapToSource(ui->deployer_list->currentIndex());
  conflict_detail_mod_id_ = deployer_model_->data(index, ModListModel::mod_id_role).toInt();
  conflict_detail_mod_name_ = deployer_model_->data(index, ModListModel::mod_name_role).toString();
  setStatusMessage("Finding file conflicts");
  setBusyStatus(true);
  emit getFileConflicts(currentApp(), currentDeployer(), conflict_detail_mod_id_, false);
}

void MainWindow::onManageGroups()
{
  emit getGroupData(currentApp());
}

void MainWindow::onGetGroupData(int app_id,
                                std::vector<std::string> group_names,
                                std::vector<std::string> group_notes,
                                std::vector<std::vector<int>> group_members,
                                std::vector<int> active_members)
{
  if(app_id != currentApp())
    return;
  std::map<int, std::string> mod_names;
  for(const auto& mod_info : mod_list_model_->getModInfo())
    mod_names[mod_info.mod.id] = mod_info.mod.name;
  manage_groups_dialog_->setupDialog(app_id,
                                     static_cast<int>(group_names.size()),
                                     group_names,
                                     group_notes,
                                     group_members,
                                     active_members,
                                     mod_names);
  manage_groups_dialog_->show();
}

void MainWindow::onGroupRenamed(int app_id, int group, QString name)
{
  emit setGroupName(app_id, group, name);
  if(app_id == currentApp())
    emit getModInfo(app_id);
}

void MainWindow::onGroupNotesChanged(int app_id, int group, QString notes)
{
  emit setGroupNotes(app_id, group, notes);
}

void MainWindow::onGroupDissolved(int app_id, int group)
{
  emit dissolveGroup(app_id, group);
  if(app_id == currentApp())
    emit getModInfo(app_id);
}

void MainWindow::onMergeTw3Scripts()
{
  setStatusMessage("Merging Witcher 3 scripts");
  setBusyStatus(true);
  emit mergeTw3Scripts(currentApp(), currentDeployer());
}

void MainWindow::onMergeTw3Config()
{
  setStatusMessage("Merging Witcher 3 config");
  setBusyStatus(true);
  emit mergeTw3Config(currentApp(), currentDeployer());
}

void MainWindow::onCyberpunkSetup()
{
  setBusyStatus(true);
  emit getCyberpunkSetupInfo(currentApp(), currentDeployer());
}

void MainWindow::onDeployRedmods()
{
  setStatusMessage("Preparing REDmod deployment");
  setBusyStatus(true);
  emit deployRedMods(currentApp(), currentDeployer());
}

void MainWindow::onGameToolResult(QString title, QString message)
{
  QMessageBox::information(this, title, message);
}

void MainWindow::onRunGameCommand(QString name, QString command)
{
  const auto answer =
    QMessageBox::question(this,
                          "Run " + name + "?",
                          "This will run the following command:\n\n" + command +
                            "\n\nThis is experimental and runs an external tool under Proton. "
                            "Continue?",
                          QMessageBox::Yes | QMessageBox::No,
                          QMessageBox::No);
  if(answer == QMessageBox::Yes)
    runConcurrent(command, name, "REDmod");
}

void MainWindow::on_actionShow_Nexus_Page_triggered()
{
  if(!initNexusApiKey())
    return;

  setStatusMessage("Fetching data from NexusMods");
  setBusyStatus(true);
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  emit getNexusPage(currentApp(), mod_id);
}

void MainWindow::onGetNexusPage(int app_id, int mod_id, nexus::Page page)
{
  nexus_mod_dialog_->setupDialog(app_id, mod_id, page);
  nexus_mod_dialog_->show();
}

// BEGIN feature #33: open the in-app NexusMods browser for the current app.
void MainWindow::onBrowseNexusTriggered()
{
  if(!initNexusApiKey())
    return;

  // The app does not store a NexusMods domain, so derive a best-effort default from the
  // current application's name (lowercased, alphanumeric only). The browser shows the
  // domain in its title and the user can refine the search from there.
  QString domain;
  for(QChar c : ui->app_selection_box->currentText().toLower())
  {
    if(c.isLetterOrNumber())
      domain.append(c);
  }
  nexus_browser_dialog_->setupDialog(currentApp(), domain);
  nexus_browser_dialog_->show();
  nexus_browser_dialog_->raise();
  nexus_browser_dialog_->activateWindow();
}
// END feature #33.

void MainWindow::onReceiveIpcMessage(QString message)
{
  activateWindow();
  if(message == "Started")
    return;

  std::string message_str = message.toStdString();
  if(nexus::Api::nxmUrlIsValid(message_str))
  {
    Log::debug("Received download request for \"" + message.toStdString() + "\".");
    ImportModInfo info;
    info.app_id = currentApp();
    info.action_type = ImportModInfo::download;
    info.remote_request_url = message.toStdString();
    mod_import_queue_.push(info);
    if(mod_import_queue_.size() == 1)
      importMod();
  }
  else
    Log::debug("Unknown IPC message: \"" + message.toStdString() + "\"");
}

void MainWindow::onDownloadComplete(ImportModInfo info)
{
  onCompletedOperations("Download complete");
  mod_import_queue_.pop();
  info.action_type = ImportModInfo::extract;
  info.target_path = ui->info_sdir_label->text().toStdString();
  info.target_path /= temp_dir_.toStdString();
  mod_import_queue_.push(info);
  importMod();
}

void MainWindow::onModDownloadRequested(int app_id,
                                        int mod_id,
                                        int file_id,
                                        QString mod_url,
                                        QString version)
{
  ImportModInfo info;
  info.app_id = app_id;
  info.action_type = ImportModInfo::download;
  info.remote_file_id = file_id;
  info.remote_source = mod_url.toStdString();
  info.target_group_id = mod_id;
  info.version_overwrite = version.toStdString();
  mod_import_queue_.push(info);
  if(mod_import_queue_.size() == 1)
    importMod();
}

void MainWindow::onDownloadFailed()
{
  onCompletedOperations("Download failed");
  mod_import_queue_.pop();
  if(!mod_import_queue_.empty())
    importMod();
}

// fork #1: Import a Nexus Collection. The dialog parses collection.json and, on accept,
// emits modDownloadRequested per mod into the existing queued download/import flow.
void MainWindow::onImportCollection()
{
  if(collection_dialog_->setupImport(currentApp()))
    collection_dialog_->exec();
}

// fork #2: Export the current app's mods as a Nexus Collection manifest.
void MainWindow::onExportCollection()
{
  const std::vector<ModInfo> mods = mod_list_model_->getModInfo();
  // Derive the game domain from the first mod that carries a NexusMods source.
  QString game_domain;
  for(const ModInfo& info : mods)
  {
    const auto domain_and_id = nexus::Api::extractDomainAndModId(info.mod.remote_source);
    if(domain_and_id)
    {
      game_domain = QString::fromStdString(domain_and_id->first);
      break;
    }
  }
  collection_dialog_->setupExport(
    currentApp(), game_domain, ui->info_name_label->text(), mods);
  collection_dialog_->exec();
}

void MainWindow::on_actionReinstall_From_Local_triggered()
{
  const auto index = mod_list_proxy_->mapToSource(ui->mod_list->selectionModel()->currentIndex());
  const int mod_id = mod_list_model_->data(index, ModListModel::mod_id_role).toInt();
  const std::string local_source =
    mod_list_model_->data(index, ModListModel::local_source_role).toString().toStdString();
  if(!std::filesystem::exists(local_source))
  {
    onReceiveError("Error",
                   std::format("Local source \"{}\" no longer exists.", local_source).c_str());
    return;
  }
  const std::string remote_source =
    mod_list_model_->data(index, ModListModel::remote_source_role).toString().toStdString();
  const std::string name =
    mod_list_model_->data(index, ModListModel::mod_name_role).toString().toStdString();
  const std::string version =
    mod_list_model_->data(index, ModListModel::mod_version_role).toString().toStdString();

  ImportModInfo info;
  info.app_id = currentApp();
  info.action_type = ImportModInfo::extract;
  info.local_source = local_source;
  info.remote_source = remote_source;
  info.target_group_id = mod_id;
  info.target_path = ui->info_sdir_label->text().toStdString();
  info.target_path /= temp_dir_.toStdString();
  info.name_overwrite = name;
  info.version_overwrite = version;
  mod_import_queue_.push(info);
  if(mod_import_queue_.size() == 1)
    importMod();
}

void MainWindow::on_check_mod_updates_button_clicked()
{
  if(!initNexusApiKey())
    return;
  setStatusMessage("Checking for updates");
  setBusyStatus(true);
  emit checkForModUpdates(currentApp());
  emit getModInfo(currentApp());
}

void MainWindow::on_actionSelect_All_triggered()
{
  if(ui->app_tab_widget->currentIndex() == mods_tab_idx)
  {
    for(int i = 0; i < mod_list_proxy_->rowCount(); i++)
      ui->mod_list->selectionModel()->select(mod_list_proxy_->index(i, 0),
                                             QItemSelectionModel::Select);
    ui->mod_list->update();
  }
}

void MainWindow::on_filter_mods_with_updates_cb_stateChanged(int state)
{
  if(state == Qt::CheckState::Checked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_no_updates);
  else if(state == Qt::CheckState::PartiallyChecked)
    mod_list_proxy_->addFilter(ModListProxyModel::filter_updates);
  else
  {
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_no_updates, false);
    mod_list_proxy_->removeFilter(ModListProxyModel::filter_updates, true);
  }
}

void MainWindow::on_actionCheck_For_Updates_triggered()
{
  if(!initNexusApiKey())
    return;
  setStatusMessage("Checking for updates");
  setBusyStatus(true);
  emit checkModsForUpdates(currentApp(), ui->mod_list->getSelectedModIds());
  emit getModInfo(currentApp());
}

void MainWindow::on_actionSuppress_Update_triggered()
{
  setBusyStatus(true);
  const auto mods = ui->mod_list->getSelectedModIds();
  Log::info(std::format(
    "Suppressing update notifications for {} mod{}.", mods.size(), mods.size() == 1 ? "" : "s"));
  emit suppressUpdateNotification(currentApp(), mods);
  emit getModInfo(currentApp());
}

void MainWindow::onModInstallationComplete(bool success)
{
  // fork #45: continue MO2 import chain if active
  if(!mo2_pending_mods_.empty() && mo2_pending_app_id_ >= 0)
  {
    if(!success)
    {
      Log::error("MO2 import: mod installation failed — aborting remaining imports");
      mo2_pending_mods_.clear();
      mo2_pending_app_id_ = -1;
    }
    else
      installNextMo2Mod();
    return;
  }

  onCompletedOperations(success ? "Installation complete" : "Installation failed");
  if(!mod_import_queue_.empty())
    importMod();
}

void MainWindow::onGetExternalChangesInfo(int app_id,
                                          ExternalChangesInfo info,
                                          int num_deployers,
                                          bool deploy)
{
  setStatusMessage("");
  if(!info.file_changes.empty())
  {
    external_changes_dialog_->setup(app_id, info, deploy);
    external_changes_dialog_->show();
  }
  else
    onExternalChangesHandled(app_id, info.deployer_id, num_deployers, deploy);
}

void MainWindow::onExternalChangesHandled(int app_id, int deployer, int num_deployers, bool deploy)
{
  setStatusMessage("");
  setBusyStatus(false);
  if(deployer == num_deployers - 1 || !deploy_for_all_)
  {
    // fork feature #49: deploy dry-run / preview.
    // Optional confirm step shown immediately before the real deploy is dispatched to the
    // ApplicationManager worker thread. The DeploymentPlan primitive lives on Deployer
    // (Deployer::computeDeploymentPlan); the deployer instances are owned by the
    // ApplicationManager and run on a separate thread, so the fully populated per-deployer
    // plans must be delivered to this point via an ApplicationManager signal. Wiring that
    // signal touches applicationmanager, which is out of scope for this change, so the seam is
    // staged here: when deploy_preview_plans_ is populated (by that future signal) and the
    // feature is enabled, the preview dialog is shown and deployment only proceeds on OK.
    if(deploy && show_deploy_preview_ && !deploy_preview_plans_.empty())
    {
      DeployPreviewDialog preview(deploy_preview_plans_, this);
      deploy_preview_plans_.clear();
      if(preview.exec() != QDialog::Accepted)
      {
        setStatusMessage("Deployment cancelled");
        setBusyStatus(false);
        return;
      }
    }
    const std::string action_string = deploy ? "Deploying" : "Undeploying";
    Log::info(action_string + " mods...");
    setStatusMessage((action_string + " mods").c_str());
    setBusyStatus(true, true, true);
    if(deploy_for_all_)
    {
      if(deploy)
        emit deployMods(app_id);
      else
        emit unDeployMods(app_id);
    }
    else
    {
      if(deploy)
        emit deployModsFor(app_id, { deployer });
      else
        emit unDeployModsFor(app_id, { deployer });
    }
    if(app_id == currentApp())
      emit getDeployerInfo(currentApp(), currentDeployer());
  }
  else
  {
    setStatusMessage("Checking for external changes");
    setBusyStatus(true, true, true);
    emit getExternalChanges(app_id, deployer + 1, deploy);
  }
}

void MainWindow::onExternalChangesDialogCompleted(int app_id,
                                                  int deployer,
                                                  const FileChangeChoices& changes_to_keep,
                                                  bool deploy)
{
  setStatusMessage("Applying changes");
  emit keepOrRevertFileModifications(app_id, deployer, changes_to_keep, deploy);
}

void MainWindow::onExternalChangesDialogAborted()
{
  setStatusMessage("Deployment aborted", 3000);
  setBusyStatus(false);
}

void MainWindow::on_export_app_config_button_clicked()
{
  QStringList deployers;
  for(int i = 0; i < ui->deployer_selection_box->count(); i++)
    deployers << ui->deployer_selection_box->itemText(i);
  QStringList auto_tags;
  for(const auto& [name, _] : auto_tags_)
    auto_tags << name.c_str();
  export_app_config_dialog_->init(
    currentApp(), ui->app_selection_box->currentText(), deployers, auto_tags);
  setBusyStatus(true, false);
  export_app_config_dialog_->show();
}

void MainWindow::onExportAppConfigDialogComplete(int app_id,
                                                 std::vector<int> deployers,
                                                 QStringList auto_tags)
{
  setBusyStatus(false);
  emit exportAppConfiguration(app_id, deployers, auto_tags);
}

void MainWindow::on_undeploy_button_clicked()
{
  setStatusMessage("Checking for external changes");
  setBusyStatus(true, true, true);
  if(deploy_for_all_)
    emit getExternalChanges(currentApp(), 0, false);
  else
    emit getExternalChanges(currentApp(), currentDeployer(), false);
}

void MainWindow::onUpdateIgnoredFiles(int app_id, int deployer)
{
  setStatusMessage("Updating ignore list");
  setBusyStatus(true, true, true);
  emit updateIgnoredFiles(app_id, deployer);
  if(app_id == currentApp() && deployer == currentDeployer())
    emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::on_actionAdd_to_Ignore_List_triggered()
{
  const auto index =
    deployer_list_proxy_->mapToSource(ui->deployer_list->selectionModel()->currentIndex());
  int mod_id = deployer_model_->data(index, ModListModel::mod_id_role).toInt();
  emit addModToIgnoreList(currentApp(), currentDeployer(), mod_id);
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onLaunchToolButtonPressed(int row, int col)
{
  const QString command(tools_[row].getCommand(is_a_flatpak_).c_str());
  const QString name(tools_[row].getName().c_str());
  if(command.isEmpty())
  {
    Log::error(("Command for tool '" + name + "' is empty").toStdString());
    QMessageBox::warning(
      this,
      "No command configured",
      "No command is configured for tool '" + name +
        "'.\n\nEdit the tool to set a command before running it.");
    return;
  }
  runConcurrent(command, name, "Tool", true);
}

void MainWindow::onEditToolButtonPressed(int row, int col)
{
  add_tool_dialog_->setEditMode(currentApp(), row, tools_[row]);
  add_tool_dialog_->exec();
}

void MainWindow::onRemoveToolButtonPressed(int row, int col)
{
  if(ask_remove_tool_)
  {
    message_box_->setText(
      ("Are you sure you want to remove \"" + tools_[row].getName() + "\"?").c_str());
    auto* check_box = message_box_->checkBox();
    check_box->setHidden(false);
    check_box->setCheckState(Qt::Unchecked);
    check_box->setText("Don't ask again");
    int answer = message_box_->exec();
    ask_remove_tool_ = check_box->checkState() == Qt::Unchecked;
    if(answer == QMessageBox::No)
      return;
  }
  Log::info("Removing tool '" + tools_[row].getName() + "'");
  emit removeTool(currentApp(), row);
  emit getAppInfo(currentApp());
}

void MainWindow::onToolAdded(int app_id, Tool tool)
{
  Log::info("Adding tool '" + tool.getName() + "'");
  emit addTool(app_id, tool);
  emit getAppInfo(currentApp());
}

void MainWindow::onToolEdited(int app_id, int tool_id, Tool tool)
{
  emit editTool(app_id, tool_id, tool);
  emit getAppInfo(currentApp());
}

void MainWindow::onModActionTriggered(int action)
{
  emit applyModAction(currentApp(),
                      currentDeployer(),
                      action,
                      ui->deployer_list->currentIndex().data(ModListModel::mod_id_role).toInt());
  emit getDeployerInfo(currentApp(), currentDeployer());
}

void MainWindow::onSortAppsAlphaToggled(bool checked)
{
  // Persist the new setting immediately so it survives a crash as well.
  sort_apps_alphabetically_ = checked;
  QSettings(QCoreApplication::applicationName())
    .setValue("sort_apps_alphabetically", sort_apps_alphabetically_);
  // Rebuild the combo box in sorted or original order.
  emit getApplicationNames(false);
}

// ---------------------------------------------------------------------------
// MO2 import  (fork #45 / limo-app/limo#92)
// ---------------------------------------------------------------------------

void MainWindow::installNextMo2Mod()
{
  if(mo2_pending_mods_.empty())
  {
    // All mods installed — apply disabled states in a second pass.
    // Use getDeployerInfo to retrieve the load order with mod_ids, then
    // call setModStatus for any mod that should be disabled.
    // We re-use onGetDeployerInfo to do this, but mark the import finished first
    // so that the normal UI refresh path takes over.
    const int app_id = mo2_pending_app_id_;
    mo2_pending_app_id_ = -1;

    // Apply enabled/disabled states for each mod.
    // After installMod, mods are always added as enabled.  We now iterate
    // through mo2_mod_enabled_map_ and call setModStatus(false) for those
    // that should be disabled.  We emit getModInfo to get the current mod_ids.
    //
    // Since setModStatus / getModInfo are async, we emit everything now.
    // The ApplicationManager processes them sequentially in the worker thread.
    // After all setModStatus calls, the UI is refreshed by the final
    // getModInfo / getDeployerInfo pair.
    emit getModInfo(app_id); // triggers onGetModInfo -> updates list
    // Actual setModStatus calls are emitted from onGetModInfoForMo2Finalise,
    // which is connected below when pending is cleared.
    Log::info("MO2 import: all mods installed; applying enabled/disabled states");

    // Quick path: emit setModStatus directly — we look up mod_ids from the
    // deployer list.  The load order (deployer 0) maps index -> mod_id.
    // We saved (name -> enabled) in mo2_mod_enabled_map_ during install.
    // We cannot do this here because we don't yet have the mod_ids; defer to
    // the next onGetDeployerInfo call by storing a pending flag.
    mo2_finalising_ = true;
    mo2_app_id_finalising_ = app_id;
    emit getDeployerInfo(app_id, 0);
    return;
  }

  const Mo2ModEntry entry = mo2_pending_mods_.front();
  mo2_pending_mods_.erase(mo2_pending_mods_.begin());

  const int remaining = static_cast<int>(mo2_pending_mods_.size());
  setStatusMessage(
    QString("Importing mod '%1' (%2 remaining)")
      .arg(QString::fromStdString(entry.name))
      .arg(remaining));
  Log::info(std::format("MO2 import: installing '{}' ({} remaining)",
                        entry.name, remaining));

  ImportModInfo info;
  info.app_id = mo2_pending_app_id_;
  info.action_type = ImportModInfo::ActionType::install;
  info.current_path = entry.source_path;
  info.local_source = entry.source_path;
  info.name = entry.name;
  info.version = "";
  info.installer = Installer::SIMPLEINSTALLER;
  info.installer_flags = 0;
  info.root_level = 0;
  info.deployers = { 0 }; // deployer index 0 = the Simple Deployer created during import

  // Track enabled state for final pass
  mo2_mod_enabled_map_[entry.name] = entry.enabled;

  setBusyStatus(true);
  emit installMod(mo2_pending_app_id_, info);
}

void MainWindow::onImportMo2ActionTriggered()
{
  import_mo2_dialog_->init();
  import_mo2_dialog_->show();
}

void MainWindow::onImportMo2DialogAccepted(EditApplicationInfo app_info,
                                           Mo2ParseResult parse_result)
{
  if(parse_result.mods.empty())
    Log::warning("MO2 import: no mods found — creating empty application");

  // Create the staging directory if it doesn't exist yet
  const sfs::path staging_dir(app_info.staging_dir);
  std::error_code ec;
  sfs::create_directories(staging_dir, ec);
  if(ec)
  {
    Log::error(std::format("MO2 import: cannot create staging directory '{}': {}",
                           staging_dir.string(), ec.message()));
    return;
  }

  Log::info(std::format("MO2 import: importing {} mods from profile '{}'",
                        parse_result.mods.size(), parse_result.profile_name));

  // Store the mod list — consumed one entry at a time by installNextMo2Mod()
  mo2_pending_mods_ = std::move(parse_result.mods);
  mo2_mod_enabled_map_.clear();
  mo2_finalising_ = false;

  // The new app will occupy index == current count (0-based)
  mo2_pending_app_id_ = ui->app_selection_box->count();

  setBusyStatus(true);
  setStatusMessage("Creating MO2 application");
  emit addApplication(app_info);
  emit getApplicationNames(true);
}

// Fork issue #7: Export the current deployer's ordered mod list to CSV or Markdown.
void MainWindow::on_actionExport_Mod_List_triggered()
{
  if(deployer_model_->rowCount() == 0)
  {
    message_box_->setText("No mods in the current deployer's load order to export.");
    message_box_->setWindowTitle("Export Mod List");
    message_box_->exec();
    return;
  }

  const QString filter =
    "CSV files (*.csv);;Markdown files (*.md);;All files (*)";
  QString selected_filter;
  const QString path = QFileDialog::getSaveFileName(
    this, "Export Mod List", QDir::homePath(), filter, &selected_filter);
  if(path.isEmpty())
    return;

  // Build a lookup map from mod id -> ModInfo for version and remote_source.
  const std::vector<ModInfo>& all_mods = mod_list_model_->getModInfo();
  std::unordered_map<int, const ModInfo*> mod_by_id;
  mod_by_id.reserve(all_mods.size());
  for(const auto& info : all_mods)
    mod_by_id[info.mod.id] = &info;

  const QString deployer_name =
    ui->deployer_selection_box->currentText();

  const bool is_csv =
    path.endsWith(".csv", Qt::CaseInsensitive) ||
    (!path.endsWith(".md", Qt::CaseInsensitive) && selected_filter.startsWith("CSV"));

  QFile file(path);
  if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    message_box_->setText(
      QString("Could not open file for writing:\n%1").arg(file.errorString()));
    message_box_->setWindowTitle("Export Mod List");
    message_box_->exec();
    return;
  }
  QTextStream out(&file);

  const int row_count = deployer_model_->rowCount();

  if(is_csv)
  {
    // Helper lambda: quote a CSV field, escaping embedded double-quotes.
    auto csv_field = [](const QString& s) -> QString
    {
      QString escaped = s;
      escaped.replace("\"", "\"\"");
      return "\"" + escaped + "\"";
    };

    out << "Load Order,Mod Name,Version,Enabled,Tags,Nexus URL\n";
    for(int row = 0; row < row_count; ++row)
    {
      const QModelIndex idx = deployer_model_->index(row, 0, {});
      const int mod_id = idx.data(ModListModel::mod_id_role).toInt();
      const QString mod_name =
        QString::fromStdString(deployer_model_->index(row, 0, {})
                                 .data(ModListModel::mod_name_role)
                                 .toString()
                                 .toStdString());
      const bool enabled =
        idx.data(DeployerListModel::mod_status_role).toBool();

      // Collect tags from the deployer model.
      const QStringList tags =
        idx.data(DeployerListModel::mod_tags_role).toStringList();

      QString version;
      QString remote_source;
      if(mod_by_id.count(mod_id))
      {
        const ModInfo* info = mod_by_id.at(mod_id);
        version = QString::fromStdString(info->mod.version);
        remote_source = QString::fromStdString(info->mod.remote_source);
      }

      out << QString::number(row + 1) << ","
          << csv_field(mod_name) << ","
          << csv_field(version) << ","
          << (enabled ? "yes" : "no") << ","
          << csv_field(tags.join("; ")) << ","
          << csv_field(remote_source) << "\n";
    }
  }
  else
  {
    // Markdown table.
    out << "# Mod List: " << deployer_name << "\n\n";
    out << "| # | Mod Name | Version | Enabled | Tags | Nexus URL |\n";
    out << "|---|----------|---------|---------|------|-----------|\n";

    // Helper lambda: escape pipe characters inside a Markdown table cell.
    auto md_cell = [](const QString& s) -> QString
    {
      QString escaped = s;
      escaped.replace("|", "\\|");
      return escaped;
    };

    for(int row = 0; row < row_count; ++row)
    {
      const QModelIndex idx = deployer_model_->index(row, 0, {});
      const int mod_id = idx.data(ModListModel::mod_id_role).toInt();
      const QString mod_name =
        idx.data(ModListModel::mod_name_role).toString();
      const bool enabled =
        idx.data(DeployerListModel::mod_status_role).toBool();
      const QStringList tags =
        idx.data(DeployerListModel::mod_tags_role).toStringList();

      QString version;
      QString remote_source;
      if(mod_by_id.count(mod_id))
      {
        const ModInfo* info = mod_by_id.at(mod_id);
        version = QString::fromStdString(info->mod.version);
        remote_source = QString::fromStdString(info->mod.remote_source);
      }

      // Render Nexus URL as a Markdown link when present.
      QString url_cell;
      if(!remote_source.isEmpty())
        url_cell = "[link](" + md_cell(remote_source) + ")";

      out << "| " << (row + 1)
          << " | " << md_cell(mod_name)
          << " | " << md_cell(version)
          << " | " << (enabled ? "yes" : "no")
          << " | " << md_cell(tags.join(", "))
          << " | " << url_cell
          << " |\n";
    }
  }

  file.close();
  Log::info("Exported mod list for deployer '" + deployer_name.toStdString() +
            "' to '" + path.toStdString() + "'");
  setStatusMessage(QString("Mod list exported to %1").arg(path), 4000);
}
