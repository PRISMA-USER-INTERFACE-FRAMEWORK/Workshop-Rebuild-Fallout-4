#pragma once

#include "WorkbenchUpgrades.h"
#include <filesystem>
#include <unordered_map>

namespace WorkbenchUpgrades
{
    bool LoadBenchDefinitions(const std::filesystem::path& path,
                              std::vector<BenchDef>& benches,
                              std::unordered_map<std::string, std::string>& categoryBench);
}
