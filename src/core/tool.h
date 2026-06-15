/*!
 * \file tool.h
 * \brief Header for the Tool class.
 */

#pragma once

#include <filesystem>
#include <json/json.h>
#include <map>


/*!
 * \brief Represents a third party tool to be run from within Limo.
 */
class Tool
{
public:
  /*! \brief Describes how the tool is to be run. */
  enum Runtime
  {
    /*! \brief Tool is to be run directly. */
    native,
    /*! \brief Tool is to be run through wine. */
    wine,
    /*! \brief Tool is to be run through proton by calling protontricks. */
    protontricks,
    /*! \brief Tool is a steam app. */
    steam,
    /*!
     * \brief Tool is to be run through Proton via umu-launcher (umu-run).
     * \note Added after the original runtimes; kept last to preserve the
     * integer values used for JSON (de)serialization of existing runtimes.
     */
    umu
  };

  /*!
   * \brief Checks whether umu-launcher (umu-run) is available on this system.
   *
   * The executable is searched for on PATH and in a few common install
   * locations using std::filesystem. The result is cached after the first call.
   * \return True if umu-run was found, else false.
   */
  static bool umuLauncherAvailable();

  /*! \brief Default constructor */
  Tool() = default;
  /*!
   * \brief Constructs a tool that runs the given command directly.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param command Command used to run the tool.
   */
  Tool(const std::string& name, const std::filesystem::path& icon_path, const std::string& command);
  /*!
   * \brief Constructs a tool using the native runtime.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param executable_path Path to the tool's executable.
   * \param working_directory Working directory in which to run the command.
   * \param environment_variables Maps environment variables to their values.
   * \param arguments Arguments to be passed to the executable.
   */
  Tool(const std::string& name,
       const std::filesystem::path& icon_path,
       const std::filesystem::path& executable_path,
       const std::filesystem::path& working_directory,
       const std::map<std::string, std::string>& environment_variables,
       const std::string& arguments);
  /*!
   * \brief Constructs a tool using the wine runtime.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param executable_path Path to the tool's executable.
   * \param prefix_path Path to the wine prefix_path.
   * \param working_directory Working directory in which to run the command.
   * \param environment_variables Maps environment variables to their values.
   * \param arguments Arguments to be passed to the executable.
   */
  Tool(const std::string& name,
       const std::filesystem::path& icon_path,
       const std::filesystem::path& executable_path,
       const std::filesystem::path& prefix_path,
       const std::filesystem::path& working_directory,
       const std::map<std::string, std::string>& environment_variables,
       const std::string& arguments);
  /*!
   * \brief Constructs a tool using the umu-launcher runtime.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param executable_path Path to the tool's executable.
   * \param prefix_path Path to the Proton prefix used by umu-launcher.
   * \param game_id Value passed to umu-launcher as GAMEID (use "0" if unknown).
   * \param working_directory Working directory in which to run the command.
   * \param environment_variables Maps environment variables to their values.
   * \param arguments Arguments to be passed to the executable.
   */
  Tool(const std::string& name,
       const std::filesystem::path& icon_path,
       const std::filesystem::path& executable_path,
       const std::filesystem::path& prefix_path,
       const std::string& game_id,
       const std::filesystem::path& working_directory,
       const std::map<std::string, std::string>& environment_variables,
       const std::string& arguments);
  /*!
   * \brief Constructs a tool using the protontricks runtime.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param executable_path Path to the tool's executable.
   * \param use_flatpak_protontricks Whether to use flatpak protontricks.
   * \param steam_app_id ID of the steam app containing the proton prefix.
   * \param working_directory Working directory in which to run the command.
   * \param environment_variables Maps environment variables to their values.
   * \param arguments Arguments to be passed to the executable.
   * \param protontricks_arguments Arguments to be passed to protontricks.
   */
  Tool(const std::string& name,
       const std::filesystem::path& icon_path,
       const std::filesystem::path& executable_path,
       bool use_flatpak_protontricks,
       int steam_app_id,
       const std::filesystem::path& working_directory,
       const std::map<std::string, std::string>& environment_variables,
       const std::string& arguments,
       const std::string& protontricks_arguments);
  /*!
   * \brief Constructs a tool using the steam runtime.
   * \param name Name of the tool.
   * \param icon_path Path to the tool's icon.
   * \param steam_app_id ID of the steam app to run.
   * \param use_flatpak_steam If true: Use the flatpak version of steam.
   */
  Tool(const std::string& name,
       const std::filesystem::path& icon_path,
       int steam_app_id,
       bool use_flatpak_steam);
  /*!
   * \brief Constructs a new Tool from data contained in the given JSON object.
   * \param json_object Source JSON object.
   */
  Tool(const Json::Value& json_object);

  /*!
   * \brief Constructs the command used to run this tool and returns it.
   * \param is_flatpak If true: The tool is to be run from within a flatpak sandbox.
   * \return The command.
   */
  std::string getCommand(bool is_flatpak) const;
  /*!
   * \brief Serializes this object to JSON.
   * \return The resulting JSON object.
   */
  Json::Value toJson() const;
  /*!
   * \brief Return this tool's name.
   * \return The name.
   */
  std::string getName() const;
  /*!
   * \brief Returns the path to an icon representing the tool.
   * \return The path.
   */
  std::filesystem::path getIconPath() const;
  /*!
   * \brief Returns the path to the executable of the tool.
   * \return The path.
   */
  std::filesystem::path getExecutablePath() const;
  /*!
   * \brief Returns the runtime used to run the tool.
   * \return The runtime.
   */
  Runtime getRuntime() const;
  /*!
   * \brief Returns true if flatpak version of protontricks or steam is used.
   * \return The status.
   */
  bool usesFlatpakRuntime() const;
  /*!
   * \brief Returns the path to the wine prefix.
   * \return The path.
   */
  std::filesystem::path getPrefixPath() const;
  /*!
   * \brief Returns the ID of the steam app containing the proton prefix.
   * \return The ID.
   */
  int getSteamAppId() const;
  /*!
   * \brief Returns the working directory in which to run the command.
   * \return The path.
   */
  std::filesystem::path getWorkingDirectory() const;
  /*!
   * \brief Returns a map containing environment variables and their values.
   * \return The map.
   */
  std::map<std::string, std::string> const getEnvironmentVariables() const;
  /*!
   * \brief Returns the arguments to be passed to the executable.
   * \return The arguments.
   */
  std::string getArguments() const;
  /*!
   * \brief Returns the arguments to be passed to protontricks.
   * \return The arguments.
   */
  std::string getProtontricksArguments() const;
  /*!
   * \brief Returns the GAMEID passed to umu-launcher.
   * \return The GAMEID.
   */
  std::string getUmuGameId() const;
  /*!
   * \brief Returns the overwrite command.
   * If this is not empty: Ignore all other settings and run this command directly.
   * \return The overwrite command.
   */
  std::string getCommandOverwrite() const;

  /*!
   * \brief Attempts to locate the Proton compatibility prefix for the given Steam app.
   *
   * Searches the well known Steam library roots (the default install at
   * \c ~/.steam/steam and \c ~/.local/share/Steam, the flatpak install at
   * \c ~/.var/app/com.valvesoftware.Steam/.local/share/Steam, plus any additional
   * libraries listed in each root's \c steamapps/libraryfolders.vdf) for
   * \c steamapps/compatdata/<appid>/pfx.
   * \param steam_app_id ID of the steam app whose Proton prefix should be located.
   * \return The path to the first existing prefix, or an empty path if none is found.
   */
  static std::filesystem::path detectProtonPrefix(int steam_app_id);
  /*!
   * \brief Returns the Windows user directory inside the Proton prefix of the given Steam app.
   *
   * This is the directory under which the Windows user folders (Documents, AppData, ...)
   * holding per-game config and save files live, i.e. \c <pfx>/drive_c/users/steamuser.
   * \param steam_app_id ID of the steam app whose Proton prefix should be located.
   * \return The path to the steamuser directory, or an empty path if the prefix is not found.
   */
  static std::filesystem::path protonUserDir(int steam_app_id);

private:
  /*! \brief Name of the tool. */
  std::string name_;
  /*! \brief Path to an icon representing the tool. */
  std::filesystem::path icon_path_;
  /*! \brief Path to the executable of the tool. */
  std::filesystem::path executable_path_;
  /*! \brief Runtime used to run the tool. */
  Runtime runtime_;
  /*! \brief If runtime is proton or steam: Whether to use the flatpak version. */
  bool use_flatpak_runtime_;
  /*! \brief If runtime is wine: Path to the wine prefix. */
  std::filesystem::path prefix_path_;
  /*!
   *  \brief If runtime is proton: ID of the steam app containing the proton prefix.
   *  If runtime is Steam: ID of the steam app to run.
   */
  int steam_app_id_;
  /*! \brief Working directory in which to run the command. */
  std::filesystem::path working_directory_;
  /*! \brief Maps environment variables to their values. */
  std::map<std::string, std::string> environment_variables_;
  /*! \brief Arguments to be passed to the executable. */
  std::string arguments_;
  /*! \brief Arguments to be passed to protontricks. */
  std::string protontricks_arguments_;
  /*! \brief If runtime is umu: Value passed to umu-launcher as GAMEID. */
  std::string umu_game_id_ = "0";
  /*! \brief If not empty: Ignore all other settings and run this command directly. */
  std::string command_overwrite_ = "";

  /*!
   * \brief Appends the given environment variables to the given command.
   * \param command Command to which to append to variables.
   * \param environment_variables Maps environment variables to their values
   * \param is_flatpak If true: Command is run from within a flatpak sandbox.
   */
  void appendEnvironmentVariables(std::string& command,
                                  const std::map<std::string, std::string>& environment_variables,
                                  bool is_flatpak) const;
};
