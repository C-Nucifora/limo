#include "matcher.h"
#include <sstream>

static std::string entryToStr(const std::shared_ptr<DeployerEntry>& e) {
  if (!e) return "<null>";
  std::string s = "{sep=" + std::to_string(e->isSeparator) + " name=" + e->name +
                  " id=" + std::to_string(e->id);
  if (!e->isSeparator) {
    const auto* info = static_cast<const DeployerModInfo*>(e.get());
    s += " enabled=" + std::to_string(info->enabled);
  }
  s += "}";
  return s;
}

bool DeployerEntryVectorMatcher::match(std::vector<std::weak_ptr<DeployerEntry>> const& actual) const {
  // Build full actual sequence for diagnostics
  std::string actual_seq = "[";
  for (size_t i = 0; i < actual.size(); ++i) {
    auto a = actual[i].lock();
    actual_seq += entryToStr(a);
    if (i + 1 < actual.size()) actual_seq += ", ";
  }
  actual_seq += "]";

  if (actual.size() != m_expected.size()) {
    m_actual_desc = "size " + std::to_string(actual.size()) + " vs expected " +
                    std::to_string(m_expected.size()) + "; actual=" + actual_seq;
    return false;
  }

  for (size_t i = 0; i < actual.size(); ++i) {
    auto a = actual[i].lock();
    auto e = m_expected[i].lock();

    if (!a || !e) { m_actual_desc = "null ptr at index " + std::to_string(i); return false; }
    if (*a != *e) {
      m_actual_desc = "mismatch at [" + std::to_string(i) + "]: actual=" +
                      entryToStr(a) + " expected=" + entryToStr(e) +
                      "; full actual=" + actual_seq;
      return false;
    }
  }
  return true;
}

std::string DeployerEntryVectorMatcher::describe() const {
  std::ostringstream ss;
  ss << "contains equal DeployerEntry objects as reference vector";
  if (!m_actual_desc.empty())
    ss << "; diff: " << m_actual_desc;
  return ss.str();
}

