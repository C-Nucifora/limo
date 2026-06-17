#include "pack.h"


std::set<int> pack_util::enabledSet(const std::vector<Pack>& packs,
                                    const std::set<std::string>& active)
{
  std::set<int> ids;
  for(const auto& pack : packs)
  {
    if(!active.contains(pack.name))
      continue;
    for(int mod_id : pack.mod_ids)
      ids.insert(mod_id);
  }
  return ids;
}

std::vector<int> pack_util::deployOrder(const std::vector<Pack>& packs,
                                        const std::set<std::string>& active)
{
  std::vector<int> order;
  std::set<int> seen;
  for(const auto& pack : packs)
  {
    if(!active.contains(pack.name))
      continue;
    for(int mod_id : pack.mod_ids)
    {
      if(seen.insert(mod_id).second)
        order.push_back(mod_id);
    }
  }
  return order;
}
