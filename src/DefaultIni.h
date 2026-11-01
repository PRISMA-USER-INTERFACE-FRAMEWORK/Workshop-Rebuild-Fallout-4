#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace crafting::ini
{
    inline constexpr const char* kDefaultIni =
        R"(; ============================================================================
;  PrismaCrafting
; ============================================================================
;  This file was generated automatically because none existed. It is safe to
;  edit -- PrismaCrafting will never overwrite it, including on update.
;
;  Every setting is commented out and shows the built-in default. Uncomment a
;  line (remove the leading ';') to change it. Delete this file to regenerate
;  it with the defaults for your current version.
;
;  Changes are read at game start. Restart Fallout 4 after editing.
; ============================================================================

; ----------------------------------------------------------------------------
;  [Debug] -- logging
; ----------------------------------------------------------------------------
;  The log is written to:
;      Documents\My Games\Fallout4\F4SE\PrismaCrafting.log
;
;  Turn Enabled on before reproducing a bug, then attach that file to your
;  report. Leave it off for normal play: debug logging writes a lot and is
;  flushed on every line.
[Debug]

;  Master switch. Off means no debug output at all, whatever the categories
;  below say.
;Enabled=0

;  Individual categories, for narrowing a log down to one subsystem.
;  Each is on by default when Enabled=1, except Input and Placement, which
;  fire every frame and would otherwise drown everything else -- switch those
;  on only when chasing an input or placement bug specifically.
;
;  These do nothing unless Enabled=1.
;  startup, config, menu open/close
;General=1
;  raw keyboard/mouse routing, search-box capture  (per-frame)
;Input=0
;  object ghost, ground snap, placement queue      (per-frame)
;Placement=0
;  free placement and free camera
;Workshop=1
;  recipe cache, category sync, queries
;Recipes=1
;  workbench upgrades and co-save
;Bench=1
;  game-thread task dispatch
;Dispatch=1
;  PrismaUI view lifecycle and the JS bridge
;View=1

; ----------------------------------------------------------------------------
;  [UI]
; ----------------------------------------------------------------------------
[UI]

;  Interface theme.
;Theme=tarkov

;  Zoom for the crafting and helper windows, as a percentage. 50-200.
;  Normally set in-game; stored here so it persists.
;CraftScale=100
;HelperScale=100

; ----------------------------------------------------------------------------
;  [Workshop] -- build-mode placement and camera
; ----------------------------------------------------------------------------
;  Keys accept either a single character (F, 7) or a Windows virtual-key code
;  in hex (0x72 is F3). A full VK list:
;  https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes
[Workshop]

;  Master switch for the whole free-placement system.
;Enabled=1

;  Toggle free placement / free camera.
;  F3
;FreePlaceKey=0x72
;  F4
;FreeCameraKey=0x73

;  Resize the held object.
;  = (plus)
;ScaleUpKey=0xBB
;  - (minus)
;ScaleDownKey=0xBD
;ScaleStep=0.05

;  Rotate the held object. Hold a key and move the mouse.
;  Numpad 4
;YawKey=0x64
;  Numpad 8
;PitchKey=0x68
;  Numpad 7
;RollKey=0x67
;RotateStepDegrees=15.0

;  Nudge the held object.
;MoveStepUnits=5.0

;  Cycle snapping modes, and reset the held object's transform.
;  F6
;SnapCycleKey=0x75
;  F7
;ResetKey=0x76

;  Free camera. Reach 0 keeps the vanilla limit.
;FreeCameraReachUnits=0.0
;FreeCameraSpeed=10.0
;FreeCameraFreezesTime=0

; ----------------------------------------------------------------------------
;  [Workbench] -- crafting bench tiers
; ----------------------------------------------------------------------------
[Workbench]

;  Ignore tier requirements and offer every recipe at every bench.
;UnlockAllBenches=0

;  Starting tier per bench type, 0-3.
;weapon=0
;munitions=0
;armor=0
;chem=0
;utility=0

; ----------------------------------------------------------------------------
;  [WorkbenchTiers] -- items crafted to reach tiers 1, 2 and 3
; ----------------------------------------------------------------------------
[WorkbenchTiers]

;weapon=30,80,180
;munitions=8,25,60
;armor=30,80,180
;chem=8,25,60
;utility=15,45,120

; ----------------------------------------------------------------------------
;  [Compat] -- other mods
; ----------------------------------------------------------------------------
[Compat]

;  Take over scripted furniture-swapper benches, such as ECO's Universal
;  Workbench. Off by default: those benches are driven by their own scripts,
;  and intercepting them can conflict with the mod that owns them. Turn it on
;  if you would rather use PrismaCrafting's menu there.
;InterceptFurnitureSwapperBenches=0

; ----------------------------------------------------------------------------
;  [Author] -- mod authoring
; ----------------------------------------------------------------------------
[Author]

;  Dump the resolved recipe/category configuration to disk at load, for
;  building or debugging a PrismaCrafting config. Not needed for playing.
;ExportConfig=0
)";

    enum class WriteResult
    {
        kCreated,          // the template is now on disk, complete
        kAlreadyExists,    // a file was already there and was not touched
        kCheckFailed,      // could not even determine whether one exists
        kDirectoryFailed,  // the parent directory could not be created
        kWriteFailed,      // the temporary could not be written or closed cleanly
        kPublishFailed,    // written, but could not be moved into place
    };

    [[nodiscard]] inline std::filesystem::path MakeTempPath(const std::filesystem::path& a_path)
    {
        static std::atomic<unsigned long long> attempt{ 0 };

#ifdef _WIN32
        const auto pid = static_cast<unsigned long long>(::_getpid());
#else
        const auto pid = static_cast<unsigned long long>(::getpid());
#endif

        std::ostringstream name;
        name << a_path.filename().string() << ".new-" << std::hex << pid << '-'
             << attempt.fetch_add(1, std::memory_order_relaxed);
        return a_path.parent_path() / name.str();
    }

    [[nodiscard]] inline WriteResult WriteDefaultIni(const std::filesystem::path& a_path,
                                                     std::string* a_detail = nullptr)
    {
        const auto note = [a_detail](const std::string& a_text) {
            if (a_detail) *a_detail = a_text;
        };

        std::error_code ec;
        if (std::filesystem::exists(a_path, ec)) return WriteResult::kAlreadyExists;
        if (ec) {
            note(ec.message());
            return WriteResult::kCheckFailed;
        }

        std::filesystem::create_directories(a_path.parent_path(), ec);
        if (ec) {
            note(ec.message());
            return WriteResult::kDirectoryFailed;
        }

        const auto temp = MakeTempPath(a_path);
        {
            std::ofstream out(temp, std::ios::binary | std::ios::noreplace);
            if (!out) {
                note("could not open temporary settings file");
                return WriteResult::kWriteFailed;
            }

            for (const char* p = kDefaultIni; *p; ++p) {
                if (*p == '\n') out.put('\r');
                out.put(*p);
            }

            out.flush();
            out.close();
            if (!out) {
                note("failed while writing temporary settings file");
                std::filesystem::remove(temp, ec);
                return WriteResult::kWriteFailed;
            }
        }

        if (std::filesystem::exists(a_path, ec)) {
            std::filesystem::remove(temp, ec);
            return WriteResult::kAlreadyExists;
        }

        std::filesystem::rename(temp, a_path, ec);
        if (ec) {
            note(ec.message());
            std::filesystem::remove(temp, ec);
            return WriteResult::kPublishFailed;
        }
        return WriteResult::kCreated;
    }

    bool EnsureSettingsIni(const std::filesystem::path& a_path);
}
