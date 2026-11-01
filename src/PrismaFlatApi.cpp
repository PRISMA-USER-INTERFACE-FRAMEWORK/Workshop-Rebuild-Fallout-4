#include "PrismaFlatApi.h"

#include "DebugLog.h"

#include <atomic>

namespace PrismaFlatApi
{
    namespace
    {
        namespace Flat = PRISMA_UI_FLAT_API;

        std::atomic<bool> s_metaReady{ false };
        std::atomic<bool> s_viewReady{ false };
        Flat::MetaAPI s_meta{};
        Flat::ViewAPI s_view{};
    }

    bool Init() noexcept
    {
        const bool meta = Flat::Discover<Flat::ApiFeature::Meta>(Flat::MetaApiVersion, s_meta);
        const bool view = Flat::Discover<Flat::ApiFeature::View>(Flat::ViewApiVersion, s_view);

        s_metaReady.store(meta, std::memory_order_release);
        s_viewReady.store(view, std::memory_order_release);

        logger::info("PrismaFlatApi: modern tables meta={} view={}", meta, view);
        return meta || view;
    }

    bool HasFeature(Flat::ApiFeature a_feature, std::uint32_t a_version) noexcept
    {
        if (!s_metaReady.load(std::memory_order_acquire)) return false;
        return Flat::HasCapability(s_meta, a_feature, a_version);
    }

    bool SetCursorPolicy(PrismaView a_view, PRISMA_UI_API::CursorPolicy a_policy) noexcept
    {
        if (!a_view) return false;

        if (s_viewReady.load(std::memory_order_acquire) && s_view.SetViewCursorPolicy) {
            return s_view.SetViewCursorPolicy(a_view, static_cast<Flat::CursorPolicy>(a_policy));
        }

        return PRISMA_UI_API::SetViewCursorPolicy(a_view, a_policy);
    }

    PRISMA_UI_API::CursorPolicy GetCursorPolicy(PrismaView a_view) noexcept
    {
        if (!a_view) return PRISMA_UI_API::CursorPolicy::Default;

        if (s_viewReady.load(std::memory_order_acquire) && s_view.GetViewCursorPolicy) {
            return static_cast<PRISMA_UI_API::CursorPolicy>(s_view.GetViewCursorPolicy(a_view));
        }

        return PRISMA_UI_API::GetViewCursorPolicy(a_view);
    }

    bool SupportsInputRegions() noexcept
    {
        if (HasFeature(Flat::ApiFeature::Input, Flat::InputApiVersion)) return true;
        return PRISMA_UI_API::HasPrismaCapability(PRISMA_UI_API::PrismaCapability::InputRegions);
    }

    bool RegisterTranslations(PrismaView a_view) noexcept
    {
        Flat::LocalizationAPI table{};
        return a_view && Flat::Discover<Flat::ApiFeature::Localization>(Flat::LocalizationApiVersion, table) &&
               table.RegisterTranslationsV4 && table.RegisterTranslationsV4(a_view, "PrismaCrafting");
    }
}
