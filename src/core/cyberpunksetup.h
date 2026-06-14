/*!
 * \file cyberpunksetup.h
 * \brief Header for the cyberpunk_setup namespace.
 *
 * Self-contained helper module for Cyberpunk 2077 Proton setup validation.
 * No Qt, no Deployer headers — pure C++23 / STL, safe to call from any context.
 *
 * \par Integration recipe (for the dev branch)
 * 1. Add cyberpunksetup.cpp to the CORE_SOURCES list in CMakeLists.txt.
 * 2. In ApplicationManager::addApplication / editApplication (applicationmanager.cpp),
 *    after the deployer list is known, iterate over EditDeployerInfo entries.
 *    For each deployer whose target_dir lives under the Cyberpunk game directory,
 *    call cyberpunk_setup::checkCyberpunkDeployment() and emit a warning signal to
 *    the UI if the optional is non-empty.
 * 3. In AddAppDialog (addappdialog.cpp), after the user selects a deploy mode for a
 *    Cyberpunk application, call the same function and display the returned warning
 *    in a QMessageBox or an inline label.
 * 4. To surface the setup checklist, show cyberpunk_setup::setupChecklist() in a
 *    dedicated info dialog or tooltip when the user first adds a Cyberpunk application
 *    (e.g. steam_app_id == CYBERPUNK_STEAM_APP_ID).
 * 5. The protontricks command from cyberpunk_setup::protontricksCommand() can be shown
 *    in the same info dialog and offered as a one-click "Copy to clipboard" button.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>


/*!
 * \brief Helpers for validating and setting up Cyberpunk 2077 mod environments under Proton.
 *
 * All functions are pure: they do not modify filesystem state, emit signals, or depend on
 * Qt. They return plain strings / optionals so callers can decide how to surface the
 * information (message box, log entry, tooltip, etc.).
 */
namespace cyberpunk_setup
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/*!
 * \brief Steam AppID for Cyberpunk 2077.
 * Used by callers to detect that a ModdedApplication is a Cyberpunk install.
 */
inline constexpr long CYBERPUNK_STEAM_APP_ID = 1091500L;

/*!
 * \brief Required Steam launch options for CET / RED4ext.
 *
 * These override must be present so that winmm.dll and version.dll are loaded
 * from the game directory (native,builtin) rather than the default Wine stubs,
 * which is the mechanism both Cyber Engine Tweaks and RED4ext rely on.
 */
inline constexpr const char* REQUIRED_LAUNCH_OPTIONS =
  R"(WINEDLLOVERRIDES="winmm,version=n,b" %command%)";

/*!
 * \brief Protontricks dependencies required in the game's Wine prefix.
 *
 * - d3dcompiler_47: needed by CET's embedded Chromium renderer.
 * - vcrun2022:      Visual C++ 2022 runtime; required by RED4ext and many
 *                  native DLL mods compiled with MSVC.
 */
inline constexpr const char* PROTONTRICKS_DEPENDENCIES = "d3dcompiler_47 vcrun2022";

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

/*!
 * \brief Returns the complete protontricks command to install required dependencies.
 *
 * The caller should display this string to the user (e.g. in an info dialog with a
 * "Copy to clipboard" button) so they can paste it into a terminal.
 *
 * \return A shell-ready string of the form:
 *         \c "protontricks 1091500 d3dcompiler_47 vcrun2022"
 */
std::string protontricksCommand();

/*!
 * \brief Returns a human-readable Cyberpunk mod setup checklist.
 *
 * Each entry is a short description of one required setup step.  Callers can
 * join them with newlines, render them as a bulleted list, or iterate over them
 * individually to show completion status.
 *
 * \return Ordered vector of checklist items.
 */
std::vector<std::string> setupChecklist();

/*!
 * \brief Checks whether the given deployer configuration is safe for Cyberpunk mods.
 *
 * Cyberpunk script extenders (CET, RED4ext) are loaded via DLL proxying; the game
 * must be able to find the proxy DLL in its own directory.  Hard links work because
 * the file resides on the same inode; symlinks may or may not work depending on the
 * Wine version, but are unreliable in practice.  Copy mode always works but wastes
 * disk space.
 *
 * Additionally, hard links across filesystem boundaries are impossible at the OS
 * level.  If staging_path and target_path are on different filesystems this function
 * warns even when deploy_mode is hard_link, because the deployment will fail at
 * link-creation time.
 *
 * \param deploy_mode Integer representation of Deployer::DeployMode:
 *        0 = hard_link, 1 = sym_link, 2 = copy.
 *        \see Deployer::DeployMode
 * \param staging_path Path to the staging / source directory (where Limo stores mods).
 * \param target_path  Path to the game's installation directory (deploy target).
 * \return A warning string if the configuration is risky, std::nullopt if it is safe.
 *
 * \note The cross-filesystem check compares the device IDs returned by
 *       std::filesystem::status().  If either path does not exist yet (common during
 *       the add-application wizard), the check is skipped and only the mode check
 *       is applied.
 * \note copy mode (2) always returns std::nullopt — it is safe for CET/RED4ext
 *       at the cost of extra disk usage.
 *
 * // TODO(cp-setup): Validate that the "sym_link is unreliable" assumption still
 * //                 holds with recent versions of Wine / Proton >= 9.x.
 */
std::optional<std::string> checkCyberpunkDeployment(
  int deploy_mode,
  const std::filesystem::path& staging_path,
  const std::filesystem::path& target_path);

/*!
 * \brief Checks whether required Wine-prefix DLLs are present in the given Proton prefix.
 *
 * Looks for known DLL markers under \p proton_prefix/drive_c/windows/system32:
 * - \c d3dcompiler_47.dll (direct check)
 * - \c msvcp140.dll       (proxy for vcrun2022; vcrun installs the full VC++ runtime
 *                          whose presence is indicated by this DLL)
 *
 * \param proton_prefix Root of the Proton prefix (the directory that contains
 *        \c drive_c, \c system.reg, etc.).
 * \return Vector of human-readable strings describing each missing prerequisite.
 *         Empty if all expected files are found.
 *
 * // TODO(cp-setup): Determine the canonical marker file for vcrun2022 — msvcp140.dll
 * //                 is used as a reasonable proxy but a freshly-installed VC++ 2022
 * //                 prefix may also contain msvcp140_1.dll or msvcp140_2.dll.
 * //                 Consider extending the check to also look at vcruntime140.dll.
 */
std::vector<std::string> missingPrefixPrerequisites(const std::filesystem::path& proton_prefix);

} // namespace cyberpunk_setup
