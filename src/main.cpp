/**
 * \file main.cpp
 * \brief Contains the main function.
 *
 * Implements a scriptable headless CLI (fork #44) alongside the existing GUI
 * entry point.  All headless subcommands operate through ApplicationManager,
 * mirroring the paths already used by the existing -l/--list and -d/--deploy
 * options.
 *
 * Exit codes:
 *   0  – success
 *   1  – argument / usage error
 *   2  – another Limo instance is already running (GUI path only)
 *   3  – runtime error reported by ApplicationManager
 */

#include "core/consts.h" // fork #22
#include "core/deployer.h"
#include "core/deployerfactory.h"
#include "core/editapplicationinfo.h"
#include "core/editdeployerinfo.h"
#include "core/editprofileinfo.h"
#include "core/installer.h"
#include "core/pack.h"
#include "core/tool.h"
#include "ui/applicationmanager.h"
#include "ui/ipcclient.h"
#include "ui/mainwindow.h"
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo> // fork #22
#include <QLocale>      // fork #22
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QTranslator> // fork #22
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif


/*!
 * \brief Applies the color scheme / theme selected in the settings dialog.
 *
 * Theme indices match the entries of the theme_box combo in the settings dialog:
 * 0 = System (Qt default palette/style is left untouched), 1 = Light, 2 = Dark,
 * 3 = High-contrast.
 * \param app The running application instance.
 * \param theme The theme index read from QSettings.
 */
static void applyTheme(QApplication& app, int theme)
{
  // System: leave Qt's default palette and style in place.
  if(theme <= 0)
    return;

  // Use the Fusion style so the custom palette is honored consistently across platforms.
  if(auto* style = QStyleFactory::create("Fusion"))
    app.setStyle(style);

  QPalette palette;
  if(theme == 1) // Light
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    palette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xe8, 0xe8, 0xe8));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::ButtonText, Qt::black);
    palette.setColor(QPalette::Link, QColor(0x24, 0x6c, 0xe0));
    palette.setColor(QPalette::Highlight, QColor(0x24, 0x6c, 0xe0));
    palette.setColor(QPalette::HighlightedText, Qt::white);
  }
  else if(theme == 2) // Dark
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    palette.setColor(QPalette::Window, QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(0x23, 0x23, 0x23));
    palette.setColor(QPalette::AlternateBase, QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::ToolTipBase, QColor(0x23, 0x23, 0x23));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Link, QColor(0x2a, 0x82, 0xda));
    palette.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7f, 0x7f, 0x7f));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x7f, 0x7f, 0x7f));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x7f, 0x7f, 0x7f));
  }
  else // High-contrast
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    palette.setColor(QPalette::Window, Qt::black);
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, Qt::black);
    palette.setColor(QPalette::AlternateBase, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::ToolTipBase, Qt::black);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, Qt::black);
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Link, QColor(0x00, 0xff, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0xff, 0xff, 0x00));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
  }
  app.setPalette(palette);
}


/*!
 * \brief Installs the UI translation for the configured language (fork #22).
 *
 * Reads the "language" key from QSettings(QCoreApplication::applicationName()).
 * An empty value or "system" uses QLocale::system(); otherwise the value is
 * treated as a locale string (e.g. "en", "de").  The compiled "limo_<locale>.qm"
 * is searched for in the install location (share/limo/translations under the
 * install prefix) and in a local-build location (./translations), mirroring how
 * the app resolves steam_app_configs.  Qt's own base translation
 * (qtbase_<locale>.qm) is loaded as well when available.  Missing translations
 * are ignored silently so a failure here never prevents startup; the source
 * (English) strings are then used.
 *
 * The QTranslator objects are heap-allocated and parented to \p app so they live
 * for the lifetime of the application.
 * \param app The running application instance.
 */
static void installTranslations(QApplication& app)
{
  QString locale_name;
  {
    QSettings settings(QCoreApplication::applicationName());
    locale_name = settings.value("language", QString()).toString().trimmed();
  }

  QLocale locale = (locale_name.isEmpty() || locale_name == "system")
                     ? QLocale::system()
                     : QLocale(locale_name);

  // Candidate directories for the bundled .qm files: install location first,
  // then a local-build fallback (mirrors steam_app_configs resolution).
  // getenv returns nullptr when "container" is unset; comparing a null char* to a
  // std::string dereferences null, so guard for non-null before the comparison.
  const char* container_env = getenv("container");
  const bool is_flatpak = std::filesystem::exists("/.flatpak-info") ||
                          (container_env && std::string(container_env) == "flatpak");
  std::vector<QString> dirs;
  dirs.push_back(QString::fromStdString(
    (std::filesystem::path(is_flatpak ? "/app" : APP_INSTALL_PREFIX) / "share/limo/translations")
      .string()));
  dirs.emplace_back("translations");

  // Limo's own translation.
  auto* translator = new QTranslator(&app);
  for(const QString& dir : dirs)
  {
    if(translator->load(locale, "limo", "_", dir))
    {
      app.installTranslator(translator);
      break;
    }
  }

  // Qt's base translation (buttons, dialogs, ...) when shipped with this Qt.
  auto* qt_translator = new QTranslator(&app);
  if(qt_translator->load(
       locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    app.installTranslator(qt_translator);
}


// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

/*! \brief Prints \p msg to stderr and returns exit code 1. */
int cliError(const std::string& msg)
{
  std::cerr << "Error: " << msg << "\n";
  return 1;
}

/*! \brief Converts a QString to a std::string. */
inline std::string qs(const QString& s) { return s.toStdString(); }

/*! \brief Tries to parse \p s as a non-negative int; returns -1 on failure. */
int parseId(const std::string& s)
{
  try
  {
    std::size_t pos;
    int v = std::stoi(s, &pos);
    if(pos != s.size() || v < 0)
      return -1;
    return v;
  }
  catch(...)
  {
    return -1;
  }
}

// Forward declarations for the option-parsing helpers (defined alongside the operation
// subcommands further down) so earlier subcommands such as install can use them too.
std::optional<std::string> takeOption(std::vector<std::string>& args, const std::string& flag);
bool takeFlag(std::vector<std::string>& args, const std::string& flag);
std::vector<int> parseIdList(const std::vector<std::string>& args, std::size_t from);

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

/*! \brief Renders the list of apps (and optionally profiles) as JSON. */
QJsonDocument appsToJson(const ApplicationManager& am)
{
  QJsonArray arr;
  for(int i = 0; i < am.getNumApplications(); i++)
  {
    QJsonObject obj;
    obj["id"]   = i;
    obj["name"] = QString::fromStdString(am.getCliAppInfo(i).name);
    QJsonArray profiles;
    for(int j = 0; const auto& p : am.getCliProfileNames(i))
    {
      QJsonObject po;
      po["id"]   = j++;
      po["name"] = QString::fromStdString(p);
      profiles.append(po);
    }
    obj["profiles"] = profiles;
    arr.append(obj);
  }
  return QJsonDocument(arr);
}

/*! \brief Renders deployer names for one app as JSON. */
QJsonDocument deployersToJson(const ApplicationManager& am, int app_id)
{
  QJsonArray arr;
  int idx = 0;
  for(const auto& name : am.getCliDeployerNames(app_id))
  {
    QJsonObject o;
    o["id"]   = idx++;
    o["name"] = QString::fromStdString(name);
    arr.append(o);
  }
  return QJsonDocument(arr);
}

/*! \brief Renders mod list for one app as JSON. */
QJsonDocument modsToJson(const ApplicationManager& am, int app_id)
{
  QJsonArray arr;
  for(const auto& info : am.getCliModInfo(app_id))
  {
    QJsonObject o;
    o["id"]      = info.mod.id;
    o["name"]    = QString::fromStdString(info.mod.name);
    o["version"] = QString::fromStdString(info.mod.version);
    QJsonArray depls;
    for(int k = 0; k < static_cast<int>(info.deployers.size()); k++)
    {
      QJsonObject d;
      d["id"]      = info.deployer_ids[k];
      d["name"]    = QString::fromStdString(info.deployers[k]);
      d["enabled"] = info.deployer_statuses[k];
      depls.append(d);
    }
    o["deployers"] = depls;
    arr.append(o);
  }
  return QJsonDocument(arr);
}

/*! \brief Renders profile list for one app as JSON. */
QJsonDocument profilesToJson(const ApplicationManager& am, int app_id)
{
  QJsonArray arr;
  int idx = 0;
  for(const auto& name : am.getCliProfileNames(app_id))
  {
    QJsonObject o;
    o["id"]   = idx++;
    o["name"] = QString::fromStdString(name);
    arr.append(o);
  }
  return QJsonDocument(arr);
}

/*! \brief Renders a status summary for one app as JSON. */
QJsonDocument statusToJson(const ApplicationManager& am, int app_id)
{
  AppInfo info = am.getCliAppInfo(app_id);
  QJsonObject root;
  root["id"]   = app_id;
  root["name"] = QString::fromStdString(info.name);
  root["staging_dir"] = QString::fromStdString(info.staging_dir);
  root["num_mods"] = info.num_mods;

  QJsonArray profiles;
  int pidx = 0;
  for(const auto& pname : am.getCliProfileNames(app_id))
  {
    QJsonObject po;
    po["id"]   = pidx++;
    po["name"] = QString::fromStdString(pname);
    profiles.append(po);
  }
  root["profiles"] = profiles;

  QJsonArray deployers;
  for(int d = 0; d < static_cast<int>(info.deployers.size()); d++)
  {
    QJsonObject dobj;
    dobj["id"]        = d;
    dobj["name"]      = QString::fromStdString(info.deployers[d]);
    dobj["type"]      = QString::fromStdString(info.deployer_types[d]);
    dobj["num_mods"]  = info.deployer_mods[d];
    deployers.append(dobj);
  }
  root["deployers"] = deployers;
  return QJsonDocument(root);
}

// ---------------------------------------------------------------------------
// Plain-text helpers
// ---------------------------------------------------------------------------

void printApps(const ApplicationManager& am)
{
  for(int i = 0; i < am.getNumApplications(); i++)
  {
    AppInfo info = am.getCliAppInfo(i);
    std::cout << "[" << i << "] " << info.name << "\n";
    int pidx = 0;
    for(const auto& p : am.getCliProfileNames(i))
      std::cout << "  profile [" << pidx++ << "] " << p << "\n";
  }
}

void printDeployers(const ApplicationManager& am, int app_id)
{
  int idx = 0;
  for(const auto& name : am.getCliDeployerNames(app_id))
    std::cout << "[" << idx++ << "] " << name << "\n";
}

void printMods(const ApplicationManager& am, int app_id)
{
  for(const auto& info : am.getCliModInfo(app_id))
  {
    std::cout << "[" << info.mod.id << "] " << info.mod.name;
    if(!info.mod.version.empty())
      std::cout << "  (" << info.mod.version << ")";
    std::cout << "\n";
    for(int k = 0; k < static_cast<int>(info.deployers.size()); k++)
    {
      std::cout << "    deployer [" << info.deployer_ids[k] << "] "
                << info.deployers[k]
                << "  " << (info.deployer_statuses[k] ? "enabled" : "disabled") << "\n";
    }
  }
}

void printProfiles(const ApplicationManager& am, int app_id)
{
  int idx = 0;
  for(const auto& name : am.getCliProfileNames(app_id))
    std::cout << "[" << idx++ << "] " << name << "\n";
}

void printStatus(const ApplicationManager& am, int app_id)
{
  AppInfo info = am.getCliAppInfo(app_id);
  std::cout << "App [" << app_id << "] " << info.name << "\n";
  std::cout << "  Staging dir : " << info.staging_dir << "\n";
  std::cout << "  Installed mods: " << info.num_mods << "\n";
  std::cout << "  Profiles:\n";
  int pidx = 0;
  for(const auto& pname : am.getCliProfileNames(app_id))
    std::cout << "    [" << pidx++ << "] " << pname << "\n";
  std::cout << "  Deployers:\n";
  for(int d = 0; d < static_cast<int>(info.deployers.size()); d++)
  {
    std::cout << "    [" << d << "] " << info.deployers[d]
              << "  type=" << info.deployer_types[d]
              << "  mods=" << info.deployer_mods[d] << "\n";
  }
}

// ---------------------------------------------------------------------------
// Subcommand: list
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: list apps | list deployers <app> | list mods <app> |
 *                 list profiles <app>
 * \param sub_args Positional arguments after "list".
 * \param json     If true, emit JSON output.
 * \return Exit code.
 */
int cmdList(const std::vector<std::string>& sub_args, bool json_out)
{
  if(sub_args.empty())
  {
    std::cerr << "Usage: limo list <apps|deployers|mods|profiles> [app_id]\n";
    return 1;
  }

  const std::string& what = sub_args[0];

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(what == "apps")
  {
    if(json_out)
      std::cout << qs(appsToJson(am).toJson(QJsonDocument::Indented)) << "\n";
    else
      printApps(am);
    return 0;
  }

  // All remaining sub-subcommands need an app_id
  if(sub_args.size() < 2)
  {
    std::cerr << "Usage: limo list " << what << " <app_id>\n";
    return 1;
  }
  int app_id = parseId(sub_args[1]);
  if(app_id < 0 || app_id >= am.getNumApplications())
    return cliError("app_id '" + sub_args[1] + "' is out of range (0.." +
                    std::to_string(am.getNumApplications() - 1) + ").");

  if(what == "deployers")
  {
    if(json_out)
      std::cout << qs(deployersToJson(am, app_id).toJson(QJsonDocument::Indented)) << "\n";
    else
      printDeployers(am, app_id);
    return 0;
  }
  if(what == "mods")
  {
    if(json_out)
      std::cout << qs(modsToJson(am, app_id).toJson(QJsonDocument::Indented)) << "\n";
    else
      printMods(am, app_id);
    return 0;
  }
  if(what == "profiles")
  {
    if(json_out)
      std::cout << qs(profilesToJson(am, app_id).toJson(QJsonDocument::Indented)) << "\n";
    else
      printProfiles(am, app_id);
    return 0;
  }
  if(what == "tools")
  {
    const AppInfo info = am.getCliAppInfo(app_id);
    if(json_out)
    {
      QJsonArray arr;
      for(int i = 0; const Tool& tool : info.tools)
      {
        QJsonObject o;
        o["id"] = i++;
        o["name"] = QString::fromStdString(tool.getName());
        o["command"] = QString::fromStdString(tool.getCommand(false));
        arr.append(o);
      }
      std::cout << qs(QJsonDocument(arr).toJson(QJsonDocument::Indented)) << "\n";
    }
    else
    {
      for(int i = 0; const Tool& tool : info.tools)
        std::cout << "[" << i++ << "] " << tool.getName() << "\n      " << tool.getCommand(false)
                  << "\n";
    }
    return 0;
  }
  if(what == "tags")
  {
    const AppInfo info = am.getCliAppInfo(app_id);
    if(json_out)
    {
      QJsonArray arr;
      for(const auto& [name, count] : info.num_mods_per_manual_tag)
      {
        QJsonObject o;
        o["name"] = QString::fromStdString(name);
        o["type"] = "manual";
        o["num_mods"] = count;
        arr.append(o);
      }
      for(const auto& [name, count] : info.num_mods_per_auto_tag)
      {
        QJsonObject o;
        o["name"] = QString::fromStdString(name);
        o["type"] = "auto";
        o["num_mods"] = count;
        arr.append(o);
      }
      std::cout << qs(QJsonDocument(arr).toJson(QJsonDocument::Indented)) << "\n";
    }
    else
    {
      std::cout << "Manual tags:\n";
      for(const auto& [name, count] : info.num_mods_per_manual_tag)
        std::cout << "  " << name << " (" << count << " mod(s))\n";
      std::cout << "Auto tags:\n";
      for(const auto& [name, count] : info.num_mods_per_auto_tag)
        std::cout << "  " << name << " (" << count << " mod(s))\n";
    }
    return 0;
  }
  if(what == "packs")
  {
    const std::vector<Pack> packs = am.getCliPacks(app_id);
    const std::vector<std::string> active = am.getCliActivePacks(app_id);
    const auto is_active = [&](const std::string& n)
    { return std::find(active.begin(), active.end(), n) != active.end(); };
    if(json_out)
    {
      QJsonArray arr;
      for(const Pack& pack : packs)
      {
        QJsonObject o;
        o["name"] = QString::fromStdString(pack.name);
        o["notes"] = QString::fromStdString(pack.notes);
        o["active"] = is_active(pack.name);
        QJsonArray mods;
        for(int id : pack.mod_ids)
          mods.append(id);
        o["mod_ids"] = mods;
        arr.append(o);
      }
      std::cout << qs(QJsonDocument(arr).toJson(QJsonDocument::Indented)) << "\n";
    }
    else
    {
      for(const Pack& pack : packs)
      {
        std::cout << (is_active(pack.name) ? "[active] " : "[      ] ") << pack.name;
        if(!pack.notes.empty())
          std::cout << " - " << pack.notes;
        std::cout << " (" << pack.mod_ids.size() << " mod(s))\n";
      }
    }
    return 0;
  }
  if(what == "loadorder")
  {
    if(sub_args.size() < 3)
      return cliError("Usage: limo list loadorder <app_id> <deployer_id>");
    const int deployer_id = parseId(sub_args[2]);
    if(deployer_id < 0)
      return cliError("deployer_id must be a non-negative integer.");
    const auto loadorder = am.getCliLoadorder(app_id, deployer_id);
    // Build an id->name map from the mod info so the load order is human-readable.
    std::map<int, std::string> names;
    for(const auto& mi : am.getCliModInfo(app_id))
      names[mi.mod.id] = mi.mod.name;
    if(json_out)
    {
      QJsonArray arr;
      for(int pos = 0; const auto& [mod_id, enabled] : loadorder)
      {
        QJsonObject o;
        o["position"] = pos++;
        o["id"] = mod_id;
        o["name"] = QString::fromStdString(names.count(mod_id) ? names[mod_id] : "");
        o["enabled"] = enabled;
        arr.append(o);
      }
      std::cout << qs(QJsonDocument(arr).toJson(QJsonDocument::Indented)) << "\n";
    }
    else
    {
      for(int pos = 0; const auto& [mod_id, enabled] : loadorder)
        std::cout << pos++ << ": [" << (enabled ? "x" : " ") << "] " << mod_id << " "
                  << (names.count(mod_id) ? names[mod_id] : "") << "\n";
    }
    return 0;
  }

  std::cerr << "Unknown list target '" << what << "'.\n"
            << "Valid targets: apps, deployers, mods, profiles, tools, tags, packs, loadorder\n";
  return 1;
}

// ---------------------------------------------------------------------------
// Subcommand: install
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: install <app_id> <archive> [--deployer <id>] [--name N] [--version V]
 *        [--root-level L]
 * \param sub_args Positional args after "install".
 * \param deployer_id Optional deployer id (-1 = add to all deployers).
 * \return Exit code.
 */
int cmdInstall(std::vector<std::string> sub_args, int deployer_id)
{
  const auto name_opt = takeOption(sub_args, "--name");
  const auto version_opt = takeOption(sub_args, "--version");
  const auto root_level_opt = takeOption(sub_args, "--root-level");
  if(sub_args.size() < 2)
  {
    std::cerr << "Usage: limo install <app_id> <archive> [--deployer <id>] [--name N] "
                 "[--version V] [--root-level L]\n";
    return 1;
  }

  int app_id = parseId(sub_args[0]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");

  const std::string archive = sub_args[1];
  if(!std::filesystem::exists(archive))
    return cliError("Archive not found: " + archive);

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");

  ImportModInfo info;
  info.app_id      = app_id;
  info.action_type = ImportModInfo::ActionType::install;
  info.local_source = archive;
  info.current_path = archive;
  info.installer    = "Simple Installer";
  // Default the display name to the archive's file-name stem so CLI-installed mods are not nameless.
  info.name = name_opt.value_or(std::filesystem::path(archive).stem().string());
  info.version = version_opt.value_or("1.0");
  info.root_level = root_level_opt ? std::max(0, parseId(*root_level_opt)) : 0;
  info.installer_flags = Installer::preserve_case | Installer::preserve_directories;

  // Determine deployers: either a specific one or all of them.
  if(deployer_id >= 0)
  {
    const auto deployer_names = am.getCliDeployerNames(app_id);
    if(deployer_id >= static_cast<int>(deployer_names.size()))
      return cliError("deployer_id " + std::to_string(deployer_id) + " is out of range.");
    info.deployers = { deployer_id };
  }
  else
  {
    const auto deployer_names = am.getCliDeployerNames(app_id);
    for(int d = 0; d < static_cast<int>(deployer_names.size()); d++)
      info.deployers.push_back(d);
  }

  am.installMod(app_id, info);
  std::cout << "Mod installed from '" << archive << "'.\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: uninstall
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: uninstall <app_id> <mod_id>
 * \param sub_args Positional args after "uninstall".
 * \return Exit code.
 */
int cmdUninstall(const std::vector<std::string>& sub_args)
{
  if(sub_args.size() < 2)
  {
    std::cerr << "Usage: limo uninstall <app_id> <mod_id>\n";
    return 1;
  }

  int app_id = parseId(sub_args[0]);
  int mod_id = parseId(sub_args[1]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");
  if(mod_id < 0)
    return cliError("mod_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");

  am.uninstallMods(app_id, { mod_id }, "");
  std::cout << "Mod " << mod_id << " uninstalled.\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: enable / disable
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: enable <app_id> <deployer_id> <mod_id>
 *                 disable <app_id> <deployer_id> <mod_id>
 * \param sub_args Positional args after the verb.
 * \param enabled  True for enable, false for disable.
 * \return Exit code.
 */
int cmdSetModStatus(const std::vector<std::string>& sub_args, bool enabled)
{
  const std::string verb = enabled ? "enable" : "disable";
  if(sub_args.size() < 3)
  {
    std::cerr << "Usage: limo " << verb << " <app_id> <deployer_id> <mod_id>\n";
    return 1;
  }

  int app_id      = parseId(sub_args[0]);
  int deployer_id = parseId(sub_args[1]);
  int mod_id      = parseId(sub_args[2]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");
  if(deployer_id < 0)
    return cliError("deployer_id must be a non-negative integer.");
  if(mod_id < 0)
    return cliError("mod_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");
  if(deployer_id >= static_cast<int>(am.getCliDeployerNames(app_id).size()))
    return cliError("deployer_id " + std::to_string(deployer_id) + " is out of range.");

  am.setModStatus(app_id, deployer_id, mod_id, enabled);
  std::cout << "Mod " << mod_id << " " << verb << "d in deployer " << deployer_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: set-profile
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: set-profile <app_id> <profile_id>
 * \param sub_args Positional args after "set-profile".
 * \return Exit code.
 */
int cmdSetProfile(const std::vector<std::string>& sub_args)
{
  if(sub_args.size() < 2)
  {
    std::cerr << "Usage: limo set-profile <app_id> <profile_id>\n";
    return 1;
  }

  int app_id    = parseId(sub_args[0]);
  int profile_id = parseId(sub_args[1]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");
  if(profile_id < 0)
    return cliError("profile_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");
  if(profile_id >= am.getNumProfiles(app_id))
    return cliError("profile_id " + std::to_string(profile_id) + " is out of range.");

  am.setProfile(app_id, profile_id);
  std::cout << "Profile " << profile_id << " set for app " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: deploy
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: deploy <app_id> [profile_id]
 * \param sub_args Positional args after "deploy".
 * \return Exit code.
 */
int cmdDeploy(const std::vector<std::string>& sub_args)
{
  if(sub_args.empty())
  {
    std::cerr << "Usage: limo deploy <app_id> [profile_id]\n";
    return 1;
  }

  int app_id = parseId(sub_args[0]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");

  // Optional profile argument.
  if(sub_args.size() >= 2)
  {
    int profile_id = parseId(sub_args[1]);
    if(profile_id < 0)
      return cliError("profile_id must be a non-negative integer.");
    if(profile_id >= am.getNumProfiles(app_id))
      return cliError("profile_id " + std::to_string(profile_id) + " is out of range.");
    am.setProfile(app_id, profile_id);
  }

  am.deployMods(app_id);
  std::cout << "Mods deployed for app " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: undeploy (fork #207)
// ---------------------------------------------------------------------------

/*!
 * \brief fork #207: Handles: undeploy <app_id> [profile_id]. Lets Steam Deck Game Mode
 * (or any local control surface / Decky plugin) revert a deployment without Desktop Mode.
 * \param sub_args Positional args after "undeploy".
 * \return Exit code.
 */
int cmdUnDeploy(const std::vector<std::string>& sub_args)
{
  if(sub_args.empty())
  {
    std::cerr << "Usage: limo undeploy <app_id> [profile_id]\n";
    return 1;
  }

  int app_id = parseId(sub_args[0]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");

  // Optional profile argument (mirrors deploy).
  if(sub_args.size() >= 2)
  {
    int profile_id = parseId(sub_args[1]);
    if(profile_id < 0)
      return cliError("profile_id must be a non-negative integer.");
    if(profile_id >= am.getNumProfiles(app_id))
      return cliError("profile_id " + std::to_string(profile_id) + " is out of range.");
    am.setProfile(app_id, profile_id);
  }

  am.unDeployMods(app_id);
  std::cout << "Mods undeployed for app " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand: status
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: status <app_id>
 * \param sub_args Positional args after "status".
 * \param json_out If true, emit JSON.
 * \return Exit code.
 */
int cmdStatus(const std::vector<std::string>& sub_args, bool json_out)
{
  if(sub_args.empty())
  {
    std::cerr << "Usage: limo status <app_id>\n";
    return 1;
  }

  int app_id = parseId(sub_args[0]);
  if(app_id < 0)
    return cliError("app_id must be a non-negative integer.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  if(app_id >= am.getNumApplications())
    return cliError("app_id " + std::to_string(app_id) + " is out of range.");

  if(json_out)
    std::cout << qs(statusToJson(am, app_id).toJson(QJsonDocument::Indented)) << "\n";
  else
    printStatus(am, app_id);
  return 0;
}

// ---------------------------------------------------------------------------
// Helpers shared by the operation subcommands below
// ---------------------------------------------------------------------------

/*! \brief Parses a deploy-mode token (hardlink/symlink/copy); std::nullopt if unrecognised. */
std::optional<Deployer::DeployMode> parseDeployMode(const std::string& s)
{
  if(s == "hardlink" || s == "hard" || s == "hard_link")
    return Deployer::hard_link;
  if(s == "symlink" || s == "sym" || s == "sym_link")
    return Deployer::sym_link;
  if(s == "copy")
    return Deployer::copy;
  return std::nullopt;
}

/*! \brief If \p flag is present in \p args, removes it and the following token and returns it. */
std::optional<std::string> takeOption(std::vector<std::string>& args, const std::string& flag)
{
  for(std::size_t i = 0; i < args.size(); i++)
  {
    if(args[i] == flag && i + 1 < args.size())
    {
      const std::string value = args[i + 1];
      args.erase(args.begin() + i, args.begin() + i + 2);
      return value;
    }
  }
  return std::nullopt;
}

/*! \brief If \p flag is present in \p args, removes it and returns true. */
bool takeFlag(std::vector<std::string>& args, const std::string& flag)
{
  const auto it = std::find(args.begin(), args.end(), flag);
  if(it == args.end())
    return false;
  args.erase(it);
  return true;
}

/*! \brief Parses a trailing list of non-negative ids beginning at index \p from. */
std::vector<int> parseIdList(const std::vector<std::string>& args, std::size_t from)
{
  std::vector<int> ids;
  for(std::size_t i = from; i < args.size(); i++)
  {
    const int v = parseId(args[i]);
    if(v >= 0)
      ids.push_back(v);
  }
  return ids;
}

/*!
 * \brief Constructs and initialises an ApplicationManager and validates \p app_id.
 * \return true on success; on failure prints an error and returns false.
 */
bool cliSetupApp(ApplicationManager& am, int app_id)
{
  am.enableExceptions(true);
  am.init();
  if(app_id < 0 || app_id >= am.getNumApplications())
  {
    cliError("app_id is out of range (have " + std::to_string(am.getNumApplications()) +
             " application(s)).");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Subcommands: applications
// ---------------------------------------------------------------------------

/*! \brief Handles: add-app <name> --staging <dir> [--command C] [--icon P] [--version V]
 *         [--steam-id N] */
int cmdAddApp(std::vector<std::string> args)
{
  const auto staging = takeOption(args, "--staging");
  const auto command = takeOption(args, "--command");
  const auto icon = takeOption(args, "--icon");
  const auto version = takeOption(args, "--version");
  const auto steam_id = takeOption(args, "--steam-id");
  if(args.empty())
    return cliError("Usage: limo add-app <name> --staging <dir> [--command C] [--icon P] "
                    "[--version V] [--steam-id N]");
  if(!staging)
    return cliError("add-app requires --staging <dir>.");

  ApplicationManager am;
  am.enableExceptions(true);
  am.init();

  EditApplicationInfo info;
  info.name = args[0];
  info.staging_dir = *staging;
  info.command = command.value_or("");
  info.icon_path = icon.value_or("");
  info.app_version = version.value_or("");
  info.steam_app_id = steam_id ? std::stol(*steam_id) : -1;
  info.move_staging_dir = false;
  am.addApplication(info);
  std::cout << "Application '" << info.name << "' added as id " << (am.getNumApplications() - 1)
            << ".\n";
  return 0;
}

/*! \brief Handles: remove-app <app_id> [--cleanup] */
int cmdRemoveApp(std::vector<std::string> args)
{
  const bool cleanup = takeFlag(args, "--cleanup");
  if(args.empty())
    return cliError("Usage: limo remove-app <app_id> [--cleanup]");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeApplication(app_id, cleanup);
  std::cout << "Application " << app_id << " removed" << (cleanup ? " (with data cleanup)" : "")
            << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: deployers
// ---------------------------------------------------------------------------

/*! \brief Handles: add-deployer <app_id> <type> <name> <target_dir> [--mode M] [--source-dir D] */
int cmdAddDeployer(std::vector<std::string> args)
{
  const auto mode = takeOption(args, "--mode");
  const auto source_dir = takeOption(args, "--source-dir");
  if(args.size() < 4)
    return cliError("Usage: limo add-deployer <app_id> <type> <name> <target_dir> "
                    "[--mode hardlink|symlink|copy] [--source-dir D]");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;

  EditDeployerInfo info;
  info.type = args[1];
  info.name = args[2];
  info.target_dir = args[3];
  info.source_dir = source_dir.value_or("");
  Deployer::DeployMode deploy_mode = Deployer::hard_link;
  if(mode)
  {
    const auto parsed = parseDeployMode(*mode);
    if(!parsed)
      return cliError("Unknown deploy mode '" + *mode + "' (use hardlink, symlink or copy).");
    deploy_mode = *parsed;
  }
  info.deploy_mode = deploy_mode;
  am.addDeployer(app_id, info);
  std::cout << "Deployer '" << info.name << "' (" << info.type << ") added to application " << app_id
            << ".\n";
  return 0;
}

/*! \brief Handles: remove-deployer <app_id> <deployer_id> [--cleanup] */
int cmdRemoveDeployer(std::vector<std::string> args)
{
  const bool cleanup = takeFlag(args, "--cleanup");
  if(args.size() < 2)
    return cliError("Usage: limo remove-deployer <app_id> <deployer_id> [--cleanup]");
  const int app_id = parseId(args[0]);
  const int deployer_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeDeployer(app_id, deployer_id, cleanup);
  std::cout << "Deployer " << deployer_id << " removed from application " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: profiles
// ---------------------------------------------------------------------------

/*! \brief Handles: add-profile <app_id> <name> [--version V] [--copy-from <profile_id>] */
int cmdAddProfile(std::vector<std::string> args)
{
  const auto version = takeOption(args, "--version");
  const auto copy_from = takeOption(args, "--copy-from");
  if(args.size() < 2)
    return cliError("Usage: limo add-profile <app_id> <name> [--version V] [--copy-from <id>]");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  EditProfileInfo info;
  info.name = args[1];
  info.app_version = version.value_or("");
  info.source = copy_from ? parseId(*copy_from) : -1;
  am.addProfile(app_id, info);
  std::cout << "Profile '" << info.name << "' added to application " << app_id << ".\n";
  return 0;
}

/*! \brief Handles: remove-profile <app_id> <profile_id> */
int cmdRemoveProfile(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo remove-profile <app_id> <profile_id>");
  const int app_id = parseId(args[0]);
  const int profile_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeProfile(app_id, profile_id);
  std::cout << "Profile " << profile_id << " removed from application " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: mod operations
// ---------------------------------------------------------------------------

/*! \brief Handles: rename-mod <app_id> <mod_id> <new_name> */
int cmdRenameMod(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo rename-mod <app_id> <mod_id> <new_name>");
  const int app_id = parseId(args[0]);
  const int mod_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.changeModName(app_id, mod_id, QString::fromStdString(args[2]));
  std::cout << "Mod " << mod_id << " renamed to '" << args[2] << "'.\n";
  return 0;
}

/*! \brief Handles: set-version <app_id> <mod_id> <version> */
int cmdSetVersion(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo set-version <app_id> <mod_id> <version>");
  const int app_id = parseId(args[0]);
  const int mod_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.changeModVersion(app_id, mod_id, QString::fromStdString(args[2]));
  std::cout << "Mod " << mod_id << " version set to '" << args[2] << "'.\n";
  return 0;
}

/*! \brief Handles: set-update-ignored <app_id> <mod_id> <0|1> */
int cmdSetUpdateIgnored(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo set-update-ignored <app_id> <mod_id> <0|1>");
  const int app_id = parseId(args[0]);
  const int mod_id = parseId(args[1]);
  const bool ignored = args[2] == "1" || args[2] == "true";
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.setUpdateIgnored(app_id, mod_id, ignored);
  std::cout << "Mod " << mod_id << " update-ignored set to " << (ignored ? "true" : "false")
            << ".\n";
  return 0;
}

/*! \brief Handles: export-mod <app_id> <mod_id> <target_path> */
int cmdExportMod(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo export-mod <app_id> <mod_id> <target_archive_path>");
  const int app_id = parseId(args[0]);
  const int mod_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.exportModArchive(app_id, mod_id, args[2]);
  std::cout << "Mod " << mod_id << " exported to '" << args[2] << "'.\n";
  return 0;
}

/*! \brief Handles: merge <app_id> <target_mod_id> <source_mod_id...> */
int cmdMerge(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo merge <app_id> <target_mod_id> <source_mod_id...>");
  const int app_id = parseId(args[0]);
  const int target = parseId(args[1]);
  const std::vector<int> sources = parseIdList(args, 2);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.mergeMods(app_id, sources, target);
  std::cout << "Merged " << sources.size() << " mod(s) into mod " << target << ".\n";
  return 0;
}

/*! \brief Handles: sort <app_id> <deployer_id> */
int cmdSort(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo sort <app_id> <deployer_id>");
  const int app_id = parseId(args[0]);
  const int deployer_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.sortModsByConflicts(app_id, deployer_id);
  std::cout << "Sorted mods by conflicts for deployer " << deployer_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: groups
// ---------------------------------------------------------------------------

/*! \brief Handles: create-group <app_id> <mod_id_1> <mod_id_2> */
int cmdCreateGroup(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo create-group <app_id> <mod_id_1> <mod_id_2>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.createGroup(app_id, parseId(args[1]), parseId(args[2]));
  std::cout << "Grouped mods " << args[1] << " and " << args[2] << ".\n";
  return 0;
}

/*! \brief Handles: remove-from-group <app_id> <mod_id> */
int cmdRemoveFromGroup(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo remove-from-group <app_id> <mod_id>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeModFromGroup(app_id, parseId(args[1]));
  std::cout << "Mod " << args[1] << " removed from its group.\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: tags
// ---------------------------------------------------------------------------

/*! \brief Handles: add-tag <app_id> <name> */
int cmdAddTag(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo add-tag <app_id> <name>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.addManualTag(app_id, QString::fromStdString(args[1]));
  std::cout << "Manual tag '" << args[1] << "' added.\n";
  return 0;
}

/*! \brief Handles: remove-tag <app_id> <name> */
int cmdRemoveTag(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo remove-tag <app_id> <name>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeManualTag(app_id, QString::fromStdString(args[1]));
  std::cout << "Manual tag '" << args[1] << "' removed.\n";
  return 0;
}

/*! \brief Handles: tag <app_id> <tag_name> <mod_id...> (or untag when \p add is false). */
int cmdTagMods(const std::vector<std::string>& args, bool add)
{
  const std::string verb = add ? "tag" : "untag";
  if(args.size() < 3)
    return cliError("Usage: limo " + verb + " <app_id> <tag_name> <mod_id...>");
  const int app_id = parseId(args[0]);
  const std::vector<int> mod_ids = parseIdList(args, 2);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  const QStringList tags{ QString::fromStdString(args[1]) };
  if(add)
    am.addTagsToMods(app_id, tags, mod_ids);
  else
    am.removeTagsFromMods(app_id, tags, mod_ids);
  std::cout << (add ? "Tagged " : "Untagged ") << mod_ids.size() << " mod(s) with '" << args[1]
            << "'.\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: modpacks
// ---------------------------------------------------------------------------

/*! \brief Handles: add-pack <app_id> <name> [notes...] */
int cmdAddPack(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo add-pack <app_id> <name> [notes...]");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  std::string notes;
  for(std::size_t i = 2; i < args.size(); i++)
    notes += (notes.empty() ? "" : " ") + args[i];
  am.addPack(app_id, QString::fromStdString(args[1]), QString::fromStdString(notes));
  std::cout << "Pack '" << args[1] << "' added.\n";
  return 0;
}

/*! \brief Handles: remove-pack <app_id> <name> */
int cmdRemovePack(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo remove-pack <app_id> <name>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removePack(app_id, QString::fromStdString(args[1]));
  std::cout << "Pack '" << args[1] << "' removed.\n";
  return 0;
}

/*! \brief Handles: rename-pack <app_id> <old_name> <new_name> */
int cmdRenamePack(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo rename-pack <app_id> <old_name> <new_name>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.renamePack(app_id, QString::fromStdString(args[1]), QString::fromStdString(args[2]));
  std::cout << "Pack '" << args[1] << "' renamed to '" << args[2] << "'.\n";
  return 0;
}

/*! \brief Handles: activate-pack/deactivate-pack <app_id> <name> */
int cmdSetPackActive(const std::vector<std::string>& args, bool active)
{
  const std::string verb = active ? "activate-pack" : "deactivate-pack";
  if(args.size() < 2)
    return cliError("Usage: limo " + verb + " <app_id> <name>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.setPackActive(app_id, QString::fromStdString(args[1]), active);
  std::cout << "Pack '" << args[1] << "' " << (active ? "activated" : "deactivated") << ".\n";
  return 0;
}

/*! \brief Handles: set-pack-mods <app_id> <name> <mod_id...> */
int cmdSetPackMods(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo set-pack-mods <app_id> <name> <mod_id...>");
  const int app_id = parseId(args[0]);
  const std::vector<int> mod_ids = parseIdList(args, 2);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  QList<int> qids;
  for(int id : mod_ids)
    qids.append(id);
  am.setPackMods(app_id, QString::fromStdString(args[1]), qids);
  std::cout << "Pack '" << args[1] << "' now contains " << mod_ids.size() << " mod(s).\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommands: tools
// ---------------------------------------------------------------------------

/*! \brief Handles: add-tool <app_id> <name> <command...> */
int cmdAddTool(const std::vector<std::string>& args)
{
  if(args.size() < 3)
    return cliError("Usage: limo add-tool <app_id> <name> <command...>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  std::string command;
  for(std::size_t i = 2; i < args.size(); i++)
    command += (command.empty() ? "" : " ") + args[i];
  am.addTool(app_id, Tool(args[1], "", command));
  std::cout << "Tool '" << args[1] << "' added.\n";
  return 0;
}

/*! \brief Handles: remove-tool <app_id> <tool_id> */
int cmdRemoveTool(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo remove-tool <app_id> <tool_id>");
  const int app_id = parseId(args[0]);
  const int tool_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.removeTool(app_id, tool_id);
  std::cout << "Tool " << tool_id << " removed.\n";
  return 0;
}

/*! \brief Handles: run-tool <app_id> <tool_id> */
int cmdRunTool(const std::vector<std::string>& args)
{
  if(args.size() < 2)
    return cliError("Usage: limo run-tool <app_id> <tool_id>");
  const int app_id = parseId(args[0]);
  const int tool_id = parseId(args[1]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  const AppInfo info = am.getCliAppInfo(app_id);
  if(tool_id < 0 || tool_id >= static_cast<int>(info.tools.size()))
    return cliError("tool_id is out of range (have " + std::to_string(info.tools.size()) +
                    " tool(s)).");
  const bool is_flatpak = std::filesystem::exists("/.flatpak-info");
  const std::string command = info.tools[tool_id].getCommand(is_flatpak);
  std::cout << "Running tool '" << info.tools[tool_id].getName() << "': " << command << "\n";
  return std::system(command.c_str()) == 0 ? 0 : 3;
}

// ---------------------------------------------------------------------------
// Subcommand: redeploy
// ---------------------------------------------------------------------------

/*! \brief Handles: redeploy <app_id> (force a purge + redeploy). */
int cmdRedeploy(const std::vector<std::string>& args)
{
  if(args.empty())
    return cliError("Usage: limo redeploy <app_id>");
  const int app_id = parseId(args[0]);
  ApplicationManager am;
  if(!cliSetupApp(am, app_id))
    return 1;
  am.forceRedeployMods(app_id);
  std::cout << "Mods force-redeployed for application " << app_id << ".\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Subcommand help text
// ---------------------------------------------------------------------------

void printCliHelp()
{
  std::cout <<
    "Usage: limo [options] [subcommand [args...]]\n"
    "\n"
    "Options:\n"
    "  -l, --list                 List all applications and their profiles (legacy)\n"
    "  -d, --deploy <app_id>      Deploy mods for an application (legacy; use with -p)\n"
    "  -p, --profile <profile_id> Set profile for legacy --deploy\n"
    "  -D, --debug                Show debug log messages\n"
    "  --json                     Machine-readable JSON output for list/status\n"
    "  --help, -h                 Show this help\n"
    "\n"
    "Read / inspect:\n"
    "  list apps | deployers <app_id> | mods <app_id> | profiles <app_id>\n"
    "  list tools <app_id> | tags <app_id> | packs <app_id>\n"
    "  list loadorder <app_id> <deployer_id>\n"
    "      List managed objects. With --json, output is machine-readable.\n"
    "  status <app_id>\n"
    "      Print a summary (profiles, deployers, mod counts).\n"
    "\n"
    "Applications:\n"
    "  add-app <name> --staging <dir> [--command C] [--icon P] [--version V] [--steam-id N]\n"
    "  remove-app <app_id> [--cleanup]\n"
    "\n"
    "Deployers & profiles:\n"
    "  add-deployer <app_id> <type> <name> <target_dir> [--mode hardlink|symlink|copy] [--source-dir D]\n"
    "  remove-deployer <app_id> <deployer_id> [--cleanup]\n"
    "  add-profile <app_id> <name> [--version V] [--copy-from <profile_id>]\n"
    "  remove-profile <app_id> <profile_id>\n"
    "  set-profile <app_id> <profile_id>\n"
    "\n"
    "Mods:\n"
    "  install <app_id> <archive> [--deployer <deployer_id>]\n"
    "  uninstall <app_id> <mod_id>\n"
    "  enable | disable <app_id> <deployer_id> <mod_id>\n"
    "  rename-mod <app_id> <mod_id> <new_name>\n"
    "  set-version <app_id> <mod_id> <version>\n"
    "  set-update-ignored <app_id> <mod_id> <0|1>\n"
    "  export-mod <app_id> <mod_id> <target_archive_path>\n"
    "  merge <app_id> <target_mod_id> <source_mod_id...>\n"
    "  sort <app_id> <deployer_id>\n"
    "  create-group <app_id> <mod_id_1> <mod_id_2>\n"
    "  remove-from-group <app_id> <mod_id>\n"
    "\n"
    "Tags:\n"
    "  add-tag | remove-tag <app_id> <name>\n"
    "  tag | untag <app_id> <tag_name> <mod_id...>\n"
    "\n"
    "Modpacks:\n"
    "  add-pack <app_id> <name> [notes...] | remove-pack <app_id> <name>\n"
    "  rename-pack <app_id> <old_name> <new_name>\n"
    "  activate-pack | deactivate-pack <app_id> <name>\n"
    "  set-pack-mods <app_id> <name> <mod_id...>\n"
    "\n"
    "Tools:\n"
    "  add-tool <app_id> <name> <command...> | remove-tool <app_id> <tool_id>\n"
    "  run-tool <app_id> <tool_id>\n"
    "\n"
    "Deploy:\n"
    "  deploy <app_id> [profile_id]\n"
    "      Deploy mods, optionally switching to the given profile first.\n"
    "  undeploy <app_id> [profile_id]\n"
    "      Undeploy mods, optionally switching to the given profile first.\n"
    "  redeploy <app_id>\n"
    "      Force a purge and redeploy.\n"
    "\n"
    "Exit codes: 0=ok  1=usage/arg error  2=another instance running  3=runtime error\n";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

/*!
 * \brief Main function of Limo.
 * \param argc Number of arguments passed to the application.
 * \param argv Array of arguments passed to the application.
 * \return 0: Application exited normally.
 *         1: An error occurred while parsing arguments.
 *         2: Execution canceled, another Limo instance is already running.
 *         3: A runtime error was reported by ApplicationManager.
 */
int main(int argc, char* argv[])
{
  QCoreApplication::setApplicationName("Limo");
  // Associate the running app with its .desktop file so Wayland/KDE shows the Limo icon in the
  // taskbar/switcher instead of the generic fallback (limo-app/limo#257). The flatpak ships the
  // reverse-DNS desktop id; the native install ships limo.desktop.
  QGuiApplication::setDesktopFileName(std::filesystem::exists("/.flatpak-info")
                                        ? "io.github.limo_app.limo"
                                        : "limo");
  QApplication app(argc, argv);

  // fork #22: Install the UI translation for the configured language as early as
  // possible so tr() lookups are localized.  Guarded so a missing .qm never
  // prevents startup (falls back to the English source strings).
  installTranslations(app);

  // limo-app/limo#230: Under Flatpak/KDE, QT_STYLE_OVERRIDE may name a style
  // (e.g. "kvantum") that isn't present in the runtime, causing Qt to silently
  // drop it and leave the app with a broken style and missing icons.  Detect
  // this and fall back to Breeze (if available) or Fusion so the UI stays sane.
  {
    const QString requested =
      QString::fromLocal8Bit(qgetenv("QT_STYLE_OVERRIDE")).trimmed();
    if(!requested.isEmpty())
    {
      const QStringList available = QStyleFactory::keys();
      // Case-insensitive search: Qt itself normalises names this way.
      const bool valid = std::any_of(
        available.cbegin(), available.cend(),
        [&](const QString& key) { return key.compare(requested, Qt::CaseInsensitive) == 0; });
      if(!valid)
      {
        const QStringList preferred = { "Breeze", "Fusion" };
        for(const QString& candidate : preferred)
        {
          if(available.contains(candidate, Qt::CaseInsensitive))
          {
            QApplication::setStyle(QStyleFactory::create(candidate));
            break;
          }
        }
      }
    }
  }

  // limo-app/limo#230: Ensure action icons render when no XDG icon theme is
  // configured (common in minimal Flatpak runtimes).
  if(QIcon::themeName().isEmpty())
    QIcon::setFallbackThemeName("breeze");

  // Apply the bundled palette-aware theme on top of the platform style.
  QFile style_file(":/styles/app.qss");
  if(style_file.open(QFile::ReadOnly | QFile::Text))
    app.setStyleSheet(QString::fromUtf8(style_file.readAll()));
  QIcon::setFallbackSearchPaths(
    QIcon::fallbackSearchPaths()
    << (std::filesystem::path(__FILE__).parent_path().parent_path() / "resources").c_str());

  // -------------------------------------------------------------------------
  // Pre-parse: collect raw args so we can intercept subcommands before Qt's
  // parser sees them (Qt's parser doesn't support subcommands).
  // -------------------------------------------------------------------------
  std::vector<std::string> raw_args;
  for(int i = 1; i < argc; i++)
    raw_args.emplace_back(argv[i]);

  // Flags that can appear anywhere on the command line.
  bool json_out    = false;
  bool debug_mode  = false;
  bool show_help   = false;
  int  deployer_opt = -1; // for "install --deployer <id>"

  // After stripping flags, the remaining tokens are the subcommand + its args.
  std::vector<std::string> pos_tokens;
  for(std::size_t i = 0; i < raw_args.size(); i++)
  {
    const std::string& a = raw_args[i];
    if(a == "--json")
      json_out = true;
    else if(a == "--debug" || a == "-D")
      debug_mode = true;
    else if(a == "--help" || a == "-h")
      show_help = true;
    else if(a == "--deployer" && i + 1 < raw_args.size())
    {
      deployer_opt = parseId(raw_args[++i]);
      if(deployer_opt < 0)
      {
        std::cerr << "Error: --deployer requires a non-negative integer.\n";
        return 1;
      }
    }
    else
      pos_tokens.push_back(a);
  }

  if(show_help && pos_tokens.empty())
  {
    printCliHelp();
    return 0;
  }

  // -------------------------------------------------------------------------
  // Detect whether the user is invoking a known headless subcommand.
  // If the first positional token is one of our subcommand names, handle it
  // entirely without touching Qt widgets.
  // -------------------------------------------------------------------------
  const std::vector<std::string> SUBCOMMANDS = {
    "list",        "install",         "uninstall",       "enable",
    "disable",     "set-profile",     "deploy",          "undeploy",
    "status",      "redeploy", // fork #207: undeploy
    "add-app",     "remove-app",      "add-deployer",    "remove-deployer",
    "add-profile", "remove-profile",  "rename-mod",      "set-version",
    "set-update-ignored", "export-mod", "merge",         "sort",
    "create-group", "remove-from-group", "add-tag",      "remove-tag",
    "tag",         "untag",           "add-pack",        "remove-pack",
    "rename-pack", "activate-pack",   "deactivate-pack", "set-pack-mods",
    "add-tool",    "remove-tool",     "run-tool"
  };

  bool is_subcommand = !pos_tokens.empty() &&
    std::find(SUBCOMMANDS.begin(), SUBCOMMANDS.end(), pos_tokens[0]) != SUBCOMMANDS.end();

  // Also treat legacy -l / --list and -d / --deploy as headless if present.
  bool legacy_list   = false;
  bool legacy_deploy = false;
  std::string legacy_deploy_app;
  std::string legacy_profile;
  std::string nxm_arg;

  if(!is_subcommand)
  {
    // Re-parse for legacy options using Qt's parser.
    QCommandLineParser parser;
    parser.setApplicationDescription("A simple tool for managing mods.");
    parser.addHelpOption();
    QCommandLineOption list_opt(QStringList() << "l" << "list",
                                "List all applications and their profiles.");
    QCommandLineOption deploy_opt(QStringList() << "d" << "deploy",
                                  "Deploy all mods for given <application>. Requires -p.",
                                  "application");
    QCommandLineOption profile_opt(QStringList() << "p" << "profile",
                                   "Set a <profile> to use for deployment.", "profile");
    QCommandLineOption debug_opt(QStringList() << "D" << "debug" << "Show debug log messages.");
    parser.addOption(list_opt);
    parser.addOption(deploy_opt);
    parser.addOption(profile_opt);
    parser.addOption(debug_opt);
    parser.addPositionalArgument("url", "Imports the mod at this URL.");
    parser.process(app);

    debug_mode = parser.isSet(debug_opt);

    if(parser.isSet(list_opt))
    {
      legacy_list = true;
    }
    else if(parser.isSet(deploy_opt))
    {
      legacy_deploy     = true;
      legacy_deploy_app = qs(parser.value(deploy_opt));
      if(parser.isSet(profile_opt))
        legacy_profile = qs(parser.value(profile_opt));
    }
    else
    {
      const auto pa = parser.positionalArguments();
      if(!pa.empty())
      {
        nxm_arg = qs(pa[0]);
        // Strip only balanced surrounding double quotes; leaving a single
        // unmatched quote in place would otherwise corrupt the URL.
        if(nxm_arg.size() >= 2 && nxm_arg.front() == '"' && nxm_arg.back() == '"')
        {
          nxm_arg.erase(nxm_arg.size() - 1, 1);
          nxm_arg.erase(0, 1);
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // Legacy --list
  // -------------------------------------------------------------------------
  if(legacy_list)
  {
    try
    {
      ApplicationManager am;
      am.enableExceptions(true);
      am.init();
      if(json_out)
        std::cout << qs(appsToJson(am).toJson(QJsonDocument::Indented)) << "\n";
      else
        std::cout << am.toString();
    }
    catch(const std::exception& e)
    {
      std::cerr << "Error: " << e.what() << "\n";
      return 3;
    }
    return 0;
  }

  // -------------------------------------------------------------------------
  // Legacy --deploy
  // -------------------------------------------------------------------------
  if(legacy_deploy)
  {
    bool is_int;
    QString qapp  = QString::fromStdString(legacy_deploy_app);
    int app_id    = qapp.toInt(&is_int);
    if(!is_int)
    {
      std::cerr << "Error: Specify the application id, '" << legacy_deploy_app
                << "' is not a number.\n";
      return 1;
    }
    if(legacy_profile.empty())
    {
      std::cerr << "Error: Missing profile id.\n";
      return 1;
    }
    QString qprof = QString::fromStdString(legacy_profile);
    int profile_id = qprof.toInt(&is_int);
    if(!is_int)
    {
      std::cerr << "Error: Specify the profile id, '" << legacy_profile
                << "' is not a number.\n";
      return 1;
    }
    try
    {
      ApplicationManager am;
      am.enableExceptions(true);
      am.init();
      if(app_id < 0 || app_id >= am.getNumApplications())
      {
        std::cerr << "Error: Application index out of bounds.\n";
        return 1;
      }
      if(profile_id < 0 || profile_id >= am.getNumProfiles(app_id))
      {
        std::cerr << "Error: Profile index out of bounds.\n";
        return 1;
      }
      am.setProfile(app_id, profile_id);
      am.deployMods(app_id);
    }
    catch(const std::exception& e)
    {
      std::cerr << "Error: " << e.what() << "\n";
      return 3;
    }
    return 0;
  }

  // -------------------------------------------------------------------------
  // Headless subcommands (fork #44)
  // -------------------------------------------------------------------------
  if(is_subcommand)
  {
    const std::string& cmd   = pos_tokens[0];
    std::vector<std::string> cmd_args(pos_tokens.begin() + 1, pos_tokens.end());

    try
    {
      if(cmd == "list")
        return cmdList(cmd_args, json_out);
      if(cmd == "install")
        return cmdInstall(cmd_args, deployer_opt);
      if(cmd == "uninstall")
        return cmdUninstall(cmd_args);
      if(cmd == "enable")
        return cmdSetModStatus(cmd_args, true);
      if(cmd == "disable")
        return cmdSetModStatus(cmd_args, false);
      if(cmd == "set-profile")
        return cmdSetProfile(cmd_args);
      if(cmd == "deploy")
        return cmdDeploy(cmd_args);
      if(cmd == "undeploy") // fork #207
        return cmdUnDeploy(cmd_args);
      if(cmd == "status")
        return cmdStatus(cmd_args, json_out);
      if(cmd == "redeploy")
        return cmdRedeploy(cmd_args);
      if(cmd == "add-app")
        return cmdAddApp(cmd_args);
      if(cmd == "remove-app")
        return cmdRemoveApp(cmd_args);
      if(cmd == "add-deployer")
        return cmdAddDeployer(cmd_args);
      if(cmd == "remove-deployer")
        return cmdRemoveDeployer(cmd_args);
      if(cmd == "add-profile")
        return cmdAddProfile(cmd_args);
      if(cmd == "remove-profile")
        return cmdRemoveProfile(cmd_args);
      if(cmd == "rename-mod")
        return cmdRenameMod(cmd_args);
      if(cmd == "set-version")
        return cmdSetVersion(cmd_args);
      if(cmd == "set-update-ignored")
        return cmdSetUpdateIgnored(cmd_args);
      if(cmd == "export-mod")
        return cmdExportMod(cmd_args);
      if(cmd == "merge")
        return cmdMerge(cmd_args);
      if(cmd == "sort")
        return cmdSort(cmd_args);
      if(cmd == "create-group")
        return cmdCreateGroup(cmd_args);
      if(cmd == "remove-from-group")
        return cmdRemoveFromGroup(cmd_args);
      if(cmd == "add-tag")
        return cmdAddTag(cmd_args);
      if(cmd == "remove-tag")
        return cmdRemoveTag(cmd_args);
      if(cmd == "tag")
        return cmdTagMods(cmd_args, true);
      if(cmd == "untag")
        return cmdTagMods(cmd_args, false);
      if(cmd == "add-pack")
        return cmdAddPack(cmd_args);
      if(cmd == "remove-pack")
        return cmdRemovePack(cmd_args);
      if(cmd == "rename-pack")
        return cmdRenamePack(cmd_args);
      if(cmd == "activate-pack")
        return cmdSetPackActive(cmd_args, true);
      if(cmd == "deactivate-pack")
        return cmdSetPackActive(cmd_args, false);
      if(cmd == "set-pack-mods")
        return cmdSetPackMods(cmd_args);
      if(cmd == "add-tool")
        return cmdAddTool(cmd_args);
      if(cmd == "remove-tool")
        return cmdRemoveTool(cmd_args);
      if(cmd == "run-tool")
        return cmdRunTool(cmd_args);
    }
    catch(const std::exception& e)
    {
      std::cerr << "Error: " << e.what() << "\n";
      return 3;
    }
    // Should never reach here because is_subcommand guards the known list.
    std::cerr << "Unknown subcommand '" << cmd << "'.\n";
    return 1;
  }

  // -------------------------------------------------------------------------
  // GUI path
  // -------------------------------------------------------------------------
  IpcClient client;
  if(client.connect())
  {
    if(nxm_arg.empty())
    {
      client.sendString("Started");
      std::cout << "Another instance is already running. Sending arguments...\n";
      return 2;
    }
    std::regex nxm_regex(R"(nxm:\/\/(.*)\/mods\/(\d+)\/files\/\d+\?.*)");
    std::smatch match;
    if(std::regex_match(nxm_arg, match, nxm_regex))
      client.sendString(nxm_arg);
    return 0;
  }

  app.setWindowIcon(QIcon(":/logo.png"));
  {
    QSettings settings(QCoreApplication::applicationName());
    applyTheme(app, settings.value("theme", 0).toInt());
  }
  MainWindow w;
  w.setDebugMode(debug_mode);
  if(!nxm_arg.empty())
    w.setCmdArgument(nxm_arg);
  emit w.getApplicationNames(false);
  w.show();
  w.initChangelog();
  return app.exec();
}
