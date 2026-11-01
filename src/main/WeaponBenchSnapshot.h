#pragma once

#include "WeaponBenchLogic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RE
{
    class TESBoundObject;
}

namespace WeaponBench
{
    struct RegistryEntry
    {
        RE::TESBoundObject* object = nullptr;
        std::uint32_t stackIndex = 0;
        std::uint64_t fingerprint = 0;
    };

    // Constraint: game thread only; the registry is rebuilt by every BuildSnapshotPayload call and the snapshot ids carry base form + stack.
    [[nodiscard]] const std::vector<RegistryEntry>& Registry();
    [[nodiscard]] std::uint64_t Revision();
    [[nodiscard]] std::string BuildSnapshotPayload(const std::string& a_session, std::optional<std::string> a_selected = std::nullopt);
    // Constraint: game thread only; a_state carries the caller-owned facts, the weapon facts are read live against the current Registry().
    [[nodiscard]] crafting::weaponbench::Reason ValidateLive(const crafting::weaponbench::ActionRequest& a_req, crafting::weaponbench::LiveState a_state);
}
