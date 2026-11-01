set_xmakever("3.0.0")

local commonlib_path = os.getenv("COMMONLIBF4_PATH")
if not commonlib_path or commonlib_path == "" then
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

local PRISMACRAFTING_VERSION = "1.2.0"

local PC_VER_MAJOR, PC_VER_MINOR, PC_VER_PATCH =
    PRISMACRAFTING_VERSION:match("^(%d+)%.(%d+)%.(%d+)$")
if not PC_VER_MAJOR then
    raise("PRISMACRAFTING_VERSION must be x.y.z, got: " .. tostring(PRISMACRAFTING_VERSION))
end

set_project("PrismaCrafting")
set_version(PRISMACRAFTING_VERSION)
set_languages("c++23")

set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.release")

add_requireconfs("spdlog", {configs = {header_only = false, wchar = true, std_format = true}, override = true})
add_requires("spdlog v1.16.0", "fmt", "nlohmann_json v3.12.0")

if is_host("linux") then
    add_requireconfs("**.cmake", {system = true, override = true})
end

target("PrismaCrafting")
    set_kind("shared")
    set_arch("x64")
    set_filename("PrismaCrafting.dll")
    set_version(PRISMACRAFTING_VERSION)
    set_runtimes("MD")

    add_defines("PRISMACRAFTING_VERSION_MAJOR=" .. PC_VER_MAJOR,
                "PRISMACRAFTING_VERSION_MINOR=" .. PC_VER_MINOR,
                "PRISMACRAFTING_VERSION_PATCH=" .. PC_VER_PATCH)

    add_deps("commonlibf4")
    add_packages("spdlog", "fmt", "nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

    set_pcxxheader("src/PCH.h")
    add_forceincludes("PCH.h", {sourcekinds = {"cxx"}})

    add_defines("COMMONLIB_RUNTIMECOUNT=3")
    if is_host("linux") then
        add_defines("_CRT_USE_BUILTIN_OFFSETOF")
    end

    add_syslinks("bcrypt", "version", "comctl32", "advapi32")
    set_targetdir("$(builddir)/windows/x64/release")
