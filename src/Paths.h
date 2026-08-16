#pragma once

#include <filesystem>

namespace Paths
{

    std::filesystem::path F4SEUserDir();

    std::filesystem::path JsonDataDir();

    std::filesystem::path LogDir();
}
