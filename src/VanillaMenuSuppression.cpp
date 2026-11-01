#include "VanillaMenuSuppression.h"

#include <cstring>
#include <mutex>

namespace
{
    struct Entry
    {
        const char* name;
        int claims;
    };

    Entry g_entries[] = {
        { "PromptMenu", 0 },
        { "ButtonBarMenu", 0 },
        { "PauseMenu", 0 },
        { "WorkshopMenu", 0 },
        { "CursorMenu", 0 },
    };

    std::mutex g_lock;
    VanillaMenuSuppression::Apply g_apply = nullptr;

    Entry* Find(const char* a_menu)
    {
        for (auto& entry : g_entries) {
            if (std::strcmp(entry.name, a_menu) == 0) return &entry;
        }
        return nullptr;
    }
}

void VanillaMenuSuppression::Configure(Apply a_apply)
{
    std::lock_guard lock(g_lock);
    g_apply = a_apply;
}

// Constraint: only the first claim suppresses and only the last release restores; an unknown menu is applied directly.
void VanillaMenuSuppression::Claim(const char* a_menu)
{
    if (!a_menu) return;
    std::lock_guard lock(g_lock);
    if (auto* entry = Find(a_menu)) {
        if (entry->claims++ == 0 && g_apply) g_apply(entry->name, true);
        return;
    }
    if (g_apply) g_apply(a_menu, true);
}

void VanillaMenuSuppression::Release(const char* a_menu)
{
    if (!a_menu) return;
    std::lock_guard lock(g_lock);
    if (auto* entry = Find(a_menu)) {
        if (entry->claims > 0 && --entry->claims == 0 && g_apply) g_apply(entry->name, false);
        return;
    }
    if (g_apply) g_apply(a_menu, false);
}
