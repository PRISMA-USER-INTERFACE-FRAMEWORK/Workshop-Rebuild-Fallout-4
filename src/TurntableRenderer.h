#pragma once

#include <string>

namespace TurntableRenderer
{

    void Init();

    void Request(const std::string& slug, const std::string& nifPath, const std::string& createdKey);

    bool Enabled();
}
