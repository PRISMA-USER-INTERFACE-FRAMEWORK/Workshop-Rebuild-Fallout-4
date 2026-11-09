#include "PCH.h"

#include "AnchorCheck.h"

#include "WorkshopIDs.h"

namespace crafting::anchors
{
    namespace
    {
        bool UserPtr(std::uintptr_t p)
        {
            return p == 0 || (p >= 0x10000 && p < 0x800000000000ull && (p & 7) == 0);
        }

        // Constraint: reads the resolved globals only, never dereferences pointers inside them.
        bool GlobalsHoldPlausibleData()
        {
            bool ok = true;
            const auto fail = [&](const char* what) {
                logger::error("PrismaCrafting: [ANCHORS]  FAIL  content {}", what);
                ok = false;
            };

            const auto* node = reinterpret_cast<const RE::Workshop::WorkshopMenuNode*>(ids::WorkshopMenuRoot.address());
            if (!UserPtr(reinterpret_cast<std::uintptr_t>(node->filterKeyword)) ||
                !UserPtr(reinterpret_cast<std::uintptr_t>(node->recipe)) ||
                !UserPtr(reinterpret_cast<std::uintptr_t>(node->form)))
                fail("WorkshopMenuRoot node holds a non-pointer where a form pointer belongs");
            if (node->parent != nullptr)
                fail("WorkshopMenuRoot node has a parent, so it is not the root");
            if (*reinterpret_cast<const std::uint8_t*>(&node->selected) > 1)
                fail("WorkshopMenuRoot node flag byte is neither 0 nor 1");
            const auto size = node->children.size();
            const auto cap = node->children.capacity();
            const auto data = reinterpret_cast<std::uintptr_t>(node->children.data());
            if (size > cap || size > 100000 || !UserPtr(data) || (cap > 0 && !data))
                fail("WorkshopMenuRoot child array is inconsistent");

            if (*reinterpret_cast<const std::uint8_t*>(ids::ActiveEdit.address()) > 1)
                fail("ActiveEdit is not a bool");
            if (*reinterpret_cast<const std::uint8_t*>(ids::MultiSelect.address()) > 1)
                fail("MultiSelect is not a bool");

            return ok;
        }
    }

    bool LogAll()
    {
        const auto& module = REX::FModule::GetExecutingModule();
        const auto ver = module.GetFileVersion();

        if (!ids::WorkshopDataResolvable()) {
            logger::warn("PrismaCrafting: [ANCHORS] runtime {}.{}.{}.{} has no engine anchor "
                         "mappings -- the workshop features are unavailable here and nothing was "
                         "resolved. The settlement workshop paths work on Next-Gen "
                         "1.10.984 only; 1.10.980 has no workshop global addresses yet.",
                         ver.major(), ver.minor(), ver.patch(), ver.build());
            return false;
        }

        std::vector<Result> results;
        const auto probe = [&](const char* n, Kind k, const auto& id) {
            logger::info("PrismaCrafting: [ANCHORS] resolving {}", n);
            logger::default_logger()->flush();
            results.push_back(Check(n, k, id));
        };
        const auto fn = [&](const char* n, const auto& id) { probe(n, Kind::Function, id); };
        const auto gl = [&](const char* n, const auto& id) { probe(n, Kind::Global, id); };

        fn("ActorSetGhost", ids::ActorSetGhost);
        fn("VatsEffectControlDisable", ids::VatsEffectControlDisable);
        fn("SetVATShaderColor", ids::SetVATShaderColor);
        fn("WorkshopCanSelectRef", ids::WorkshopCanSelectRef);
        fn("PlaceCurrentReference", ids::PlaceCurrentReference);
        fn("WorkshopFreeBuild", ids::WorkshopFreeBuild);
        fn("ContextDataCtor", ids::ContextDataCtor);
        fn("MainRenderAEnd", ids::MainRenderAEnd);
        fn("AutoAimPickRef", ids::AutoAimPickRef);
        fn("RegisterForPlacementStatusEvent", ids::RegisterForPlacementStatusEvent);
        fn("CollectScrapRewards", ids::CollectScrapRewards);
        fn("CollectLinkedWorkshopContainers", ids::CollectLinkedWorkshopContainers);
        fn("ToggleModFavorite", ids::ToggleModFavorite);
        fn("IsWorkshopItem", RE::ID::Workshop::IsWorkshopItem);
        fn("WorkshopCanScrapRef", RE::ID::Workshop::WorkshopCanScrapRef);
        fn("ScrapReference", RE::ID::Workshop::ScrapReference);
        fn("WorkshopUpdate", RE::ID::Workshop::Update);
        fn("WorkshopDestroyCurrentItem", RE::ID::Workshop::DestroyCurrentItem);
        fn("ActorSetProcessMe", RE::ID::Actor::SetProcessMe);
        fn("ActorSetUnconscious", RE::ID::Actor::SetUnconscious);

        gl("WorkshopMenuRoot", ids::WorkshopMenuRoot);
        gl("ActiveEdit", ids::ActiveEdit);
        gl("MultiSelect", ids::MultiSelect);
        gl("CurrentWorkshopHandle", ids::CurrentWorkshopHandle);
        gl("PlacementItemTranslLS", ids::PlacementItemTranslLS);
        gl("PlacementCollisionCount", ids::PlacementCollisionCount);

        std::size_t ok = 0, bad = 0;
        for (const auto& r : results)
            (r.sane ? ok : bad)++;
        if (bad == 0 && !GlobalsHoldPlausibleData()) ++bad;

        logger::info("PrismaCrafting: [ANCHORS] runtime {}.{}.{}.{} -- checking {} engine anchors",
                     ver.major(), ver.minor(), ver.patch(), ver.build(), results.size());

        for (const auto& r : results) {
            const char* kind = r.kind == Kind::Function ? "fn " : "gbl";
            if (r.sane && r.kind == Kind::Function) {
                logger::info("PrismaCrafting: [ANCHORS]   ok   {} {:<32} relOffset {:#09x}  first byte {:#04x}",
                             kind, r.name, r.relOffset, r.first);
            } else if (r.sane) {
                logger::info("PrismaCrafting: [ANCHORS]   ok   {} {:<32} relOffset {:#09x}",
                             kind, r.name, r.relOffset);
            } else if (r.resolved) {
                logger::error("PrismaCrafting: [ANCHORS]  FAIL  {} {:<32} {} (relOffset {:#x})",
                              kind, r.name, r.note, r.relOffset);
            } else {
                logger::error("PrismaCrafting: [ANCHORS]  FAIL  {} {:<32} {}",
                              kind, r.name, r.note);
            }
        }

        if (bad == 0) {
            logger::info("PrismaCrafting: [ANCHORS] all {} resolved and landed in a plausible "
                         "section", ok);
        } else {
            logger::critical("PrismaCrafting: [ANCHORS] {} anchor or content checks FAILED out of {} anchors -- the features "
                             "using them will misbehave or crash. Report this log; the FAIL lines "
                             "above say exactly which mappings are wrong on this runtime.",
                             bad, results.size());
        }

        return bad == 0;
    }
}
