#include "tool.h"
#include <cstdlib>
#include <format>
#include <fstream>
#include <ranges>
#include <string_view>
#include <system_error>
#include <string>
#include <vector>

namespace sfs = std::filesystem;

/*!
 * \brief POSIX single-quote escapes \p s so it is safe to embed in a shell
 *        command constructed for popen(3).  Every single-quote in the input is
 *        replaced with the sequence '\'' (end quote, literal single-quote,
 *        reopen quote) and the whole string is wrapped in single quotes.
 *        Single-quoting prevents the shell from expanding $, backticks, \, and
 *        double-quotes inside the value.
 */
static std::string shellEscape(const std::string& s)
{
  std::string out = "'";
  for(char c : s)
  {
    if(c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}


Tool::Tool(const std::string& name, const sfs::path& icon_path, const std::string& command) :
  name_(name), icon_path_(icon_path), runtime_(native), command_overwrite_(command)
{}

Tool::Tool(const std::string& name,
           const sfs::path& icon_path,
           const sfs::path& executable_path,
           const sfs::path& working_directory,
           const std::map<std::string, std::string>& environment_variables,
           const std::string& arguments) :
  name_(name), icon_path_(icon_path), executable_path_(executable_path), runtime_(native),
  working_directory_(working_directory), environment_variables_(environment_variables),
  arguments_(arguments)
{}

Tool::Tool(const std::string& name,
           const sfs::path& icon_path,
           const sfs::path& executable_path,
           const sfs::path& prefix_path,
           const sfs::path& working_directory,
           const std::map<std::string, std::string>& environment_variables,
           const std::string& arguments) :
  name_(name), icon_path_(icon_path), executable_path_(executable_path), runtime_(wine),
  prefix_path_(prefix_path), working_directory_(working_directory),
  environment_variables_(environment_variables), arguments_(arguments)
{}

Tool::Tool(const std::string& name,
           const sfs::path& icon_path,
           const sfs::path& executable_path,
           const sfs::path& prefix_path,
           const std::string& game_id,
           const sfs::path& working_directory,
           const std::map<std::string, std::string>& environment_variables,
           const std::string& arguments) :
  name_(name), icon_path_(icon_path), executable_path_(executable_path), runtime_(umu),
  prefix_path_(prefix_path), umu_game_id_(game_id), working_directory_(working_directory),
  environment_variables_(environment_variables), arguments_(arguments)
{}

Tool::Tool(const std::string& name,
           const sfs::path& icon_path,
           const sfs::path& executable_path,
           bool use_flatpak_protontricks,
           int steam_app_id,
           const sfs::path& working_directory,
           const std::map<std::string, std::string>& environment_variables,
           const std::string& arguments,
           const std::string& protontricks_arguments) :
  name_(name), icon_path_(icon_path), executable_path_(executable_path), runtime_(protontricks),
  use_flatpak_runtime_(use_flatpak_protontricks), steam_app_id_(steam_app_id),
  working_directory_(working_directory), environment_variables_(environment_variables),
  arguments_(arguments), protontricks_arguments_(protontricks_arguments)
{}

Tool::Tool(const std::string& name,
           const sfs::path& icon_path,
           int steam_app_id,
           bool use_flatpak_steam) :
  name_(name), icon_path_(icon_path), runtime_(steam), steam_app_id_(steam_app_id),
  use_flatpak_runtime_(use_flatpak_steam)
{}

Tool::Tool(const Json::Value& json_object)
{
  if(!json_object.isMember("use_flatpak_runtime")) // Initialize from old format
  {
    name_ = json_object["name"].asString();
    icon_path_ = "";
    command_overwrite_ = json_object["command"].asString();
  }
  else
  {
    name_ = json_object["name"].asString();
    icon_path_ = json_object["icon_path"].asString();
    executable_path_ = json_object["executable_path"].asString();
    runtime_ = static_cast<Runtime>(json_object["runtime"].asInt());
    use_flatpak_runtime_ = json_object["use_flatpak_runtime"].asBool();
    prefix_path_ = json_object["prefix_path"].asString();
    steam_app_id_ = json_object["steam_app_id"].asInt();
    working_directory_ = json_object["working_directory"].asString();
    for(int i = 0; i < json_object["environment_variables"].size(); i++)
    {
      environment_variables_[json_object["environment_variables"][i]["variable"].asString()] =
        json_object["environment_variables"][i]["value"].asString();
    }
    arguments_ = json_object["arguments"].asString();
    protontricks_arguments_ = json_object["protontricks_arguments"].asString();
    if(json_object.isMember("umu_game_id"))
      umu_game_id_ = json_object["umu_game_id"].asString();
    command_overwrite_ = json_object["command"].asString();
  }
}

sfs::path Tool::detectProtonPrefix(int steam_app_id)
{
  const char* home_env = std::getenv("HOME");
  if(home_env == nullptr || *home_env == '\0')
    return {};
  const sfs::path home(home_env);

  // Well known Steam library roots (a "root" is a directory containing a steamapps/ subdir).
  std::vector<sfs::path> library_roots{
    home / ".steam" / "steam",
    home / ".local" / "share" / "Steam",
    home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam"
  };

  // Cheaply parse each root's libraryfolders.vdf for additional library paths.
  // The relevant lines look like:   "path"   "/some/library"
  const std::size_t base_root_count = library_roots.size();
  for(std::size_t i = 0; i < base_root_count; i++)
  {
    const sfs::path vdf_path = library_roots[i] / "steamapps" / "libraryfolders.vdf";
    std::error_code ec;
    if(!sfs::exists(vdf_path, ec))
      continue;
    std::ifstream vdf(vdf_path);
    if(!vdf)
      continue;
    std::string line;
    while(std::getline(vdf, line))
    {
      const auto key_start = line.find("\"path\"");
      if(key_start == std::string::npos)
        continue;
      const auto value_open = line.find('"', key_start + 6);
      if(value_open == std::string::npos)
        continue;
      const auto value_close = line.find('"', value_open + 1);
      if(value_close == std::string::npos)
        continue;
      std::string value = line.substr(value_open + 1, value_close - value_open - 1);
      // VDF escapes backslashes; convert "\\" to "\".
      std::string unescaped;
      for(std::size_t c = 0; c < value.size(); c++)
      {
        if(value[c] == '\\' && c + 1 < value.size())
          c++;
        unescaped += value[c];
      }
      if(!unescaped.empty())
        library_roots.emplace_back(unescaped);
    }
  }

  for(const auto& root : library_roots)
  {
    const sfs::path pfx =
      root / "steamapps" / "compatdata" / std::to_string(steam_app_id) / "pfx";
    std::error_code ec;
    if(sfs::exists(pfx, ec))
      return pfx;
  }

  return {};
}

sfs::path Tool::protonUserDir(int steam_app_id)
{
  const sfs::path pfx = detectProtonPrefix(steam_app_id);
  if(pfx.empty())
    return {};
  return pfx / "drive_c" / "users" / "steamuser";
}

std::string Tool::getCommand(bool is_flatpak) const
{
  if(!command_overwrite_.empty())
    return is_flatpak ? "flatpak-spawn --host " + command_overwrite_
                      : command_overwrite_;

  std::string command;
  if(is_flatpak)
    command = "flatpak-spawn --host ";

  if(runtime_ == steam)
  {
    if(!use_flatpak_runtime_)
      command += "steam ";
    else
      command += "flatpak run com.valvesoftware.Steam ";
    return command + std::format("-applaunch {}", steam_app_id_);
  }

  if(!working_directory_.empty())
  {
    if(is_flatpak)
      command += "--directory=" + shellEscape(working_directory_.string());
    else
      command += "cd " + shellEscape(working_directory_.string()) + ";";
  }

  appendEnvironmentVariables(command, environment_variables_, is_flatpak);
  if(runtime_ == wine && !prefix_path_.empty())
    appendEnvironmentVariables(command, { { "WINEPREFIX", prefix_path_.string() } }, is_flatpak);
  if(runtime_ == umu)
  {
    if(!prefix_path_.empty())
      appendEnvironmentVariables(command, { { "WINEPREFIX", prefix_path_.string() } }, is_flatpak);
    appendEnvironmentVariables(
      command, { { "GAMEID", umu_game_id_.empty() ? "0" : umu_game_id_ } }, is_flatpak);
  }

  if(!command.empty() && runtime_ != native)
    command += " ";
  if(runtime_ == wine)
    command += "wine";
  else if(runtime_ == umu)
    command += "umu-run";
  else if(runtime_ == protontricks)
  {
    if(use_flatpak_runtime_)
      command += "flatpak run --command=protontricks-launch com.github.Matoking.protontricks ";
    else
      command += "protontricks-launch ";
    command += "--appid " + std::to_string(steam_app_id_);
    if(!protontricks_arguments_.empty())
      command += " " + protontricks_arguments_;
  }

  if(!command.empty())
    command += " ";
  command += shellEscape(executable_path_.string());

  if(!arguments_.empty())
    command += " " + arguments_;

  return command;
}

Json::Value Tool::toJson() const
{
  Json::Value json_object;
  json_object["name"] = name_;
  json_object["icon_path"] = icon_path_.string();
  json_object["executable_path"] = executable_path_.string();
  json_object["runtime"] = static_cast<int>(runtime_);
  json_object["use_flatpak_runtime"] = use_flatpak_runtime_;
  json_object["prefix_path"] = prefix_path_.string();
  json_object["steam_app_id"] = steam_app_id_;
  json_object["working_directory"] = working_directory_.string();
  for(const auto& [i, pair] : std::views::enumerate(environment_variables_))
  {
    const auto& [variable, value] = pair;
    json_object["environment_variables"][(int)i]["variable"] = variable;
    json_object["environment_variables"][(int)i]["value"] = value;
  }
  json_object["arguments"] = arguments_;
  json_object["protontricks_arguments"] = protontricks_arguments_;
  json_object["umu_game_id"] = umu_game_id_;
  json_object["command"] = command_overwrite_;
  return json_object;
}

std::string Tool::getName() const
{
  return name_;
}

sfs::path Tool::getIconPath() const
{
  return icon_path_;
}

sfs::path Tool::getExecutablePath() const
{
  return executable_path_;
}

Tool::Runtime Tool::getRuntime() const
{
  return runtime_;
}

bool Tool::usesFlatpakRuntime() const
{
  return use_flatpak_runtime_;
}

sfs::path Tool::getPrefixPath() const
{
  return prefix_path_;
}

int Tool::getSteamAppId() const
{
  return steam_app_id_;
}

sfs::path Tool::getWorkingDirectory() const
{
  return working_directory_;
}

const std::map<std::string, std::string> Tool::getEnvironmentVariables() const
{
  return environment_variables_;
}

std::string Tool::getArguments() const
{
  return arguments_;
}

std::string Tool::getProtontricksArguments() const
{
  return protontricks_arguments_;
}

std::string Tool::getUmuGameId() const
{
  return umu_game_id_;
}

std::string Tool::getCommandOverwrite() const
{
  return command_overwrite_;
}

bool Tool::umuLauncherAvailable()
{
  // Cache the result: detection only needs to happen once per process.
  static const bool available = []
  {
    const std::string executable = "umu-run";

    // Search every directory listed on PATH.
    if(const char* const path_env = std::getenv("PATH"); path_env != nullptr)
    {
      std::string_view paths(path_env);
      while(!paths.empty())
      {
        const auto separator = paths.find(':');
        const std::string_view entry =
          separator == std::string_view::npos ? paths : paths.substr(0, separator);
        if(!entry.empty())
        {
          std::error_code ec;
          const sfs::path candidate = sfs::path(entry) / executable;
          if(sfs::exists(candidate, ec) && !ec)
            return true;
        }
        if(separator == std::string_view::npos)
          break;
        paths.remove_prefix(separator + 1);
      }
    }

    // Fall back to a few common install locations.
    const sfs::path common_locations[] = {
      "/usr/bin/umu-run",
      "/usr/local/bin/umu-run",
      "/app/bin/umu-run",
    };
    for(const auto& candidate : common_locations)
    {
      std::error_code ec;
      if(sfs::exists(candidate, ec) && !ec)
        return true;
    }

    return false;
  }();
  return available;
}

void Tool::appendEnvironmentVariables(
  std::string& command,
  const std::map<std::string, std::string>& environment_variables,
  bool is_flatpak) const
{
  for(const auto& [variable, value] : environment_variables)
  {
    if(!command.empty())
      command += " ";
    if(is_flatpak)
      command += "--env=";
    command += variable + "=" + shellEscape(value);
  }
}

