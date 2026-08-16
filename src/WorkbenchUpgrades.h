#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace F4SE { class SerializationInterface; }
namespace RE { class TESForm; }

namespace WorkbenchUpgrades
{
    struct CostItem
    {
        std::string  ref;
        std::string  name;
        RE::TESForm* form  = nullptr;
        uint32_t     count = 0;
    };

    struct LevelDef
    {
        int                   level   = 0;
        uint32_t              timeSec = 0;
        uint32_t              caps    = 0;
        std::vector<CostItem> cost;
        std::string           bonus;
    };

    struct BenchDef
    {
        std::string              id;
        std::string              name;
        std::string              desc;
        std::string              nif;
        std::vector<std::string> categories;
        std::vector<LevelDef>    levels;
    };

    struct BenchState
    {
        int      level             = 0;
        uint64_t upgradeCompleteAt = 0;
    };

    void Init();

    std::string BenchForCategory(const std::string& category);

    std::string BenchNameForCategory(const std::string& category);

    int RequiredLevel(const std::string& category, int score);

    int TopTierCutoff(const std::string& category);

    void Poll();

    int PlayerLevel(const std::string& benchId);

    bool IsPowered(const std::string& benchId);

    bool IsRecipeCraftable(const std::string& category, int createdValue);

    bool UnlockAllBenches();

    std::string LockReason(const std::string& category, int createdValue);

    const std::vector<BenchDef>& Benches();

    BenchState GetState(const std::string& benchId);

    uint64_t NowEpoch();

    void DebugSetLevel(const std::string& benchId, int level);

    bool StartUpgrade(const std::string& benchId,
                      const std::vector<RE::TESObjectREFR*>& sources,
                      std::string* outReason = nullptr);

    void CoSaveWrite(const F4SE::SerializationInterface* intfc);
    bool CoSaveLoadRecord(uint32_t type, uint32_t version, uint32_t length,
                          const F4SE::SerializationInterface* intfc);
    void CoSaveRevert();
}
