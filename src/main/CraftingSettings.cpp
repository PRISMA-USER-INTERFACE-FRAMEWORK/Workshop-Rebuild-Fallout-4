#include "RuntimeInternal.h"
#include <aclapi.h>

std::filesystem::path SettingsIniPath()
{
    auto p = Paths::F4SEUserDir();
    p /= "PrismaCrafting.ini";
    return p;
}
static std::mutex g_settingsLock;

static bool LoadSettingsIni(CSimpleIniA& ini, bool& existing)
{
    ini.SetUnicode();
    const auto path = SettingsIniPath();
    const auto result = ini.LoadFile(path.c_str());
    if (result >= 0) { existing = true; return true; }
    std::error_code ec;
    existing = false;
    return result == SI_FILE && !std::filesystem::exists(path, ec) && !ec;
}

static bool CopySettingsDacl(const std::filesystem::path& path, const std::filesystem::path& temp)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const bool copied = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
        SetNamedSecurityInfoW(const_cast<wchar_t*>(temp.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED) ?
                PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
            nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
    LocalFree(descriptor);
    return copied;
}

static bool SaveSettingsIni(const CSimpleIniA& ini, bool existing)
{
    std::string data;
    if (ini.Save(data, true) < 0) return false;
    const auto path = SettingsIniPath();
    const auto temp = crafting::ini::MakeTempPath(path);
    std::error_code ec;
    if (existing) {
        if (!CopyFileW(path.c_str(), temp.c_str(), TRUE)) return false;
        if (!CopySettingsDacl(path, temp)) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    } else {
        const auto file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        if (!CloseHandle(file)) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::ofstream out(temp, std::ios::binary | std::ios::out | std::ios::in);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    out.close();
    if (out) std::filesystem::resize_file(temp, data.size(), ec);
    if (out && !ec && MoveFileExW(temp.c_str(), path.c_str(),
            MOVEFILE_WRITE_THROUGH | (existing ? MOVEFILE_REPLACE_EXISTING : 0)))
        return true;
    std::filesystem::remove(temp, ec);
    return false;
}
int LoadScale(const char* key)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    long v = ini.GetLongValue("UI", key, 100);
    return static_cast<int>(v < 50 ? 50 : (v > 200 ? 200 : v));
}
void SaveScale(const char* key, int pct)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    if (pct < 50) pct = 50; else if (pct > 200) pct = 200;
    CSimpleIniA ini;
    bool existing = false;
    if (!LoadSettingsIni(ini, existing) || ini.SetLongValue("UI", key, pct) < 0 || !SaveSettingsIni(ini, existing))
        logger::warn("PrismaCrafting: failed to write {} to PrismaCrafting.ini", key);
}

bool LoadInterceptFurnitureSwapperBenches()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    return ini.GetBoolValue("Compat", "InterceptFurnitureSwapperBenches", false);
}

std::string LoadTheme()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    return ini.GetValue("UI", "Theme", "tarkov");
}
void SaveTheme(const char* name)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    bool existing = false;
    if (!LoadSettingsIni(ini, existing) || ini.SetValue("UI", "Theme", (name && name[0]) ? name : "tarkov") < 0 ||
        !SaveSettingsIni(ini, existing))
        logger::warn("PrismaCrafting: failed to write Theme to PrismaCrafting.ini");
}
