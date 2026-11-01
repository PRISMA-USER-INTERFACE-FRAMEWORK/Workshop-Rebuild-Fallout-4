#include "WorkshopInputRegions.h"

#include "DebugLog.h"
#include "PrismaFlatApi.h"
#include "PrismaGameThreadCompat.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>

namespace WorkshopInputRegions
{
    struct PanelRect { float x0, y0, x1, y1; };

    static PRISMA_UI_API::IVPrismaUI11** s_api = nullptr;
    static std::atomic<PrismaView>* s_view = nullptr;
    static HWND* s_hwnd = nullptr;
    static std::mutex s_rectMx;
    static std::vector<PanelRect> s_rects;

    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr, HWND* hwndPtr)
    {
        s_api = apiPtr;
        s_view = viewPtr;
        s_hwnd = hwndPtr;
    }

    bool InPanel(HWND hwnd, int x, int y)
    {
        RECT rc{};
        if (!GetClientRect(hwnd, &rc)) return false;
        const float w = static_cast<float>(rc.right), h = static_cast<float>(rc.bottom);
        if (w <= 0.0f || h <= 0.0f) return false;
        const float fx = x / w, fy = y / h;

        std::lock_guard<std::mutex> lk(s_rectMx);
        if (s_rects.empty()) return fy > 0.33f;
        for (const auto& r : s_rects)
            if (fx >= r.x0 && fx <= r.x1 && fy >= r.y0 && fy <= r.y1) return true;
        return false;
    }

    static void ApplyNow()
    {
        if (!s_api || !*s_api || !s_view) return;
        const PrismaView view = s_view->load();
        if (!view || !(*s_api)->IsValid(view) || !(*s_api)->HasFocus(view)) return;

        RECT rc{};
        if (!s_hwnd || !GetClientRect(*s_hwnd, &rc)) return;
        const int width = rc.right - rc.left, height = rc.bottom - rc.top;
        if (width <= 0 || height <= 0) return;

        std::vector<PRISMA_UI_API::InputRegion> regions;
        const auto clampUnit = [](float value) {
            if (!(value >= 0.0f)) return 0.0f;
            return value > 1.0f ? 1.0f : value;
        };
        {
            std::lock_guard<std::mutex> lk(s_rectMx);
            regions.reserve(s_rects.size());
            for (const auto& rect : s_rects) {
                const int left = static_cast<int>(clampUnit(rect.x0) * width);
                const int top = static_cast<int>(clampUnit(rect.y0) * height);
                const int right = static_cast<int>(clampUnit(rect.x1) * width);
                const int bottom = static_cast<int>(clampUnit(rect.y1) * height);
                if (right > left && bottom > top)
                    regions.push_back({ left, top, right - left, bottom - top });
            }
        }

        const auto* data = regions.empty() ? nullptr : regions.data();
        if (!PrismaFlatApi::SupportsInputRegions())
            logger::debug("WorkshopInputRegions: provider does not advertise InputRegions; publishing anyway");
        if (!(*s_api)->SetInputRegions(view, data, static_cast<std::uint32_t>(regions.size())))
            logger::warn("WorkshopInputRegions: failed to publish {} input region(s)", regions.size());
    }

    void Apply()
    {
        if (PrismaCraftingThreading::IsGameThread()) ApplyNow();
        else PrismaCraftingThreading::Dispatch(ApplyNow);
    }

    void Clear()
    {
        if (!s_api || !*s_api || !s_view) return;
        const PrismaView view = s_view->load();
        if (!view || !(*s_api)->IsValid(view) || !(*s_api)->HasFocus(view)) return;
        if (!(*s_api)->SetInputRegions(view, nullptr, 0))
            logger::warn("WorkshopInputRegions: failed to clear input regions");
    }

    void SetPanelRects(const char* json)
    {
        const auto input = nlohmann::json::parse(json ? json : "[]", nullptr, false);
        if (!input.is_array()) return;
        std::vector<PanelRect> parsed;
        for (const auto& rect : input) {
            if (!rect.is_array() || rect.size() != 4) return;
            float v[4] = {};
            for (std::size_t i = 0; i < 4; ++i) {
                if (!rect[i].is_number()) return;
                const double value = rect[i].get<double>();
                if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
                    value > std::numeric_limits<float>::max()) return;
                v[i] = static_cast<float>(value);
            }
            const PanelRect r{ (std::min)(v[0], v[2]), (std::min)(v[1], v[3]),
                               (std::max)(v[0], v[2]), (std::max)(v[1], v[3]) };
            if (r.x1 > r.x0 && r.y1 > r.y0) parsed.push_back(r);
        }
        {
            std::lock_guard<std::mutex> lk(s_rectMx);
            s_rects.swap(parsed);
        }
        Apply();
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lk(s_rectMx);
        s_rects.clear();
    }
}
