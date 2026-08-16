set_xmakever("3.0.0")

-- Set COMMONLIBF4_PATH to a CommonLibF4 checkout, or place one next to this repo.
local commonlib_path = os.getenv("COMMONLIBF4_PATH")
if not commonlib_path then
    for _, candidate in ipairs({ "../CommonLibF4", "CommonLibF4" }) do
        if os.isdir(path.join(os.scriptdir(), candidate)) then
            commonlib_path = candidate
            break
        end
    end
end
if not commonlib_path then
    raise("CommonLibF4 not found. Set COMMONLIBF4_PATH to its directory.")
end
includes(commonlib_path)

set_project("PrismaCrafting")
set_version("1.0.0")
set_languages("c++23")

set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.release")

add_requires("spdlog v1.16.0", "fmt", "nlohmann_json")

-- When cross-building the Windows DLL from a Linux host, force xmake's package CMake calls to use
-- the system CMake so fmt and spdlog pass their compiler checks. No effect on a Windows host.
if is_host("linux") then
    add_requireconfs("**.cmake", {system = true, override = true})
end

target("PrismaCrafting")
    set_kind("shared")
    set_arch("x64")
    set_filename("PrismaCrafting.dll")

    add_deps("commonlibf4")
    add_packages("spdlog", "fmt", "nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

    set_pcxxheader("src/PCH.h")

    add_defines("COMMONLIB_RUNTIMECOUNT=3")

    add_syslinks("bcrypt", "version", "comctl32")

    set_targetdir("$(builddir)/windows/x64/release")
