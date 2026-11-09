#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <REL/ID.h>
#include <REL/VariantID.h>
#include <REX/FModule.h>

namespace crafting::anchors
{
    enum class Kind : std::uint8_t
    {
        Function,
        Global
    };

    struct Result
    {
        const char* name = "";
        Kind        kind = Kind::Function;
        bool        resolved = false;
        bool        sane = false;
        std::uintptr_t relOffset = 0;
        std::uint8_t   first = 0;
        const char*    note = "";
    };

    template <class T>
    [[nodiscard]] inline Result Check(const char* a_name, Kind a_kind, const T& a_id)
    {
        Result r{};
        r.name = a_name;
        r.kind = a_kind;

        const auto addr = a_id.address();
        if (!addr) {
            r.note = "resolved to 0";
            return r;
        }

        const auto& module = REX::FModule::GetExecutingModule();
        const auto base = module.GetBaseAddress();
        r.resolved = true;
        r.relOffset = addr - base;

        const auto section = a_kind == Kind::Function ? ".text" : ".data";
        const auto sec = module.GetSection(section);
        const auto lo = sec.GetAddress();
        const auto hi = lo + sec.GetSize();
        if (!lo || !sec.GetSize()) {
            r.note = "could not read the section table";
            return r;
        }
        if (addr < lo || addr >= hi) {
            r.note = a_kind == Kind::Function ? "OUTSIDE .text" : "OUTSIDE .data";
            return r;
        }

        if (a_kind == Kind::Function) {
            r.first = *reinterpret_cast<const std::uint8_t*>(addr);
            if (r.first == 0xCC) {
                r.note = "points at 0xCC PADDING, not code";
                return r;
            }
            if (r.first == 0x00) {
                r.note = "points at a zero byte, not code";
                return r;
            }
        }

        r.sane = true;
        r.note = "ok";
        return r;
    }

    [[nodiscard]] bool LogAll();
    [[nodiscard]] bool ExecutableIsKnown();
}
