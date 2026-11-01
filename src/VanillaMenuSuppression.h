#pragma once

#include <cstddef>

namespace VanillaMenuSuppression
{
    using Apply = bool (*)(const char* a_menu, bool a_suppress);

    void Configure(Apply a_apply);
    void Claim(const char* a_menu);
    void Release(const char* a_menu);
}
