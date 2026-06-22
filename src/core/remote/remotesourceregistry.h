/*!
 * \file remotesourceregistry.h
 * \brief Thin registry that maps provider names to RemoteSource instances.
 *
 * Integration point for limo-app/limo#60.
 *
 * \par Status (fork audit F087)
 * EXPERIMENTAL — not yet wired into the GUI. The Thunderstore, GameBanana and mod.io providers
 * behind this registry are implemented and exercised by network-free smoke tests
 * (tests/test_remoteproviders.cpp), but no UI currently lets the user pick a provider and browse
 * or install from it; the shipped remote flows are Nexus (src/core/nexus/) and OMM repositories
 * (RepositoriesDialog). Surfacing these providers in a download/import dialog is the remaining
 * work. Until then treat this registry as a tested-but-unwired building block, not dead code.
 *
 * Usage example (e.g. from ApplicationManager or a future download dialog):
 * \code
 *   auto& reg = remote::RemoteSourceRegistry::instance();
 *   auto* ts  = reg.get("Thunderstore");
 *   auto mods = ts->search("ror2", "bepinex");
 * \endcode
 *
 * Wire UI-level provider selection to this registry rather than
 * constructing providers directly.  The Nexus flow in src/core/nexus/ is
 * deliberately left unchanged; this registry supplements it.
 */

#pragma once

#include "remotesource.h"
#include "thunderstoreprovider.h"
#include "gamebanana_provider.h"
#include "modio_provider.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace remote
{

/*!
 * \brief Singleton registry of available RemoteSource providers.
 *
 * Providers are registered at program startup with their display names as keys.
 * Callers that only need the Thunderstore provider can use the static helper
 * thunderstore() directly.
 *
 * \warning EXPERIMENTAL: The Thunderstore/GameBanana/mod.io providers exposed by
 * this registry are not yet wired into the repositories/import UI. They are
 * provided as scaffolding for limo-app/limo#60 and their parse/install paths
 * have not been validated end-to-end. Treat all results as untrusted and do not
 * rely on this registry in production flows until it is integrated. Query
 * isExperimental() to gate any UI that surfaces these providers.
 */
class RemoteSourceRegistry
{
public:
  /*!
   * \brief Whether the providers in this registry are experimental/unwired.
   *
   * Returns true while the registry is not integrated into the import UI.
   * UI code should hide or disable provider selection unless this is false.
   */
  static constexpr bool isExperimental() { return true; }

  /*! \brief Get the process-wide singleton instance. */
  static RemoteSourceRegistry& instance()
  {
    static RemoteSourceRegistry reg;
    return reg;
  }

  /*! \brief Non-copyable, non-movable singleton. */
  RemoteSourceRegistry(const RemoteSourceRegistry&) = delete;
  RemoteSourceRegistry& operator=(const RemoteSourceRegistry&) = delete;

  /*!
   * \brief Look up a provider by display name.
   * \param provider_name Case-sensitive name, e.g. "Thunderstore".
   * \return Pointer to the provider, or nullptr if not registered.
   */
  RemoteSource* get(const std::string& provider_name) const
  {
    auto it = providers_.find(provider_name);
    return it != providers_.end() ? it->second.get() : nullptr;
  }

  /*!
   * \brief List the names of all registered providers.
   * \return Sorted vector of provider display names.
   */
  std::vector<std::string> providerNames() const
  {
    std::vector<std::string> names;
    names.reserve(providers_.size());
    for(const auto& [k, _] : providers_)
      names.push_back(k);
    return names;
  }

  /*!
   * \brief Convenience accessor for the Thunderstore provider.
   * \return Reference to the Thunderstore provider.
   */
  ThunderstoreProvider& thunderstore()
  {
    return *static_cast<ThunderstoreProvider*>(providers_.at("Thunderstore").get());
  }

  /*!
   * \brief Convenience accessor for the GameBanana provider.
   * \return Reference to the GameBanana provider.
   */
  GamebananaProvider& gamebanana()
  {
    return *static_cast<GamebananaProvider*>(providers_.at("GameBanana").get());
  }

  /*!
   * \brief Convenience accessor for the mod.io provider.
   *
   * Remember to call modio().setApiKey(key) before any request.
   * \return Reference to the mod.io provider.
   */
  ModioProvider& modio()
  {
    return *static_cast<ModioProvider*>(providers_.at("mod.io").get());
  }

private:
  RemoteSourceRegistry()
  {
    providers_["Thunderstore"] = std::make_unique<ThunderstoreProvider>();
    providers_["GameBanana"]   = std::make_unique<GamebananaProvider>();
    providers_["mod.io"]       = std::make_unique<ModioProvider>();
  }

  std::unordered_map<std::string, std::unique_ptr<RemoteSource>> providers_;
};

} // namespace remote
