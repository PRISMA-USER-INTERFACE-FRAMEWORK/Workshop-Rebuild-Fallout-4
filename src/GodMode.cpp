#include "GodMode.h"

#include <RE/P/PlayerCharacter.h>

namespace
{
    bool ReadPlayerGodMode()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && player->IsGodMode();
    }

    struct GodModeProbeInstaller
    {
        GodModeProbeInstaller()
        {
            crafting::SetGodModeProbe(&ReadPlayerGodMode);
        }
    } g_godModeProbeInstaller;
}
