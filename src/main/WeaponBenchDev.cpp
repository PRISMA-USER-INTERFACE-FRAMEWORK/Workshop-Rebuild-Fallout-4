#include "RuntimeInternal.h"
#include "WeaponBench.h"

#include <RE/B/BGSMod.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectWEAP.h>

#include <cstring>
#include <utility>

namespace WeaponBench
{
    void GrantDevTestLoadout()
    {
        // Constraint: dev mode is runtime-only; never add test items or attachments to the save.
        logger::warn("PrismaCrafting: weapon bench DEV mode is runtime-only (virtual attachments); no items were added to the save");
    }
}
