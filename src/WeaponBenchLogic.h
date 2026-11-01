#pragma once

#include <algorithm>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crafting::weaponbench
{
    struct PartKey
    {
        std::uint32_t mod = 0;
        std::uint32_t rank = 1;
        auto operator<=>(const PartKey&) const = default;
    };

    namespace detail
    {
        [[nodiscard]] inline std::string ToChars(std::uint32_t a_value, int a_base)
        {
            char buf[16];
            const auto r = std::to_chars(buf, buf + sizeof(buf), a_value, a_base);
            return std::string(buf, r.ptr);
        }

        [[nodiscard]] inline std::optional<std::uint32_t> ParseU32(std::string_view a_text, int a_base)
        {
            if (a_text.empty()) return std::nullopt;
            std::uint32_t v = 0;
            const char* end = a_text.data() + a_text.size();
            const auto r = std::from_chars(a_text.data(), end, v, a_base);
            if (r.ec != std::errc{} || r.ptr != end) return std::nullopt;
            return v;
        }

        constexpr std::uint64_t Mix(std::uint64_t a_hash, std::uint64_t a_value) noexcept
        {
            for (int i = 0; i < 8; ++i) {
                a_hash ^= (a_value >> (8 * i)) & 0xFF;
                a_hash *= 1099511628211ull;
            }
            return a_hash;
        }
    }

    struct WeaponKey
    {
        std::uint32_t baseId = 0;
        std::uint32_t stackIndex = 0;
        auto operator<=>(const WeaponKey&) const = default;
    };

    // Constraint: decode accepts only the canonical encoding (lowercase hex, no leading zeros, decimal stack) so encode(decode(s)) is exact.
    [[nodiscard]] inline std::string EncodeWeaponId(const WeaponKey& a_weapon)
    {
        return "w" + detail::ToChars(a_weapon.baseId, 16) + "s" + detail::ToChars(a_weapon.stackIndex, 10);
    }
    [[nodiscard]] inline std::string EncodeSlotId(std::uint32_t a_keyword) { return "s" + detail::ToChars(a_keyword, 16); }
    [[nodiscard]] inline std::string EncodePartId(const PartKey& a_part)
    {
        return "m" + detail::ToChars(a_part.mod, 16) + "r" + detail::ToChars(a_part.rank, 10);
    }

    [[nodiscard]] inline std::optional<WeaponKey> DecodeWeaponId(std::string_view a_id)
    {
        if (a_id.size() < 4 || a_id[0] != 'w') return std::nullopt;
        const auto divider = a_id.find('s', 1);
        if (divider == std::string_view::npos) return std::nullopt;
        const auto baseId = detail::ParseU32(a_id.substr(1, divider - 1), 16);
        const auto stackIndex = detail::ParseU32(a_id.substr(divider + 1), 10);
        if (!baseId || !stackIndex) return std::nullopt;
        const WeaponKey key{ *baseId, *stackIndex };
        if (EncodeWeaponId(key) != a_id) return std::nullopt;
        return key;
    }

    [[nodiscard]] inline std::optional<std::uint32_t> DecodeSlotId(std::string_view a_id)
    {
        if (a_id.size() < 2 || a_id[0] != 's') return std::nullopt;
        const auto v = detail::ParseU32(a_id.substr(1), 16);
        if (!v || EncodeSlotId(*v) != a_id) return std::nullopt;
        return v;
    }

    [[nodiscard]] inline std::optional<PartKey> DecodePartId(std::string_view a_id)
    {
        if (a_id.size() < 4 || a_id[0] != 'm') return std::nullopt;
        const auto r = a_id.find('r');
        if (r == std::string_view::npos) return std::nullopt;
        const auto mod = detail::ParseU32(a_id.substr(1, r - 1), 16);
        const auto rank = detail::ParseU32(a_id.substr(r + 1), 10);
        if (!mod || !rank) return std::nullopt;
        const PartKey part{ *mod, *rank };
        if (EncodePartId(part) != a_id) return std::nullopt;
        return part;
    }

    using ConfigKey = std::vector<PartKey>;

    struct StackFacts
    {
        std::uint32_t baseFormId = 0;
        std::uint32_t count = 1;
        bool equipped = false;
        std::vector<PartKey> installed;
    };

    [[nodiscard]] inline ConfigKey MakeConfigKey(const StackFacts& a_stack)
    {
        ConfigKey key = a_stack.installed;
        std::ranges::sort(key);
        return key;
    }

    // Constraint: staleness checks only; the post-write count diff uses ConfigKey, which excludes count and equipped.
    [[nodiscard]] inline std::uint64_t Fingerprint(const StackFacts& a_stack)
    {
        const ConfigKey key = MakeConfigKey(a_stack);
        std::uint64_t h = detail::Mix(14695981039346656037ull, a_stack.baseFormId);
        h = detail::Mix(h, key.size());
        for (const auto& p : key) h = detail::Mix(detail::Mix(h, p.mod), p.rank);
        return detail::Mix(detail::Mix(h, a_stack.count), a_stack.equipped ? 1 : 0);
    }

    struct ModFacts
    {
        std::uint8_t targetFormType = 0;  // 0 = any
        std::uint32_t attachPoint = 0;    // 0 = null
        std::uint32_t maxRank = 1;
    };

    enum class CompatResult : std::uint8_t { Ok, WrongFormType, WrongSlot, NotAttachable, RankUnsupported };

    // Constraint: a_attachParents must hold the base weapon's APPA keywords plus those of enabled installed mods.
    [[nodiscard]] inline CompatResult CheckCompat(const ModFacts& a_mod, std::uint8_t a_baseFormType,
        std::uint32_t a_slot, std::span<const std::uint32_t> a_attachParents)
    {
        if (a_mod.targetFormType != 0 && a_mod.targetFormType != a_baseFormType) return CompatResult::WrongFormType;
        if (a_mod.attachPoint == 0 || a_mod.attachPoint != a_slot) return CompatResult::WrongSlot;
        if (std::ranges::find(a_attachParents, a_slot) == a_attachParents.end()) return CompatResult::NotAttachable;
        if (a_mod.maxRank >= 2) return CompatResult::RankUnsupported;
        return CompatResult::Ok;
    }

    // Constraint: a_newConfig must be the configuration the action will produce, not the current one.
    [[nodiscard]] inline bool PartReturns(const PartKey& a_part, bool a_hasLooseForm, bool a_isNullMod,
        std::span<const PartKey> a_newConfig)
    {
        return a_part.rank == 1 && a_hasLooseForm && !a_isNullMod
            && std::ranges::find(a_newConfig, a_part) == a_newConfig.end();
    }

    enum class Action : std::uint8_t { SelectWeapon, Install, Remove };

    enum class Reason : std::uint8_t
    {
        Ok, Disabled, ViewClosed, SessionMismatch, MenuClosed, PlayerUnavailable, Busy, StaleRevision,
        BadId, UnknownWeapon, StaleStack, RemoveUnsupported, UnknownSlot, UnknownPart, AlreadyInstalled,
        Incompatible, NoLooseItem, LooseRankedStack, WeaponLocked, WeaponTemporaryStack, WeaponRankedPart,
        InstallUnavailable,
    };

    struct ActionRequest
    {
        Action action = Action::SelectWeapon;
        std::string session;
        std::uint64_t revision = 0;
        std::string requestId;
        std::string weaponId;
        std::string slotId;
        std::string partId;
    };

    struct ChoiceFacts
    {
        PartKey part;
        ModFacts mod;
        std::uint32_t looseCount = 0;  // rank-1 loose items only
        bool looseHasRankedStack = false;
    };

    struct SlotFacts
    {
        std::uint32_t keyword = 0;
        std::optional<PartKey> installed;
        std::vector<ChoiceFacts> choices;
    };

    struct WeaponFacts
    {
        std::uint64_t registryFingerprint = 0;
        std::uint64_t liveFingerprint = 0;
        bool locked = false;
        bool hasTemporaryStack = false;
        bool hasRankedInstalled = false;  // any installed part with rank > 1
        std::uint8_t baseFormType = 0;
        std::vector<std::uint32_t> attachParents;
        std::vector<SlotFacts> slots;
    };

    struct LiveState
    {
        bool enabled = false;
        bool viewAlive = false;
        bool menuAlive = false;
        bool playerCanAct = false;
        bool busy = false;
        std::string session;
        std::uint64_t revision = 0;
        std::optional<WeaponFacts> weapon;  // registry entry for request.weaponId, if any
    };

    // Constraint: looseCount, looseHasRankedStack, locked, hasTemporaryStack and hasRankedInstalled must be read live at action time, not from the snapshot.
    [[nodiscard]] inline Reason ValidateAction(const ActionRequest& a_req, const LiveState& a_state)
    {
        if (!a_state.enabled) return Reason::Disabled;
        if (!a_state.viewAlive) return Reason::ViewClosed;
        if (a_state.session.empty() || a_req.session != a_state.session) return Reason::SessionMismatch;
        if (!a_state.menuAlive) return Reason::MenuClosed;
        if (!a_state.playerCanAct) return Reason::PlayerUnavailable;
        if (a_state.busy) return Reason::Busy;
        if (a_req.revision != a_state.revision) return Reason::StaleRevision;
        if (!DecodeWeaponId(a_req.weaponId)) return Reason::BadId;
        if (!a_state.weapon) return Reason::UnknownWeapon;
        const WeaponFacts& w = *a_state.weapon;
        if (w.registryFingerprint != w.liveFingerprint) return Reason::StaleStack;
        if (a_req.action == Action::SelectWeapon) return Reason::Ok;
        if (a_req.action == Action::Remove) return Reason::RemoveUnsupported;

        const auto keyword = DecodeSlotId(a_req.slotId);
        const auto part = DecodePartId(a_req.partId);
        if (!keyword || !part) return Reason::BadId;
        const auto slot = std::ranges::find(w.slots, *keyword, &SlotFacts::keyword);
        if (slot == w.slots.end()) return Reason::UnknownSlot;
        const auto choice = std::ranges::find(slot->choices, *part, &ChoiceFacts::part);
        if (choice == slot->choices.end()) return Reason::UnknownPart;
        if (slot->installed == part) return Reason::AlreadyInstalled;
        if (part->rank != 1 || CheckCompat(choice->mod, w.baseFormType, *keyword, w.attachParents) != CompatResult::Ok) {
            return Reason::Incompatible;
        }
        if (slot->installed && slot->installed->rank > 1) return Reason::WeaponRankedPart;
        if (choice->looseCount < 1) return Reason::NoLooseItem;
        if (choice->looseHasRankedStack) return Reason::LooseRankedStack;
        if (w.locked) return Reason::WeaponLocked;
        if (w.hasTemporaryStack) return Reason::WeaponTemporaryStack;
        if (w.hasRankedInstalled) return Reason::WeaponRankedPart;
        return Reason::Ok;
    }

    using ConfigCounts = std::map<ConfigKey, std::uint32_t>;  // item count per configuration

    // Constraint: a_c0 must lose exactly one item, exactly one other configuration must gain exactly one, and nothing else may change.
    [[nodiscard]] inline std::optional<ConfigKey> ResolveModdedConfig(
        const ConfigCounts& a_pre, const ConfigCounts& a_post, const ConfigKey& a_c0)
    {
        std::map<ConfigKey, std::int64_t> delta;
        for (const auto& [key, n] : a_pre) delta[key] -= n;
        for (const auto& [key, n] : a_post) delta[key] += n;
        const auto pre0 = a_pre.find(a_c0);
        if (pre0 == a_pre.end() || pre0->second < 1 || delta[a_c0] != -1) return std::nullopt;
        std::optional<ConfigKey> c1;
        for (const auto& [key, d] : delta) {
            if (key == a_c0 || d == 0) continue;
            if (d != 1 || c1) return std::nullopt;
            c1 = key;
        }
        return c1;
    }

    struct StackEntry
    {
        std::uint32_t index = 0;
        ConfigKey key;
        std::uint32_t count = 0;
    };

    // Constraint: stacks are matched by (config, count) value because a merge renumbers indices between a_pre and a_post.
    [[nodiscard]] inline std::optional<std::uint32_t> PickModdedStack(std::span<const StackEntry> a_pre,
        std::span<const StackEntry> a_post, const ConfigKey& a_c1, std::optional<std::uint32_t> a_preferredIndex)
    {
        std::vector<std::uint32_t> unmatched;
        for (const auto& e : a_pre) {
            if (e.key == a_c1) unmatched.push_back(e.count);
        }
        std::optional<std::uint32_t> first, rose;
        for (const auto& e : a_post) {
            if (e.key != a_c1) continue;
            if (a_preferredIndex && e.index == *a_preferredIndex) return e.index;
            if (!first) first = e.index;
            const auto match = std::ranges::find(unmatched, e.count);
            if (match != unmatched.end()) unmatched.erase(match);
            else if (!rose) rose = e.index;
        }
        return rose ? rose : first;
    }

    struct LooseStack
    {
        std::uint32_t count = 0;
    };

    [[nodiscard]] inline std::optional<std::size_t> PickLooseStack(std::span<const LooseStack> a_stacks)
    {
        for (std::size_t i = 0; i < a_stacks.size(); ++i) {
            if (a_stacks[i].count >= 1) return i;
        }
        return std::nullopt;
    }
}
