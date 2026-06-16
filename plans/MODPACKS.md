# Plan: Modpacks — per-map mod sets & multiple packs active at once

Research: [`research/05-modpacks-and-ui.md`](research/05-modpacks-and-ui.md).

Two distinct requirements from the user:
1. **"Different mods for different maps/saves."**
2. **"Multiple packs active at once."**

## 1. Mapping onto Limo's existing primitives

| Primitive | What it is | Multi-active? | Drives deploy? |
|---|---|---|---|
| **Profile** | A complete, independent **load-order + enabled-state + conflict-groups** snapshot, per deployer (`deployer.h:651-659`, `current_profile_` is a single int). Mod *membership/groups/tags* are instance-wide; only enabled-state/order are per-profile. | **No** — exactly one active (`setProfile(int)`). | Yes |
| **Group** | A set where **only the active member deploys** (a version/variant selector, e.g. texture v1 vs v2). | No — single active by construction. | Yes |
| **Tag** | A label for filtering/grouping in the UI (`ManualTag`/`AutoTag`). A mod can carry many. | Yes | **No** — tags only drive the filter proxies, zero deploy effect. |

### Requirement 1 — "different mods per map" → **Profiles (already works)**

A profile *is* a self-contained enabled-set + load order. One profile per map ("Erlengrat", "Frutiger Berg") works today via the profile selector. **Gap is only ergonomics:** creating one per map is manual, there's no "duplicate profile" affordance, and no per-save auto-switch. → small sugar, no core change.

### Requirement 2 — "multiple packs active at once" → **the real gap**

Nothing fits: profiles are single-select, groups are single-active, tags are multi-but-inert. There is **no primitive that toggles several named, overlapping bundles on/off and deploys their union.**

## 2. Recommended design

### Option A — "Pack = a tag with a toggle that drives enabled-state" (ship this)

A **pack** is a named, user-defined set of mods; any number can be on at once; the deployed set is the **union** of all enabled packs within the current profile.

- **Storage:** reuse `ManualTag` for membership (a pack is a named tag; a mod can be in several packs because tags overlap). Membership UI already exists (`managemodtagsdialog`, `editmanualtagsdialog`, `addTagsToMods`).
- **New state:** a per-deployer-per-profile set of **active pack names**, persisted in `json_settings_`.
- **Deploy logic:** when the active-pack set changes, recompute each mod's enabled bit as `enabled = (mod ∈ ≥1 active pack)` and write it into the existing per-profile load-order tree, then deploy as normal. Deployment already keys off the per-entry enabled bool — you're computing that bool from pack state instead of from a manual click.
- **Honest limitation:** load *order* between packs still comes from the single underlying per-profile order; packs decide on/off, not relative order. Fine for FS/AC (order rarely matters); for Bethesda-style games keep using profiles/load order.

### Option B — "Pack as a first-class concept" (later)

Add `packs_` to `ModdedApplication` parallel to `groups_`: `Pack { name; notes; std::set<int> mod_ids; bool enabled; }` + `pack_map_`, serialized in `updateSettings`/`updateState`, deploy = union of enabled packs, with its own manage dialog. Cleaner, but touches serialization + deploy path + new dialog. Option A degrades gracefully into B later (promote the tag to a struct with an `enabled` flag + union-deploy).

**Recommendation:** ship **A**, lean on **profiles as-is** for per-map sets (+ a "duplicate profile" affordance + clearer naming).

## 3. Files / classes touched (Option A)

- **Core model** — `src/core/moddedapplication.{h,cpp}`: add active-pack state (e.g. `std::vector<std::set<std::string>> active_packs_per_profile_`), a `setPackEnabled(name, bool)` method, recompute-enabled logic feeding the existing load-order tree, and serialization in `updateSettings`/`updateState`. Profile add/remove must extend the per-profile pack vector (mirror `addProfile`/`removeProfile`, `deployer.cpp:363-393`).
- **Deploy path** — `deployMods`/`updateDeployerGroups` (`moddedapplication.cpp`): apply computed enabled bits before deploy; reuse `setModStatusAcrossDeployers` semantics for split mods.
- **Membership** — reuse `ManualTag`, `addTagsToMods`/`removeTagsFromMods`, `manual_tag_map_`.
- **UI** — `src/ui/mainwindow.cpp`: a "Packs" multi-checkable toggle bar near `profile_selection_box`/`deployer_selection_box`, built from a `TagCheckBox` analog (`src/ui/tagcheckbox.{h,cpp}`) but checking a pack box **enables those mods** (not filters the view). A "Manage Packs" dialog modeled on `managegroupsdialog`/`managemodtagsdialog`.
- **Signals** — `src/ui/applicationmanager.{h,cpp}`: a `setPackEnabled` signal/slot mirroring the existing `setProfile`/`addProfile` plumbing (`mainwindow.cpp:463-474`).

## 4. FS tie-in

A modpack in FS is just one-or-more `.zip`s (no special format). "Different mods per map" = a profile per map; "multiple packs active" = enabling several pack toggles whose union deploys into the FS `mods/` folder. Ship the pack feature **together with** the FS preset ([FARMING_SIMULATOR.md](FARMING_SIMULATOR.md)) for the full experience.

## 5. Work breakdown

1. Core: active-pack state + `setPackEnabled` + recompute-enabled + serialization. — *Issue #232*
2. UI: pack toggle bar + Manage Packs dialog + signal plumbing. — *Issue #232*
3. Sugar: "duplicate profile" affordance for fast per-map profile creation. — *Issue #232 (or split)*
4. (Later) Option B first-class `packs_` + per-pack ordering.
