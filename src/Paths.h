#pragma once

#include <filesystem>

namespace Paths
{

    const std::filesystem::path& F4SEUserDir();

    const std::filesystem::path& JsonDataDir();

    const std::filesystem::path& LogDir();
}
