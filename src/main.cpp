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

#include "ui/applicationmanager.h"
#include "ui/ipcclient.h"
#include "ui/mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>


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

  std::cerr << "Unknown list target '" << what << "'.\n"
            << "Valid targets: apps, deployers, mods, profiles\n";
  return 1;
}

// ---------------------------------------------------------------------------
// Subcommand: install
// ---------------------------------------------------------------------------

/*!
 * \brief Handles: install <app_id> <archive> [--deployer <id>]
 * \param sub_args Positional args after "install".
 * \param deployer_id Optional deployer id (-1 = add to all deployers).
 * \return Exit code.
 */
int cmdInstall(const std::vector<std::string>& sub_args, int deployer_id)
{
  if(sub_args.size() < 2)
  {
    std::cerr << "Usage: limo install <app_id> <archive> [--deployer <id>]\n";
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
    "Subcommands:\n"
    "  list apps\n"
    "      List all managed applications.\n"
    "  list deployers <app_id>\n"
    "      List deployers for an application.\n"
    "  list mods <app_id>\n"
    "      List installed mods for an application.\n"
    "  list profiles <app_id>\n"
    "      List profiles for an application.\n"
    "  install <app_id> <archive> [--deployer <deployer_id>]\n"
    "      Install a local archive as a mod. Adds to all deployers unless\n"
    "      --deployer is given.\n"
    "  uninstall <app_id> <mod_id>\n"
    "      Uninstall (remove) a mod.\n"
    "  enable <app_id> <deployer_id> <mod_id>\n"
    "      Enable a mod in a deployer.\n"
    "  disable <app_id> <deployer_id> <mod_id>\n"
    "      Disable a mod in a deployer.\n"
    "  set-profile <app_id> <profile_id>\n"
    "      Switch the active profile.\n"
    "  deploy <app_id> [profile_id]\n"
    "      Deploy mods, optionally switching to the given profile first.\n"
    "  status <app_id>\n"
    "      Print a summary (profiles, deployers, mod counts).\n"
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
    "list", "install", "uninstall", "enable", "disable", "set-profile", "deploy", "status"
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
        if(nxm_arg.starts_with('"'))  nxm_arg.erase(0, 1);
        if(nxm_arg.ends_with('"'))    nxm_arg.erase(nxm_arg.size() - 1, 1);
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
      if(cmd == "status")
        return cmdStatus(cmd_args, json_out);
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
    std::regex nxm_regex(R"(nxm:\/\/.*\mods\/\d+\/files\/\d+\?.*)");
    std::smatch match;
    if(std::regex_match(nxm_arg, match, nxm_regex))
      client.sendString(nxm_arg);
    return 0;
  }

  app.setWindowIcon(QIcon(":/logo.png"));
  MainWindow w;
  w.setDebugMode(debug_mode);
  if(!nxm_arg.empty())
    w.setCmdArgument(nxm_arg);
  emit w.getApplicationNames(false);
  w.show();
  w.initChangelog();
  return app.exec();
}
