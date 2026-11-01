#include "WorkbenchUpgrades.h"
#include "WorkbenchDefinitions.h"

#include "DebugLog.h"

#include "CobjCache.h"
#include "CraftingConfig.h"
#include "Paths.h"
#include "SimpleIni.h"

#include <RE/B/BGSComponent.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESBoundObject.h>

#include <F4SE/F4SE.h>
#include <F4SE/Interfaces.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WorkbenchUpgrades
{
    namespace
    {

        constexpr std::uint32_t kRecType = 'WBUP';
        constexpr std::uint32_t kRecVer  = 1;

        using Tiers = std::array<int, 3>;

        std::vector<BenchDef>                       s_benches;
        std::unordered_map<std::string, BenchState> s_state;
        std::unordered_map<std::string, int>        s_defaultLevel;
        std::unordered_map<std::string, Tiers>      s_tiers;
        std::unordered_map<std::string, std::size_t> s_benchIndex;
        std::unordered_map<std::string, std::string> s_categoryBench;
        bool                                        s_inited = false;
        std::atomic<bool>                           s_unlockAll{ false };
        std::recursive_mutex                        s_stateLock;

        Tiers DefaultTiers(const std::string& id)
        {
            if (id == "weapon")    return { 30, 80,  180 };
            if (id == "munitions") return { 8,  25,  60  };
            if (id == "armor")     return { 30, 80,  180 };
            if (id == "chem")      return { 8,  25,  60  };
            if (id == "utility")   return { 15, 45,  120 };
            return { 30, 80, 180 };
        }

        void ReadIniDefaults()
        {
            s_defaultLevel.clear();
            s_tiers.clear();
            auto path = Paths::F4SEUserDir() / "PrismaCrafting.ini";
            CSimpleIniA ini; ini.SetUnicode();
            const SI_Error rc = ini.LoadFile(path.c_str());
            if (rc < 0) {
                logger::warn("WorkbenchUpgrades: could not read PrismaCrafting.ini (rc={}): {} -- all defaults in effect",
                             rc, rc == SI_FILE ? "file missing or unreadable" : "parse error");
            }

            s_unlockAll.store(ini.GetBoolValue("Workbench", "UnlockAllBenches", false), std::memory_order_release);
            DLOG(Bench, "ini: [Workbench]UnlockAllBenches={} (from PrismaCrafting.ini)", s_unlockAll.load());
            for (const char* id : { "weapon", "munitions", "armor", "chem", "utility" }) {

                long v = ini.GetLongValue("Workbench", id, 0);
                if (v < 0) v = 0; if (v > 3) v = 3;
                s_defaultLevel[id] = static_cast<int>(v);

                DLOG(Bench, "ini: [Workbench]{}={} (starting tier)", id, v);
                Tiers t = DefaultTiers(id);
                const char* csv = ini.GetValue("WorkbenchTiers", id, nullptr);
                if (csv && csv[0]) {
                    int parsed[3] = { t[0], t[1], t[2] }; int n = 0;
                    const char* p = csv;
                    while (*p && n < 3) {
                        char* end = nullptr;
                        long val = std::strtol(p, &end, 10);
                        if (end == p) break;
                        parsed[n++] = static_cast<int>(val);
                        p = end;
                        if (n < 3) while (*p && (*p == ',' || *p == ' ')) ++p;
                    }

                    while (*p == ' ' || *p == '\t') ++p;
                    const bool consumed = (*p == '\0');
                    if (n == 3 && consumed &&
                        parsed[0] >= 1 && parsed[1] > parsed[0] && parsed[2] > parsed[1] && parsed[2] <= 1000000) {
                        t = { parsed[0], parsed[1], parsed[2] };
                    } else {
                        logger::warn("WorkbenchUpgrades: [WorkbenchTiers] {} = '{}' must be exactly 3 strictly-ascending positive cutoffs (<=1000000); using defaults", id, csv);
                    }
                }
                s_tiers[id] = t;
            }
        }
        int DefaultLevel(const std::string& id)
        {
            auto it = s_defaultLevel.find(id);
            return it == s_defaultLevel.end() ? 0 : it->second;
        }

        const BenchDef* FindBench(const std::string& id)
        {
            const auto it = s_benchIndex.find(id);
            if (it == s_benchIndex.end() || it->second >= s_benches.size()) return nullptr;
            return &s_benches[it->second];
        }

        int MaxLevel(const std::string& id)
        {
            const auto* def = FindBench(id);
            return def ? static_cast<int>(def->levels.size()) : -1;
        }

        std::uint32_t BenchCode(const std::string& id)
        {
            if (id == "weapon")    return 'WEAP';
            if (id == "munitions") return 'MUNI';
            if (id == "armor")     return 'ARMR';
            if (id == "chem")      return 'CHEM';
            if (id == "utility")   return 'UTIL';
            return 0;
        }
        std::string BenchIdFromCode(std::uint32_t code)
        {
            switch (code) {
            case 'WEAP': return "weapon";
            case 'MUNI': return "munitions";
            case 'ARMR': return "armor";
            case 'CHEM': return "chem";
            case 'UTIL': return "utility";
            default:     return "";
            }
        }

        constexpr std::uint64_t kClockSlackSec       = 60;

        constexpr std::uint64_t kMaxSaneRemainingSec = 7ull * 24 * 60 * 60;

        std::uint64_t EpochNow()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
        }

        CostItem C(const char* name, const char* ref, std::uint32_t n)
        {
            CostItem c; c.name = name; c.ref = ref; c.count = n; return c;
        }
        const char* kSteel     = "Fallout4.esm|01FABD";
        const char* kScrews    = "Fallout4.esm|03D294";
        const char* kAluminum  = "Fallout4.esm|01FA91";
        const char* kGears     = "Fallout4.esm|01FAB0";
        const char* kCircuitry = "Fallout4.esm|01FA9B";
        const char* kOil       = "Fallout4.esm|01FAB4";
        const char* kAdhesive  = "Fallout4.esm|01FAA5";
        const char* kCloth     = "Fallout4.esm|1223C7";
        const char* kLeather   = "Fallout4.esm|01FAAE";

        LevelDef MkLevel(int lvl, std::uint32_t timeSec, std::uint32_t caps,
                         const char* bonus, std::vector<CostItem> cost)
        {
            LevelDef d;
            d.level = lvl; d.timeSec = timeSec; d.caps = caps; d.bonus = bonus;
            d.cost = std::move(cost);
            return d;
        }

        const char* kNifWeapon = "Furniture\\Workstations\\WorkbenchWeapons\\WorkbenchWeaponsA.nif";
        const char* kNifArmor  = "Furniture\\Workstations\\WorkbenchArmor\\WorkstationArmorB01.nif";
        const char* kNifChem   = "Furniture\\Workstations\\WorkstationChemistryA01.nif";

        void BuildDefaults()
        {
            s_benches.clear();
            s_benchIndex.clear();
            s_categoryBench.clear();
            auto add = [](const char* id, const char* name, const char* desc, const char* nif,
                          std::vector<std::string> cats, std::vector<LevelDef> levels) {
                BenchDef b;
                b.id = id; b.name = name; b.desc = desc; b.nif = nif;
                b.categories = std::move(cats); b.levels = std::move(levels);
                const auto index = s_benches.size();
                s_benches.push_back(std::move(b));
                const auto& stored = s_benches[index];
                s_benchIndex.emplace(stored.id, index);
                for (const auto& category : stored.categories)
                    s_categoryBench.emplace(category, stored.id);
            };

            add("weapon", "Weapon Bench", "Gunsmith table for crafting and repairing weapons.", kNifWeapon,
                { "WEAPONS", "MELEE" }, {
                MkLevel(1, 3600,  500,  "Unlocks tier 1 weapons",
                    { C("Steel", kSteel, 25), C("Screws", kScrews, 10), C("Oil", kOil, 4) }),
                MkLevel(2, 14400, 1500, "Unlocks tier 2 (combat) weapons",
                    { C("Steel", kSteel, 40), C("Aluminum", kAluminum, 20), C("Gears", kGears, 12), C("Oil", kOil, 8) }),
                MkLevel(3, 43200, 4000, "Unlocks tier 3 (advanced) weapons",
                    { C("Aluminum", kAluminum, 35), C("Gears", kGears, 20), C("Circuitry", kCircuitry, 15), C("Oil", kOil, 15) }),
            });
            add("munitions", "Munitions Bench", "Reloading bench for ammunition and explosives.", kNifWeapon,
                { "AMMO", "GRENADES", "MINES" }, {
                MkLevel(1, 3600,  500,  "Unlocks tier 1 ammo",
                    { C("Steel", kSteel, 15), C("Screws", kScrews, 8) }),
                MkLevel(2, 14400, 1500, "Unlocks tier 2 ammo + explosives",
                    { C("Steel", kSteel, 30), C("Aluminum", kAluminum, 15), C("Circuitry", kCircuitry, 8) }),
                MkLevel(3, 43200, 4000, "Unlocks tier 3 (energy/heavy) ammo",
                    { C("Aluminum", kAluminum, 25), C("Circuitry", kCircuitry, 20), C("Gears", kGears, 12) }),
            });
            add("armor", "Armor Bench", "Workbench for fabricating armor, headgear and packs.", kNifArmor,
                { "ARMOR", "HEADGEAR", "BACKPACKS" }, {
                MkLevel(1, 3600,  500,  "Unlocks tier 1 armor",
                    { C("Cloth", kCloth, 20), C("Leather", kLeather, 15), C("Adhesive", kAdhesive, 6) }),
                MkLevel(2, 14400, 1500, "Unlocks tier 2 (combat) armor",
                    { C("Leather", kLeather, 25), C("Steel", kSteel, 20), C("Adhesive", kAdhesive, 10), C("Aluminum", kAluminum, 10) }),
                MkLevel(3, 43200, 4000, "Unlocks tier 3 (heavy/synth) armor",
                    { C("Aluminum", kAluminum, 30), C("Gears", kGears, 15), C("Circuitry", kCircuitry, 12), C("Adhesive", kAdhesive, 15) }),
            });
            add("chem", "Chem Station", "Lab for synthesizing medicine and chems.", kNifChem,
                { "HEALING", "DRUGS" }, {
                MkLevel(1, 3600,  500,  "Unlocks tier 1 medicine",
                    { C("Steel", kSteel, 10), C("Oil", kOil, 5), C("Cloth", kCloth, 10) }),
                MkLevel(2, 14400, 1500, "Unlocks tier 2 chems",
                    { C("Circuitry", kCircuitry, 8), C("Oil", kOil, 10), C("Aluminum", kAluminum, 10) }),
                MkLevel(3, 43200, 4000, "Unlocks tier 3 (advanced) chems",
                    { C("Circuitry", kCircuitry, 15), C("Gears", kGears, 10), C("Aluminum", kAluminum, 20) }),
            });
            add("utility", "Utility Bench", "General fabrication for gear, holotapes and parts.", kNifChem,
                { "UTILITY", "HOLOTAPES" }, {
                MkLevel(1, 3600,  500,  "Unlocks tier 1 gear",
                    { C("Steel", kSteel, 15), C("Screws", kScrews, 8), C("Adhesive", kAdhesive, 5) }),
                MkLevel(2, 14400, 1500, "Unlocks tier 2 gear + holotapes",
                    { C("Steel", kSteel, 25), C("Circuitry", kCircuitry, 10), C("Gears", kGears, 8) }),
                MkLevel(3, 43200, 4000, "Unlocks tier 3 gear",
                    { C("Aluminum", kAluminum, 20), C("Circuitry", kCircuitry, 18), C("Gears", kGears, 15) }),
            });

            for (const auto& b : s_benches)
                s_state[b.id] = BenchState{ DefaultLevel(b.id), 0 };
        }

        void ResolveCosts()
        {
            for (auto& b : s_benches)
                for (auto& lvl : b.levels)
                    for (auto& c : lvl.cost)
                        c.form = CraftingConfig::ResolveForm(c.ref);
        }

        std::uint64_t UpgradeDurationSec(const std::string& a_id, int a_level)
        {
            const auto* def = FindBench(a_id);
            if (!def || a_level < 0 || a_level >= static_cast<int>(def->levels.size())) return 0;
            return static_cast<std::uint64_t>(def->levels[a_level].timeSec);
        }

        void PollComplete()
        {
            std::lock_guard<std::recursive_mutex> lock(s_stateLock);
            const std::uint64_t now = EpochNow();
            for (auto& [id, st] : s_state) {
                if (st.upgradeCompleteAt == 0) continue;

                // Constraint: a forward wall-clock jump completes this early; monotonic timing needs a co-save format change.
                if (now >= st.upgradeCompleteAt) {
                    st.level += 1;
                    st.upgradeCompleteAt = 0;
                    logger::info("WorkbenchUpgrades: '{}' upgrade complete -> level {}", id, st.level);
                    continue;
                }

                const std::uint64_t remaining = st.upgradeCompleteAt - now;
                const std::uint64_t duration  = UpgradeDurationSec(id, st.level);
                if (duration != 0 && remaining > duration + kClockSlackSec) {
                    logger::warn("WorkbenchUpgrades: [CLOCK] '{}' reports {}s remaining but its upgrade "
                                 "only takes {}s -- the system clock moved backwards. Re-anchoring the "
                                 "deadline to now+{}s so the bench is not stranded in UPGRADING.",
                                 id, remaining, duration, duration);
                    st.upgradeCompleteAt = now + duration;
                } else if (duration == 0 && remaining > kMaxSaneRemainingSec) {
                    logger::warn("WorkbenchUpgrades: [CLOCK] '{}' reports {}s remaining with no known "
                                 "upgrade duration -- clearing the stuck timer and granting the level.",
                                 id, remaining);
                    st.level += 1;
                    st.upgradeCompleteAt = 0;
                }
            }
        }
    }

    void Init()
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        if (s_inited) return;
        s_inited = true;
        ReadIniDefaults();
        BuildDefaults();
        ResolveCosts();
        LoadBenchDefinitions(Paths::F4SEUserDir() / "PrismaCraftingWorkbenches.json", s_benches, s_categoryBench);
        logger::info("WorkbenchUpgrades: initialized {} benches (start levels w={} m={} a={} c={} u={})",
            s_benches.size(), DefaultLevel("weapon"), DefaultLevel("munitions"),
            DefaultLevel("armor"), DefaultLevel("chem"), DefaultLevel("utility"));
    }

    std::string BenchForCategory(const std::string& category)
    {
        const auto it = s_categoryBench.find(category);
        return it == s_categoryBench.end() ? std::string{} : it->second;
    }

    std::string BenchNameForCategory(const std::string& category)
    {
        const std::string id = BenchForCategory(category);
        const auto* def = id.empty() ? nullptr : FindBench(id);
        return def ? def->name : std::string{};
    }

    int RequiredLevel(const std::string& category, int score)
    {
        const std::string bench = BenchForCategory(category);
        if (bench.empty()) return 0;
        auto it = s_tiers.find(bench);
        const Tiers t = (it != s_tiers.end()) ? it->second : DefaultTiers(bench);
        if (score < t[0]) return 0;
        if (score < t[1]) return 1;
        if (score < t[2]) return 2;
        return 3;
    }

    int TopTierCutoff(const std::string& category)
    {
        const std::string bench = BenchForCategory(category);
        if (bench.empty()) return 0;
        auto it = s_tiers.find(bench);
        const Tiers t = (it != s_tiers.end()) ? it->second : DefaultTiers(bench);
        return t[2];
    }

    void Poll()
    {
        PollComplete();
    }

    int PlayerLevel(const std::string& benchId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        auto it = s_state.find(benchId);
        return it == s_state.end() ? 0 : it->second.level;
    }

    bool IsRecipeCraftable(const std::string& category, int createdValue)
    {
        const std::string bench = BenchForCategory(category);
        if (bench.empty()) return true;
        if (s_unlockAll.load(std::memory_order_acquire)) return true;
        return PlayerLevel(bench) >= RequiredLevel(category, createdValue);
    }

    bool UnlockAllBenches() { return s_unlockAll.load(std::memory_order_acquire); }

    void SetUnlockAll(bool a_unlock) { s_unlockAll.store(a_unlock, std::memory_order_release); }

    std::string LockReason(const std::string& category, int createdValue)
    {
        const std::string bench = BenchForCategory(category);
        if (bench.empty()) return "";
        const int need = RequiredLevel(category, createdValue);
        if (PlayerLevel(bench) >= need) return "";

        if (const auto* def = FindBench(bench)) {
            std::string nm = def->name;
            for (auto& ch : nm) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return nm + " Lv" + std::to_string(need);
        }
        return "BENCH Lv" + std::to_string(need);
    }

    const std::vector<BenchDef>& Benches() { return s_benches; }

    BenchState GetState(const std::string& benchId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        auto it = s_state.find(benchId);
        return it == s_state.end() ? BenchState{} : it->second;
    }

    std::uint64_t NowEpoch() { return EpochNow(); }

    void DebugSetLevel(const std::string& benchId, int level)
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        const int maxLevel = MaxLevel(benchId);
        if (maxLevel < 0) return;
        level = std::clamp(level, 0, maxLevel);
        s_state[benchId].level = level;
        s_state[benchId].upgradeCompleteAt = 0;
        logger::info("WorkbenchUpgrades: DEBUG set '{}' -> level {}", benchId, level);
    }

    bool StartUpgrade(const std::string& benchId,
                      const std::vector<RE::TESObjectREFR*>& sources,
                      std::string* outReason)
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        auto setReason = [&](const char* r) { if (outReason) *outReason = r; };

        PollComplete();

        const BenchDef* def = FindBench(benchId);
        if (!def)                            { setReason("NO_BENCH");  return false; }

        auto itSt = s_state.find(benchId);
        if (itSt == s_state.end())           { setReason("NO_BENCH");  return false; }
        BenchState& st = itSt->second;

        if (st.level < 0) { st.level = 0; st.upgradeCompleteAt = 0; }
        if (st.level >= static_cast<int>(def->levels.size())) { setReason("MAXED"); return false; }
        if (st.upgradeCompleteAt != 0)       { setReason("BUSY");      return false; }

        const LevelDef& L = def->levels[st.level];
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)                          { setReason("NEED_MATS"); return false; }

        const std::uint32_t caps = CobjCache::PlayerCaps();
        if (caps < L.caps)                    { setReason("NEED_CAPS"); return false; }
        auto* capsForm = L.caps > 0 ? RE::TESForm::GetFormByID(0x0000000F) : nullptr;
        if (L.caps > 0 && !capsForm) {
            logger::warn("StartUpgrade: Caps001 form (0x0F) not found; refusing before material debit");
            setReason("NEED_CAPS");
            return false;
        }

        std::vector<std::pair<RE::TESForm*, std::uint32_t>> costs;
        costs.reserve(L.cost.size());
        for (const auto& c : L.cost)
            if (c.count) costs.emplace_back(c.form, c.count);

        if (!CobjCache::ConsumeComponents(costs, player, sources, true) ||
            !CobjCache::ConsumeComponents(costs, player, sources)) {
            setReason("NEED_MATS");
            return false;
        }

        if (L.caps > 0) {
            RE::TESObjectREFR::RemoveItemData rm{ capsForm, static_cast<std::int32_t>(L.caps) };
            player->RemoveItem(rm);
        }

        st.upgradeCompleteAt = EpochNow() + L.timeSec;
        logger::info("WorkbenchUpgrades: '{}' upgrade STARTED -> level {} in {}s (caps -{}, completeAt {})",
            benchId, st.level + 1, L.timeSec, L.caps, st.upgradeCompleteAt);
        return true;
    }

    void CoSaveWrite(const F4SE::SerializationInterface* intfc)
    {
        if (!intfc) return;
        struct Persisted
        {
            std::uint32_t code;
            std::int32_t level;
            std::uint64_t at;
        };
        std::vector<Persisted> snapshot;
        {
            std::lock_guard<std::recursive_mutex> lock(s_stateLock);
            snapshot.reserve(s_state.size());
            for (const auto& [id, state] : s_state) {
                const auto code = BenchCode(id);
                if (code) snapshot.push_back({ code, state.level, state.upgradeCompleteAt });
            }
        }

        if (!intfc->OpenRecord(kRecType, kRecVer)) return;
        const auto count = static_cast<std::uint32_t>(snapshot.size());
        intfc->WriteRecordData(count);
        for (const auto& item : snapshot) {
            intfc->WriteRecordData(item.code);
            intfc->WriteRecordData(item.level);
            intfc->WriteRecordData(item.at);
        }
    }

    void ApplyState(const std::vector<std::pair<std::string, BenchState>>& staged,
                    const char* site)
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        for (const auto& [id, state] : staged) s_state[id] = state;
        PollComplete();
        logger::info("WorkbenchUpgrades: [COSAVE] applied {} bench state(s) ({})",
                     staged.size(), site);
    }

    bool CoSaveLoadRecord(std::uint32_t type, std::uint32_t version, std::uint32_t length,
                          const F4SE::SerializationInterface* intfc)
    {
        if (type != kRecType) return false;
        if (!intfc) return true;
        if (version != kRecVer) {
            logger::warn("WorkbenchUpgrades: unsupported co-save version {}, skipping", version);
            return true;
        }
        if (length < sizeof(std::uint32_t)) {
            logger::warn("WorkbenchUpgrades: co-save payload is too short ({} bytes)", length);
            return true;
        }

        std::uint32_t count = 0;
        if (intfc->ReadRecordData(count) != sizeof(count)) {
            logger::warn("WorkbenchUpgrades: failed to read co-save entry count");
            return true;
        }
        if (count > 5) {
            logger::warn("WorkbenchUpgrades: co-save record count {} looks corrupt, skipping", count);
            return true;
        }
        constexpr std::uint32_t entrySize = sizeof(std::uint32_t) + sizeof(std::int32_t) + sizeof(std::uint64_t);
        const auto expected = static_cast<std::uint64_t>(sizeof(std::uint32_t)) +
                              static_cast<std::uint64_t>(count) * entrySize;
        if (expected != length) {
            logger::warn("WorkbenchUpgrades: co-save length {} does not match {} entr{} (expected {})",
                         length, count, count == 1 ? "y" : "ies", expected);
            return true;
        }

        std::vector<std::pair<std::string, BenchState>> staged;
        staged.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t code = 0;
            std::int32_t level = 0;
            std::uint64_t at = 0;
            if (intfc->ReadRecordData(code) != sizeof(code) ||
                intfc->ReadRecordData(level) != sizeof(level) ||
                intfc->ReadRecordData(at) != sizeof(at)) {
                logger::warn("WorkbenchUpgrades: truncated co-save entry {}, skipping record", i);
                return true;
            }
            const auto id = BenchIdFromCode(code);
            if (id.empty()) {
                logger::warn("WorkbenchUpgrades: unknown co-save bench code {:08X}, skipping entry", code);
                continue;
            }
            const int maxLevel = MaxLevel(id);
            if (maxLevel < 0 || level < 0 || level > maxLevel) {
                logger::warn("WorkbenchUpgrades: invalid saved level {} for '{}', skipping entry", level, id);
                continue;
            }
            if (level == maxLevel) at = 0;
            staged.emplace_back(id, BenchState{ level, at });
        }

        ApplyState(staged, "load");
        return true;
    }

    void CoSaveRevert()
    {
        std::lock_guard<std::recursive_mutex> lock(s_stateLock);
        std::vector<std::pair<std::string, BenchState>> staged;
        staged.reserve(s_benches.size());
        for (const auto& bench : s_benches)
            staged.emplace_back(bench.id, BenchState{ DefaultLevel(bench.id), 0 });
        ApplyState(staged, "revert");
    }

}
