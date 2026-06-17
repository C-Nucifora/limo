#include "archivenormalizer.h"
#include <algorithm>
#include <cctype>


namespace
{
std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Drops the first `levels` '/'-separated components from a path. Mirrors the installer's
// moveFilesWithDepth strip so the prefix is computed against the same rooted layout.
std::string stripLeadingComponents(const std::string& path, int levels)
{
  if(levels <= 0)
    return path;
  std::size_t pos = 0;
  for(int i = 0; i < levels; i++)
  {
    const std::size_t slash = path.find('/', pos);
    if(slash == std::string::npos)
      return ""; // path is shallower than the strip level: nothing remains
    pos = slash + 1;
  }
  return path.substr(pos);
}

std::string baseName(const std::string& path)
{
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace


std::string archive_normalizer::contentPrefix(const std::vector<std::string>& archive_paths,
                                              int root_level,
                                              const std::vector<Anchor>& anchors,
                                              const std::string& loose_root_folder)
{
  for(const auto& anchor : anchors)
  {
    if(anchor.marker.empty() || anchor.prefix.empty())
      continue;
    const std::string marker_lower = toLower(anchor.marker);
    const std::string prefix_lower = toLower(anchor.prefix) + "/";
    for(const auto& raw_path : archive_paths)
    {
      const std::string rooted = stripLeadingComponents(raw_path, root_level);
      if(rooted.empty())
        continue;
      if(toLower(baseName(rooted)) != marker_lower)
        continue;
      // Marker found: prepend the prefix unless the marker already lives under it.
      const std::string rooted_lower = toLower(rooted);
      if(rooted_lower.rfind(prefix_lower, 0) == 0)
        return "";
      // If the marker sits directly at the archive root (no enclosing directory), the loose files
      // would land directly under the prefix (e.g. content/cars/ui_car.json). Assetto Corsa and
      // similar games require each item in its own subfolder, so wrap them in a folder named after
      // the mod when one was supplied.
      if(rooted.find('/') == std::string::npos && !loose_root_folder.empty())
      {
        std::string folder = loose_root_folder;
        std::replace(folder.begin(), folder.end(), '/', '_');
        std::replace(folder.begin(), folder.end(), '\\', '_');
        if(!folder.empty())
          return anchor.prefix + "/" + folder;
      }
      return anchor.prefix;
    }
  }
  return "";
}
