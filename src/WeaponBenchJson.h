#pragma once

#include "WeaponBenchLogic.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace crafting::weaponbench
{
    struct ModelInfo
    {
        std::string nifPath;
        std::string matSwap;
        std::vector<std::string> nifPaths;
    };

    struct WeaponEntry
    {
        std::uint32_t baseId = 0;
        std::uint32_t stackIndex = 0;
        std::string name;
        bool equipped = false;
    };

    struct InstalledInput
    {
        PartKey part;
        std::string name;
        bool hasLooseForm = false;
        bool isNullMod = false;
        std::vector<PartKey> newConfig;
    };

    struct ChoiceInput
    {
        PartKey part;
        std::string name;
        std::string description;
        std::uint32_t looseCount = 0;
        bool compatible = true;
        std::string reason;
        std::optional<ModelInfo> model;
    };

    struct SlotInput
    {
        std::uint32_t keyword = 0;
        std::string name;
        std::optional<InstalledInput> installed;
        std::vector<ChoiceInput> choices;
    };

    struct SelectedWeapon
    {
        std::uint32_t baseId = 0;
        std::uint32_t stackIndex = 0;
        std::string name;
        std::string subtitle;
        std::vector<std::pair<std::string, std::string>> stats;
        std::optional<ModelInfo> model;
        std::vector<SlotInput> slots;
    };

    struct SnapshotInput
    {
        std::string session;
        std::uint64_t revision = 0;
        std::vector<WeaponEntry> weapons;
        std::optional<SelectedWeapon> selected;
    };

    [[nodiscard]] inline const char* ReasonCode(Reason a_reason)
    {
        switch (a_reason) {
        case Reason::Ok: return "ok";
        case Reason::Disabled: return "disabled";
        case Reason::ViewClosed: return "view_closed";
        case Reason::SessionMismatch: return "session_mismatch";
        case Reason::MenuClosed: return "menu_closed";
        case Reason::PlayerUnavailable: return "player_unavailable";
        case Reason::Busy: return "busy";
        case Reason::StaleRevision: return "stale_revision";
        case Reason::BadId: return "bad_id";
        case Reason::UnknownWeapon: return "unknown_weapon";
        case Reason::StaleStack: return "stale_stack";
        case Reason::RemoveUnsupported: return "remove_unsupported";
        case Reason::UnknownSlot: return "unknown_slot";
        case Reason::UnknownPart: return "unknown_part";
        case Reason::AlreadyInstalled: return "already_installed";
        case Reason::Incompatible: return "incompatible";
        case Reason::NoLooseItem: return "no_loose_item";
        case Reason::LooseRankedStack: return "loose_ranked_stack";
        case Reason::WeaponLocked: return "weapon_locked";
        case Reason::WeaponTemporaryStack: return "weapon_temporary_stack";
        case Reason::WeaponRankedPart: return "weapon_ranked_part";
        case Reason::InstallUnavailable: return "install_unavailable";
        }
        return "unknown";
    }

    [[nodiscard]] inline const char* ReasonMessage(Reason a_reason)
    {
        switch (a_reason) {
        case Reason::Ok: return "";
        case Reason::Disabled: return "The weapon workbench is disabled.";
        case Reason::ViewClosed: return "The weapon workbench view is closed.";
        case Reason::SessionMismatch: return "This workbench session has ended. Reopen the bench.";
        case Reason::MenuClosed: return "The workbench menu is closed.";
        case Reason::PlayerUnavailable: return "You cannot modify weapons right now.";
        case Reason::Busy: return "Another change is still being applied.";
        case Reason::StaleRevision: return "The inventory changed. Review the refreshed list and try again.";
        case Reason::BadId: return "That request was not understood.";
        case Reason::UnknownWeapon: return "That weapon is no longer available.";
        case Reason::StaleStack: return "That weapon changed. Review the refreshed list and try again.";
        case Reason::RemoveUnsupported: return "Removing attachments is not supported yet.";
        case Reason::UnknownSlot: return "That slot is not available on this weapon.";
        case Reason::UnknownPart: return "That attachment is not available for this slot.";
        case Reason::AlreadyInstalled: return "That attachment is already fitted.";
        case Reason::Incompatible: return "That attachment is not compatible with this weapon.";
        case Reason::NoLooseItem: return "You do not own that attachment.";
        case Reason::LooseRankedStack: return "That attachment is a ranked item and cannot be used.";
        case Reason::WeaponLocked: return "This weapon is locked and cannot be modified.";
        case Reason::WeaponTemporaryStack: return "This weapon has temporary effects and cannot be modified.";
        case Reason::WeaponRankedPart: return "This weapon has ranked attachments and cannot be modified.";
        case Reason::InstallUnavailable: return "Installing parts is not available yet.";
        }
        return "";
    }

    [[nodiscard]] inline nlohmann::json ModelJson(const ModelInfo& a_model)
    {
        nlohmann::json out = { { "nifPath", a_model.nifPath } };
        if (!a_model.matSwap.empty()) out["matSwap"] = a_model.matSwap;
        if (a_model.nifPaths.size() > 1) out["nifPaths"] = a_model.nifPaths;
        return out;
    }

    [[nodiscard]] inline nlohmann::json BuildSnapshot(const SnapshotInput& a_in)
    {
        using nlohmann::json;
        json out = json::object();
        out["session"] = a_in.session;
        out["revision"] = a_in.revision;
        out["installAvailable"] = false;
        out["weapons"] = json::array();
        for (const auto& w : a_in.weapons) {
            out["weapons"].push_back({ { "id", EncodeWeaponId({ w.baseId, w.stackIndex }) }, { "name", w.name }, { "equipped", w.equipped } });
        }
        if (!a_in.selected) return out;

        const auto& sel = *a_in.selected;
        json weapon = { { "id", EncodeWeaponId({ sel.baseId, sel.stackIndex }) }, { "name", sel.name }, { "subtitle", sel.subtitle },
            { "stats", json::array() }, { "slots", json::array() } };
        if (sel.model && !sel.model->nifPath.empty()) weapon["model"] = ModelJson(*sel.model);
        for (const auto& [label, value] : sel.stats) weapon["stats"].push_back({ { "label", label }, { "value", value } });
        for (const auto& s : sel.slots) {
            json slot = { { "id", EncodeSlotId(s.keyword) }, { "name", s.name }, { "removable", false },
                { "installed", nullptr }, { "choices", json::array() } };
            if (s.installed) {
                const auto& i = *s.installed;
                const bool returns = PartReturns(i.part, i.hasLooseForm, i.isNullMod, i.newConfig);
                slot["installed"] = { { "id", EncodePartId(i.part) }, { "name", i.name }, { "returns", returns } };
            }
            for (const auto& c : s.choices) {
                json choice = { { "id", EncodePartId(c.part) }, { "name", c.name }, { "description", c.description },
                    { "count", c.looseCount }, { "compatible", c.compatible } };
                if (!c.reason.empty()) choice["reason"] = c.reason;
                if (c.model && !c.model->nifPath.empty()) choice["model"] = ModelJson(*c.model);
                slot["choices"].push_back(std::move(choice));
            }
            weapon["slots"].push_back(std::move(slot));
        }
        out["weapon"] = std::move(weapon);
        return out;
    }

    // Constraint: a_requestId is the bench's string, echoed unchanged; a_snapshot must be freshly built after the action or rejection.
    [[nodiscard]] inline nlohmann::json BuildActionReply(const std::string& a_requestId, Reason a_reason,
        nlohmann::json a_snapshot)
    {
        a_snapshot["requestId"] = a_requestId;
        a_snapshot["ok"] = a_reason == Reason::Ok;
        a_snapshot["reason"] = ReasonCode(a_reason);
        if (a_reason != Reason::Ok) a_snapshot["message"] = ReasonMessage(a_reason);
        return a_snapshot;
    }
}
