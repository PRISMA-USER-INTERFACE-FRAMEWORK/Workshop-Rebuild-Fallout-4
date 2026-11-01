#include "Paths.h"

#include <ShlObj.h>

#include <iterator>

namespace Paths
{
    const std::filesystem::path& F4SEUserDir()
    {
        static const std::filesystem::path dir = [] {
            std::filesystem::path resolved;
            PWSTR docs = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
                resolved = docs;
            if (docs) CoTaskMemFree(docs);

            if (resolved.empty()) resolved = std::filesystem::current_path();
            resolved /= L"My Games";
            resolved /= L"Fallout4";
            resolved /= L"F4SE";

            std::error_code ec;
            std::filesystem::create_directories(resolved, ec);
            return resolved;
        }();
        return dir;
    }

    const std::filesystem::path& JsonDataDir()
    {
        static const std::filesystem::path dir = [] {
            auto resolved = std::filesystem::current_path();
            resolved /= L"Data";
            resolved /= L"PrismaUI_F4";
            resolved /= L"JsonData";

            std::error_code ec;
            std::filesystem::create_directories(resolved, ec);
            return resolved;
        }();
        return dir;
    }

    const std::filesystem::path& LogDir()
    {
        static const std::filesystem::path dir = [] {
            std::filesystem::path resolved;
            HMODULE hm = nullptr;
            if (::GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&LogDir), &hm) && hm) {
                wchar_t buf[32768]{};
                if (::GetModuleFileNameW(hm, buf, static_cast<DWORD>(std::size(buf)))) {
                    resolved = std::filesystem::path(buf).parent_path().parent_path().parent_path() / L"logs";
                }
            }
            if (resolved.empty()) resolved = F4SEUserDir() / L"logs";

            std::error_code ec;
            std::filesystem::create_directories(resolved, ec);
            return resolved;
        }();
        return dir;
    }
}
