#include "TurntableRenderer.h"
#include "Paths.h"

#include <atomic>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

namespace fs = std::filesystem;

namespace
{
    struct Config
    {
        bool         enabled = true;
        std::wstring nifInfo;
        std::wstring texconv;
        std::wstring viewsDir;
        std::wstring dataDir;
        int          frames = 12;
        int          size   = 256;
    };

    Config            g_cfg;
    std::atomic<bool> g_inited{ false };
    std::mutex        g_mutex;
    std::set<std::string> g_inflight;

    std::wstring IniPath() { return (Paths::F4SEUserDir() / L"PrismaCrafting.ini").wstring(); }

    std::wstring ReadStr(const wchar_t* key, const std::wstring& def, const std::wstring& ini)
    {
        wchar_t buf[1024]{};
        GetPrivateProfileStringW(L"Turntable", key, def.c_str(), buf, 1024, ini.c_str());
        return buf;
    }

    std::string Narrow(const std::wstring& w)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, 0);
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
        return s;
    }
    std::wstring Widen(const std::string& s)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, 0);
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    }

    std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

    std::wstring MeshArg(const std::string& nif)
    {
        std::string p = nif;
        for (auto& c : p) { if (c == '\\') c = '/'; c = static_cast<char>(std::tolower((unsigned char)c)); }
        size_t s = p.find_first_not_of('/');
        p = (s == std::string::npos) ? std::string() : p.substr(s);
        if (p.rfind("meshes/", 0) != 0) p = "meshes/" + p;
        return Widen(p);
    }

    bool RunHidden(std::wstring cmd, DWORD timeoutMs)
    {
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return false;
        WaitForSingleObject(pi.hProcess, timeoutMs);
        DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return code == 0;
    }

    int RenderAll(const std::string& slug, const std::string& nifPath)
    {
        const fs::path dir = fs::path(g_cfg.viewsDir) / L"icons" / L"turntable" / fs::path(Widen(slug));
        std::error_code ec; fs::create_directories(dir, ec);
        const std::wstring mesh = MeshArg(nifPath);
        const std::wstring rdr  = L"-render" + std::to_wstring(g_cfg.size) + L"x" + std::to_wstring(g_cfg.size);

        int ok = 0;
        for (int i = 0; i < g_cfg.frames; ++i)
        {
            wchar_t nn[8]; swprintf(nn, 8, L"%02d", i);
            const fs::path dds = dir / (std::wstring(nn) + L".dds");
            const fs::path pngU = dir / (std::wstring(nn) + L".PNG");
            const fs::path png  = dir / (std::wstring(nn) + L".png");
            const int rz = i * 360 / g_cfg.frames;

            fs::remove(dds, ec);

            std::wstring c1 = Quote(g_cfg.nifInfo) + L" " + rdr + L" " + Quote(dds.wstring()) +
                L" -cam 1.0 0 0 0 " + std::to_wstring(rz) + L" -light 1.0 60 135 " +
                Quote(g_cfg.dataDir) + L" " + Quote(mesh);
            RunHidden(c1, 60000);
            if (!fs::exists(dds, ec) || fs::file_size(dds, ec) < 256) { fs::remove(dds, ec); continue; }

            std::wstring c2 = Quote(g_cfg.texconv) + L" -ft png -f R8G8B8A8_UNORM -o " +
                Quote(dir.wstring()) + L" -y " + Quote(dds.wstring());
            RunHidden(c2, 30000);
            fs::remove(dds, ec);

            std::error_code eqEc;
            if (!fs::equivalent(pngU, png, eqEc)) {
                fs::remove(png, ec);
                fs::rename(pngU, png, ec);
            }
            if (fs::exists(png, ec)) ++ok;
        }
        return ok;
    }
}

namespace TurntableRenderer
{
    void Init()
    {
        if (g_inited.exchange(true)) return;

        const auto cwd = fs::current_path();
        const std::wstring defNif =
            (cwd.parent_path() / L"Tools" / L"CraftingTurntable" / L"nif_info.exe").wstring();

        const std::wstring defViews = (cwd / L"Data" / L"PrismaUI_F4" / L"views").wstring();
        const std::wstring defData = (cwd / L"Data").wstring();

        const auto ini = IniPath();
        if (GetPrivateProfileIntW(L"Turntable", L"Enabled2", -1, ini.c_str()) == -1)
        {

            WritePrivateProfileStringW(L"Turntable", L"Enabled2", L"0",            ini.c_str());
            WritePrivateProfileStringW(L"Turntable", L"NifInfo",  defNif.c_str(),  ini.c_str());
            WritePrivateProfileStringW(L"Turntable", L"ViewsDir", defViews.c_str(),ini.c_str());
            WritePrivateProfileStringW(L"Turntable", L"DataDir",  defData.c_str(), ini.c_str());
            WritePrivateProfileStringW(L"Turntable", L"Frames",   L"12",           ini.c_str());
            WritePrivateProfileStringW(L"Turntable", L"Size",     L"256",          ini.c_str());
        }

        g_cfg.enabled  = GetPrivateProfileIntW(L"Turntable", L"Enabled2", 0, ini.c_str()) != 0;
        g_cfg.nifInfo  = ReadStr(L"NifInfo",  defNif,   ini);
        g_cfg.viewsDir = ReadStr(L"ViewsDir", defViews, ini);
        g_cfg.dataDir  = ReadStr(L"DataDir",  defData,  ini);
        g_cfg.frames   = GetPrivateProfileIntW(L"Turntable", L"Frames", 12, ini.c_str());
        g_cfg.size     = GetPrivateProfileIntW(L"Turntable", L"Size",  256, ini.c_str());
        g_cfg.texconv  = (fs::path(g_cfg.nifInfo).parent_path() / L"texconv.exe").wstring();

        if (!fs::exists(g_cfg.nifInfo) || !fs::exists(g_cfg.texconv))
            g_cfg.enabled = false;

        logger::info("Turntable: enabled={} nifinfo_exists={} texconv_exists={} frames={} size={} data='{}'",
                     g_cfg.enabled, fs::exists(g_cfg.nifInfo), fs::exists(g_cfg.texconv),
                     g_cfg.frames, g_cfg.size, Narrow(g_cfg.dataDir));
    }

    bool Enabled() { return g_inited.load() && g_cfg.enabled; }

    void Request(const std::string& slug, const std::string& nifPath, const std::string& )
    {
        if (!Enabled()) { logger::info("Turntable: ignored (disabled) slug={}", slug); return; }
        if (slug.empty() || nifPath.empty()) return;

        const fs::path dir = fs::path(g_cfg.viewsDir) / L"icons" / L"turntable" / fs::path(Widen(slug));
        std::error_code ec;
        if (fs::exists(dir / L"00.png", ec)) { logger::info("Turntable: cached {}", slug); return; }

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            if (!g_inflight.insert(slug).second) return;
        }
        logger::info("Turntable: rendering slug={} nif={}", slug, nifPath);

        std::thread([slug, nifPath]() {
            int n = RenderAll(slug, nifPath);
            logger::info("Turntable: finished slug={} frames={}", slug, n);
            std::lock_guard<std::mutex> lk(g_mutex);
            g_inflight.erase(slug);
        }).detach();
    }
}
