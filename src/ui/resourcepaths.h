#pragma once

#include "core/consts.h"
#include <QCoreApplication>
#include <filesystem>

inline std::filesystem::path limoResourcePath(const std::filesystem::path& relative)
{
  namespace fs = std::filesystem;
  const fs::path executable_dir = QCoreApplication::applicationDirPath().toStdString();
  for(const auto& dir : { executable_dir.parent_path() / "share/limo",
                          executable_dir / "share/limo" })
  {
    std::error_code error;
    if(fs::exists(dir / relative, error))
      return dir / relative;
  }
  return fs::path(APP_DATA_DIR) / relative;
}
