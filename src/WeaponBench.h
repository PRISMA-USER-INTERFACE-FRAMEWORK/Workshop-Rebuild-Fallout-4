#pragma once

#include <RE/M/MenuOpenCloseEvent.h>

namespace WeaponBench
{
    void Initialize(bool a_flag);
    bool Enabled();
    bool SessionActive();
    bool DevEnabled();
    void GrantDevTestLoadout();
    void OnExamineMenuEvent(const RE::MenuOpenCloseEvent& a_event);
}
