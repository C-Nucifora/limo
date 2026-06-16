#include "versionchangelog.h"
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <iomanip>


VersionChangelog::VersionChangelog(const Json::Value& json)
{
  version_ = json["version"].asString();
  date_ = json["date"].asInt64();
  changes_.reserve(json["changes"].size());
  title_ = json["title"].asString();
  for(int i = 0; i < json["changes"].size(); i++)
    changes_.emplace_back(json["changes"][i]);
  std::sort(changes_.begin(), changes_.end());
}

std::string VersionChangelog::getVersion() const
{
  return version_;
}

std::time_t VersionChangelog::getDate() const
{
  return date_;
}

std::string VersionChangelog::getTitle() const
{
  return title_;
}

const std::vector<ChangelogEntry>& VersionChangelog::getChanges() const
{
  return changes_;
}

std::string VersionChangelog::versionAndDateString() const
{
  std::stringstream ss;
  std::tm tm_buf{};
  std::time_t date = date_;
  localtime_r(&date, &tm_buf);
  ss << version_ << " (" << std::put_time(&tm_buf, "%Y-%m-%d") << ")";
  return ss.str();
}

bool VersionChangelog::operator<(const VersionChangelog& other) const
{
  return date_ < other.date_;
}
