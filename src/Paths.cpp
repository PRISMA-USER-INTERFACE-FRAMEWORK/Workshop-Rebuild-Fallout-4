#include "Paths.h"

#include <ShlObj.h>

namespace Paths
{
    std::filesystem::path F4SEUserDir()
    {
        std::filesystem::path dir;
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
            dir = docs;
        if (docs) CoTaskMemFree(docs);

        if (dir.empty()) dir = std::filesystem::current_path();
        dir /= L"My Games";
        dir /= L"Fallout4";
        dir /= L"F4SE";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::filesystem::path JsonDataDir()
    {
        auto dir = std::filesystem::current_path();
        dir /= L"Data";
        dir /= L"PrismaUI_F4";
        dir /= L"JsonData";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::filesystem::path LogDir()
    {
        std::filesystem::path dir;
        HMODULE hm = nullptr;
        if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&LogDir), &hm) && hm) {
            wchar_t buf[MAX_PATH]{};
            if (::GetModuleFileNameW(hm, buf, MAX_PATH)) {

                dir = std::filesystem::path(buf).parent_path().parent_path().parent_path() / L"logs";
            }
        }
        if (dir.empty()) dir = F4SEUserDir() / L"logs";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
}
