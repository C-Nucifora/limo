/*!
 * \file modrule.h
 * \brief Contains the ModRule struct and RuleType enum.
 */

#pragma once

#include <json/json.h>
#include <string>


/*!
 * \brief The type of relationship a ModRule describes.
 */
enum class RuleType
{
  /*! \brief The source mod requires the target mod to be enabled. */
  requires_mod,
  /*! \brief The source mod conflicts with the target mod; both should not be enabled together. */
  conflicts_with
};

/*!
 * \brief Describes a dependency or conflict relationship between two mods.
 *
 * Rules are app-global (not profile-scoped) and are checked before deployment.
 * Violations produce non-blocking warnings.
 */
struct ModRule
{
  /*! \brief Id of the mod that owns this rule. */
  int source_mod_id;
  /*! \brief The type of rule. */
  RuleType type;
  /*! \brief Id of the mod the rule refers to. */
  int target_mod_id;

  /*!
   * \brief Constructs a rule from its components.
   * \param source_id Id of the source mod.
   * \param rule_type The rule type.
   * \param target_id Id of the target mod.
   */
  ModRule(int source_id, RuleType rule_type, int target_id) :
    source_mod_id(source_id), type(rule_type), target_mod_id(target_id)
  {}

  /*!
   * \brief Constructs a rule from a JSON object.
   * \param json Source JSON.
   */
  explicit ModRule(const Json::Value& json) :
    source_mod_id(json["source_mod_id"].asInt()),
    type(static_cast<RuleType>(json["type"].asInt())),
    target_mod_id(json["target_mod_id"].asInt())
  {}

  /*!
   * \brief Serializes this rule to a JSON object.
   * \return The JSON object.
   */
  Json::Value toJson() const
  {
    Json::Value v;
    v["source_mod_id"] = source_mod_id;
    v["type"] = static_cast<int>(type);
    v["target_mod_id"] = target_mod_id;
    return v;
  }

  /*!
   * \brief Returns a human-readable label for the rule type.
   * \param t The rule type.
   * \return "requires" or "conflicts with".
   */
  static std::string typeLabel(RuleType t)
  {
    return t == RuleType::requires_mod ? "requires" : "conflicts with";
  }

  bool operator==(const ModRule& other) const
  {
    return source_mod_id == other.source_mod_id && type == other.type &&
           target_mod_id == other.target_mod_id;
  }
};
