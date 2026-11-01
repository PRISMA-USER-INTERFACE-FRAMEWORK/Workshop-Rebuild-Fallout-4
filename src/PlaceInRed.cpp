#include "PCH.h"

#include "PlaceInRed.h"

#include "DebugLog.h"
#include "WorkshopIDs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace PlaceInRed
{
    namespace
    {
        struct CompiledPattern
        {
            std::vector<std::uint8_t> bytes;
            std::vector<std::uint8_t> mask;
            std::size_t               anchor{ 0 };
            std::uint8_t              anchorValue{ 0 };
        };

        std::uint8_t HexNibble(char ch)
        {
            if (ch >= '0' && ch <= '9') return static_cast<std::uint8_t>(ch - '0');
            if (ch >= 'a' && ch <= 'f') return static_cast<std::uint8_t>(ch - 'a' + 10);
            if (ch >= 'A' && ch <= 'F') return static_cast<std::uint8_t>(ch - 'A' + 10);
            return 0xFF;
        }

        CompiledPattern Compile(std::string_view sig)
        {
            CompiledPattern out;
            for (std::size_t i = 0; i < sig.size(); ++i) {
                const char c = sig[i];
                if (c == ' ') continue;
                if (c == '?') {
                    out.bytes.push_back(0);
                    out.mask.push_back(0);
                    if (i + 1 < sig.size() && sig[i + 1] == '?') ++i;
                    continue;
                }
                if (i + 1 >= sig.size()) break;
                const auto hi = HexNibble(c), lo = HexNibble(sig[i + 1]);
                if (hi == 0xFF || lo == 0xFF) break;
                out.bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
                out.mask.push_back(0xFF);
                ++i;
            }
            for (std::size_t i = 0; i < out.bytes.size(); ++i) {
                if (out.mask[i]) {
                    out.anchor = i;
                    out.anchorValue = out.bytes[i];
                    break;
                }
            }
            return out;
        }

        bool MatchAt(const std::uint8_t* at, const CompiledPattern& pat)
        {
            for (std::size_t i = 0; i < pat.bytes.size(); ++i)
                if (pat.mask[i] && at[i] != pat.bytes[i]) return false;
            return true;
        }

        struct TextSection
        {
            const std::uint8_t* begin{ nullptr };
            std::size_t         size{ 0 };

            [[nodiscard]] const std::uint8_t* LastStart(std::size_t a_len) const
            {
                if (!begin || a_len == 0 || size < a_len) return nullptr;
                return begin + (size - a_len);
            }

            [[nodiscard]] bool Holds(const std::uint8_t* a_at, std::size_t a_len) const
            {
                return begin && a_at >= begin && a_len <= size && a_at <= begin + (size - a_len);
            }
        };

        TextSection Text()
        {
            const auto sec = REX::FModule::GetExecutingModule().GetSection(".text");
            return { reinterpret_cast<const std::uint8_t*>(sec.GetAddress()), sec.GetSize() };
        }

        std::uintptr_t ModuleBase()
        {
            return REX::FModule::GetExecutingModule().GetBaseAddress();
        }

        std::uintptr_t Scan(std::string_view sig, std::size_t& outCount)
        {
            outCount = 0;
            const auto text = Text();
            if (!text.begin || !text.size) return 0;

            const auto pat = Compile(sig);
            const std::size_t n = pat.bytes.size();
            const auto* last = text.LastStart(n);
            if (!last) return 0;
            const auto* cur = text.begin;
            std::uintptr_t first = 0;
            while (cur <= last) {
                const std::size_t starts = static_cast<std::size_t>(last - cur) + 1;
                const auto* hit = static_cast<const std::uint8_t*>(
                    std::memchr(cur + pat.anchor, pat.anchorValue, starts));
                if (!hit) break;
                const auto* start = hit - pat.anchor;
                if (MatchAt(start, pat)) {
                    if (++outCount == 1) first = reinterpret_cast<std::uintptr_t>(start);
                    if (outCount >= 2) break;
                }
                cur = start + 1;
            }
            return first;
        }

        std::uintptr_t FindUnique(const char* label, std::string_view sig, bool quietMiss = false)
        {
            std::size_t count = 0;
            const auto addr = Scan(sig, count);
            if (!addr) {
                if (!quietMiss) logger::warn("PlaceInRed: signature {} found nothing", label);
                return 0;
            }
            if (count > 1) {
                logger::warn("PlaceInRed: signature {} is not unique; refusing to patch", label);
                return 0;
            }
            return addr;
        }

        struct Edit
        {
            std::uintptr_t addr{ 0 };
            std::uint8_t   size{ 0 };
            std::uint8_t   patched[8]{};
            std::uint8_t   original[8]{};
        };

        std::vector<Edit> s_pir;
        std::atomic<bool> s_scanned{ false };
        std::atomic<bool> s_pirOn{ false };
        std::atomic<bool> s_groundSnapOn{ true };
        std::atomic<bool> s_objectSnapOn{ true };

        std::uintptr_t s_statusByte{ 0 };

        bool s_yellowMovement{ false };

        struct GroundEdit {
            std::uintptr_t addr{ 0 };
            std::uint8_t   original[5]{};
            std::uint8_t   patched[5]{};
        };
        std::vector<GroundEdit> s_groundEdits;

        std::uintptr_t s_objectSnapAddr{ 0 };
        std::uint8_t   s_objectSnapOriginal[8]{};
        constexpr std::array<std::uint8_t, 8> kObjectSnapDisabled{
            0x66, 0x0F, 0xEF, 0xF6, 0x0F, 0x1F, 0x40, 0x00
        };

        bool AddEdit(const char* label, std::uintptr_t addr,
                     std::span<const std::uint8_t> patched,
                     std::span<const std::uint8_t> expect)
        {
            if (!addr || patched.empty() || patched.size() > 8 || patched.size() != expect.size())
                return false;

            Edit e{};
            e.addr = addr;
            e.size = static_cast<std::uint8_t>(patched.size());
            std::memcpy(e.patched, patched.data(), patched.size());
            std::memcpy(e.original, reinterpret_cast<const void*>(addr), patched.size());
            if (std::memcmp(e.original, expect.data(), expect.size()) != 0) {
                logger::warn("PlaceInRed: {} at {:#x} has unexpected live bytes; refusing to patch",
                             label, addr - ModuleBase());
                return false;
            }
            s_pir.push_back(e);
            return true;
        }

        void ApplyPlaceEdits(bool patched)
        {
            for (const auto& e : s_pir)
                REL::WriteSafe(e.addr, patched ? e.patched : e.original, e.size);
        }

        std::uintptr_t Rip(std::uintptr_t site, std::size_t disp, std::size_t len)
        {
            const auto rel = *reinterpret_cast<const std::int32_t*>(site + disp);
            return site + len + static_cast<std::intptr_t>(rel);
        }

        struct ImmStore
        {
            std::uintptr_t addr{ 0 };
            std::uint8_t status{ 0 };
        };

        std::uintptr_t DiscoverStatusGlobal(std::vector<ImmStore>& out)
        {
            const auto text = Text();
            const auto* last = text.LastStart(7);
            if (!last) return 0;

            std::map<std::uintptr_t, std::vector<ImmStore>> byTarget;
            const auto* cur = text.begin;
            while (cur <= last) {
                const auto* hit = static_cast<const std::uint8_t*>(
                    std::memchr(cur, 0xC6, static_cast<std::size_t>(last - cur) + 1));
                if (!hit) break;
                if (hit[1] == 0x05) {
                    const auto addr = reinterpret_cast<std::uintptr_t>(hit);
                    const auto imm = hit[6];
                    if (imm <= 0x0D) byTarget[Rip(addr, 2, 7)].push_back({ addr, imm });
                }
                cur = hit + 1;
            }

            std::uintptr_t best = 0;
            std::size_t bestDistinct = 0;
            std::vector<ImmStore>* bestStores = nullptr;
            for (auto& [target, stores] : byTarget) {
                std::set<std::uint8_t> distinct;
                for (const auto& s : stores) distinct.insert(s.status);
                if (distinct.size() < 8 || !distinct.contains(0x01) || !distinct.contains(0x0A))
                    continue;
                if (distinct.size() > bestDistinct) {
                    bestDistinct = distinct.size();
                    best = target;
                    bestStores = &stores;
                }
            }
            if (!best) return 0;
            out = *bestStores;
            logger::info("PlaceInRed: placement status byte -> {:#x} ({} stores, {} distinct values)",
                         best - ModuleBase(), out.size(), bestDistinct);
            return best;
        }

        void PatchComputedStores(std::uintptr_t status)
        {
            static constexpr std::uint8_t kF1[]{ 0x33, 0xF6 };
            static constexpr std::uint8_t kF2[]{ 0x40, 0x32, 0xFF };
            const auto inert = [](const std::uint8_t* at, std::size_t n) {
                if (!n) return true;
                if (n == sizeof(kF1) && std::memcmp(at, kF1, sizeof(kF1)) == 0) return true;
                if (n == sizeof(kF2) && std::memcmp(at, kF2, sizeof(kF2)) == 0) return true;
                return false;
            };

            const auto text = Text();
            const auto* last = text.LastStart(6);
            if (!last) return;
            const auto* cur = text.begin;
            int patchedCount = 0;
            int missed = 0;

            while (cur <= last) {
                const auto* hit = static_cast<const std::uint8_t*>(
                    std::memchr(cur, 0x88, static_cast<std::size_t>(last - cur) + 1));
                if (!hit) break;
                cur = hit + 1;

                const auto modrm = hit[1];
                if (modrm != 0x05 && modrm != 0x0D) continue;
                const auto addr = reinterpret_cast<std::uintptr_t>(hit);
                if (Rip(addr, 2, 6) != status) continue;

                const bool isAl = modrm == 0x05;
                const std::uint8_t movImm = isAl ? 0xB0 : 0xB1;
                const std::uint8_t cmovDst = isAl ? 0xC1 : 0xC8;
                bool done = false;

                for (std::size_t back = 2; back <= 9 && !done; ++back) {
                    const auto* q = reinterpret_cast<const std::uint8_t*>(addr - back);
                    if (!text.Holds(q, back)) break;
                    if (q[0] != movImm || !inert(q + 2, back - 2)) continue;
                    const std::array<std::uint8_t, 1> replacement{ 0x01 };
                    const std::array<std::uint8_t, 1> expect{ q[1] };
                    if (AddEdit("computed status immediate", addr - back + 1, replacement, expect))
                        ++patchedCount;
                    done = true;
                }

                for (std::size_t back = 3; back <= 9 && !done; ++back) {
                    const auto* q = reinterpret_cast<const std::uint8_t*>(addr - back);
                    if (!text.Holds(q, back)) break;
                    if (q[0] != 0x0F || (q[1] != 0x44 && q[1] != 0x45)) continue;
                    if (q[2] != cmovDst && !(!isAl && q[2] == 0xC9)) continue;
                    if (!inert(q + 3, back - 3)) continue;

                    std::uintptr_t start = addr - back;
                    std::size_t len = 3;
                    if (q > text.begin) {
                        const auto rex = *reinterpret_cast<const std::uint8_t*>(start - 1);
                        if (rex >= 0x40 && rex <= 0x4F) {
                            --start;
                            ++len;
                        }
                    }

                    std::array<std::uint8_t, 8> replacement{};
                    replacement[0] = movImm;
                    replacement[1] = 0x01;
                    for (std::size_t i = 2; i < len; ++i) replacement[i] = 0x90;

                    std::array<std::uint8_t, 8> expect{};
                    std::memcpy(expect.data(), reinterpret_cast<const void*>(start), len);
                    if (AddEdit("computed status cmov", start,
                                std::span{ replacement.data(), len },
                                std::span{ expect.data(), len }))
                        ++patchedCount;
                    done = true;
                }

                if (!done) {
                    ++missed;
                    logger::warn("PlaceInRed: {} status store at {:#x} has an unrecognized loader; "
                                 "that path may still reject placement",
                                 isAl ? "AL" : "CL", addr - ModuleBase());
                }
            }

            logger::info("PlaceInRed: {} computed status writers armed ({} unrecognized)",
                         patchedCount, missed);
        }

        void DiscoverStatusNeighbors(std::uintptr_t status)
        {
            const auto text = Text();
            const auto* lastTint = text.LastStart(9);
            const auto* lastFlag = text.LastStart(8);
            if (!lastTint || !lastFlag) return;
            const auto adjacent = [status](std::uintptr_t target) {
                return target + 16 >= status && target <= status;
            };
            const std::array<std::uint8_t, 1> off{ 0x00 };
            const std::array<std::uint8_t, 1> on{ 0x01 };

            for (const auto* cur = text.begin; cur <= lastTint;) {
                const auto* hit = static_cast<const std::uint8_t*>(
                    std::memchr(cur, 0xC6, static_cast<std::size_t>(lastTint - cur) + 1));
                if (!hit) break;
                cur = hit + 1;
                if (hit[1] != 0x05 || hit[6] != 0x01 || hit[7] != 0x84 || hit[8] != 0xC0) continue;
                const auto addr = reinterpret_cast<std::uintptr_t>(hit);
                if (!adjacent(Rip(addr, 2, 7))) continue;
                AddEdit("red tint", addr + 6, off, on);
                break;
            }
            for (const auto* cur = text.begin; cur <= lastFlag;) {
                const auto* hit = static_cast<const std::uint8_t*>(
                    std::memchr(cur, 0xB2, static_cast<std::size_t>(lastFlag - cur) + 1));
                if (!hit) break;
                cur = hit + 1;
                if (hit[1] != 0x01 || hit[2] != 0x88 || hit[3] != 0x15) continue;
                const auto addr = reinterpret_cast<std::uintptr_t>(hit);
                if (!adjacent(Rip(addr, 4, 8))) continue;
                AddEdit("invalid flag", addr + 1, off, on);
                break;
            }
        }

        void DiscoverSnapControls()
        {

            struct GSite {
                const char*                  label;
                std::string_view             sig;
                std::array<std::uint8_t, 5>  patched;
            };
            static const GSite ogSites[] = {
                { "ground snap clamp",
                  "E8 ? ? ? ? 0F B6 F0 88 85 B8 16 00 00 E9",
                  { 0x30, 0xC0, 0x90, 0x90, 0x90 } },
                { "ground snap zcorr1",
                  "E8 ? ? ? ? F3 0F 10 05 49 E6 6E 05 41 0F 2E C2 74 0D",
                  { 0x90, 0x90, 0x90, 0x90, 0x90 } },
                { "ground snap zcorr2",
                  "E8 ? ? ? ? F3 44 0F 10 44 24 68 80 3D 11 D9 6E 05",
                  { 0x90, 0x90, 0x90, 0x90, 0x90 } },
            };

            static const GSite aeSites[] = {
                { "ground snap clamp",
                  "E8 ? ? ? ? 88 85 ? ? ? ? E9",
                  { 0x30, 0xC0, 0x90, 0x90, 0x90 } },
                { "ground snap zcorr1",
                  "E8 ? ? ? ? F3 0F 10 05 ? ? ? ? 41 0F 2E C3 74 0D",
                  { 0x90, 0x90, 0x90, 0x90, 0x90 } },
                { "ground snap zcorr2",
                  "E8 ? ? ? ? 80 3D ? ? ? ? 00 74 26 80 3D ? ? ? ? 00 75 26",
                  { 0x90, 0x90, 0x90, 0x90, 0x90 } },
            };

            const bool isOG = REX::FModule::IsRuntimeOG();
            const auto& gsites = isOG ? ogSites : aeSites;
            s_groundEdits.clear();
            for (const auto& s : gsites) {
                const auto a = FindUnique(s.label, s.sig);
                if (!a) { s_groundEdits.clear(); break; }
                GroundEdit e{};
                e.addr = a;
                std::memcpy(e.original, reinterpret_cast<const void*>(a), 5);
                std::memcpy(e.patched, s.patched.data(), 5);
                s_groundEdits.push_back(e);
            }
            if (s_groundEdits.size() == 3)
                logger::info("PlaceInRed: ground snap -> 3 call sites armed ({:#x} {:#x} {:#x})",
                             s_groundEdits[0].addr - ModuleBase(),
                             s_groundEdits[1].addr - ModuleBase(),
                             s_groundEdits[2].addr - ModuleBase());
            else {
                s_groundEdits.clear();
                logger::warn("PlaceInRed: ground-snap call sites incomplete; ground snap disabled");
            }

            const char* const objSnapSig = isOG
                ? "F3 0F 10 35 ? ? ? ? 33 F6 48 89 85 A0 00 00 00"
                : "F3 0F 10 35 ? ? ? ? 0F 28 C6 48 89 85 C0 00 00 00";
            if (const auto o = FindUnique("object snap", objSnapSig)) {
                s_objectSnapAddr = o;
                std::memcpy(s_objectSnapOriginal, reinterpret_cast<const void*>(o), 8);
                logger::info("PlaceInRed: object snap -> {:#x}", o - ModuleBase());
            }
        }

        void DiscoverIndependentPlaceEdits()
        {
            const auto sel = REL::Relocation<std::uintptr_t>(
                                 crafting::ids::WorkshopCanSelectRef).address() + 0x23;
            static constexpr std::array<std::uint8_t, 3> nop3{ 0x0F, 0x1F, 0x00 };
            static constexpr std::array<std::uint8_t, 3> original{ 0x8B, 0x58, 0x14 };
            s_yellowMovement = AddEdit("yellow object movement", sel, nop3, original);
        }
    }

    void Init()
    {
        if (s_scanned.exchange(true)) return;

        DiscoverSnapControls();
        DiscoverIndependentPlaceEdits();

        std::vector<ImmStore> stores;
        s_statusByte = DiscoverStatusGlobal(stores);
        if (!s_statusByte) {
            logger::critical("PlaceInRed: could not identify the placement-status byte; F3 disabled");
        } else {
            int fixedStores = 0;
            for (const auto& s : stores) {
                if (s.status <= 0x01) continue;
                const std::array<std::uint8_t, 1> replacement{ 0x01 };
                const std::array<std::uint8_t, 1> expect{ s.status };
                if (AddEdit("fixed placement status", s.addr + 6, replacement, expect)) ++fixedStores;
            }
            logger::info("PlaceInRed: {} fixed status writers armed", fixedStores);
            PatchComputedStores(s_statusByte);

            DiscoverStatusNeighbors(s_statusByte);
        }

        const auto caps = GetCapabilities();
        logger::info("PlaceInRed: {} capabilities | placement={} yellow={} "
                     "groundSnap={} objectSnap={} | {} place edits armed",
                     REX::FModule::IsRuntimeOG() ? "1.10.163" : "AE",
                     caps.placementStatus, caps.yellowMovement,
                     caps.groundSnap, caps.objectSnap, s_pir.size());
    }

    bool Available() { return s_statusByte != 0; }
    bool PlaceInRedOn() { return s_pirOn.load(); }
    bool YellowMovementAvailable() { return s_yellowMovement; }
    bool GroundSnapAvailable() { return s_groundEdits.size() == 3; }
    bool ObjectSnapAvailable() { return s_objectSnapAddr != 0; }

    Capabilities GetCapabilities()
    {
        return {
            s_statusByte != 0,
            s_yellowMovement,
            s_groundEdits.size() == 3,
            s_objectSnapAddr != 0,
        };
    }

    bool SetGroundSnap(bool enabled)
    {
        if (s_groundEdits.size() != 3) {
            DLOG(Placement, "SetGroundSnap({}) refused: {} of 3 call sites armed",
                 enabled, s_groundEdits.size());
            return false;
        }

        for (const auto& e : s_groundEdits) {
            const auto* live = reinterpret_cast<const std::uint8_t*>(e.addr);
            if (std::memcmp(live, e.original, 5) != 0 && std::memcmp(live, e.patched, 5) != 0) {
                logger::error("PlaceInRed: ground-snap site {:#x} changed after discovery; refusing write",
                              e.addr - ModuleBase());
                return false;
            }
        }
        for (const auto& e : s_groundEdits)
            REL::WriteSafe(e.addr, enabled ? e.original : e.patched, 5);
        s_groundSnapOn.store(enabled);
        logger::info("PlaceInRed: ground snap {}", enabled ? "enabled" : "disabled");
        return true;
    }

    bool SetObjectSnap(bool enabled)
    {
        if (!s_objectSnapAddr) {
            DLOG(Placement, "SetObjectSnap({}) refused: no object-snap site discovered", enabled);
            return false;
        }
        const auto* live = reinterpret_cast<const std::uint8_t*>(s_objectSnapAddr);
        const bool isOriginal = std::memcmp(live, s_objectSnapOriginal, 8) == 0;
        const bool isDisabled = std::memcmp(live, kObjectSnapDisabled.data(), 8) == 0;
        if (!isOriginal && !isDisabled) {
            logger::error("PlaceInRed: object-snap site changed after discovery; refusing write");
            return false;
        }
        REL::WriteSafe(s_objectSnapAddr,
                       enabled ? s_objectSnapOriginal : kObjectSnapDisabled.data(), 8);
        s_objectSnapOn.store(enabled);
        logger::info("PlaceInRed: object snap {}", enabled ? "enabled" : "disabled");
        return true;
    }

    void HoldValid()
    {
        if (!s_statusByte || !s_pirOn.load()) return;
        auto* placement = RE::Workshop::GetCurrentPlacementItemData();
        if (!placement || !placement->placementItem.get()) {
            DLOG(Placement, "HoldValid: no placement item (data={} item={})",
                 placement != nullptr, placement && placement->placementItem.get() != nullptr);
            return;
        }
        *reinterpret_cast<volatile std::uint8_t*>(s_statusByte) = 0x01;
    }

    void SetPlaceInRed(bool on)
    {
        if (!s_statusByte || s_pirOn.load() == on) {
            DLOG(Placement, "SetPlaceInRed({}) no-op (statusByte={} current={})",
                 on, s_statusByte != 0, s_pirOn.load());
            return;
        }
        ApplyPlaceEdits(on);

        s_pirOn.store(on);
        logger::info("PlaceInRed: place in red {}", on ? "ON" : "OFF");
    }

    void RestoreAll()
    {
        SetPlaceInRed(false);
        SetGroundSnap(true);
        SetObjectSnap(true);
    }
}
