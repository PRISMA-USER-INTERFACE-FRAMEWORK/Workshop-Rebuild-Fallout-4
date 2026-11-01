#include "PCH.h"

#include "AnchorCheck.h"

#include "WorkshopIDs.h"

namespace crafting::anchors
{
    bool LogAll()
    {
        const auto& module = REX::FModule::GetExecutingModule();
        const auto ver = module.GetFileVersion();

        if (!ids::WorkshopDataResolvable()) {
            logger::warn("PrismaCrafting: [ANCHORS] runtime {}.{}.{}.{} has no engine anchor "
                         "mappings -- the workshop features are unavailable here and nothing was "
                         "resolved. On Next-Gen (1.10.980-1.10.984) the settlement "
                         "workshop paths stay off until NG addresses are found for its globals.",
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
        gl("PlacementItemTranslLS", RE::ID::Workshop::PlacementItemTranslLS);
        gl("PlacementCollisionCount", RE::ID::Workshop::PlacementCollisionCount);

        std::size_t ok = 0, bad = 0;
        for (const auto& r : results)
            (r.sane ? ok : bad)++;

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
            logger::critical("PrismaCrafting: [ANCHORS] {} of {} anchors FAILED -- the features "
                             "using them will misbehave or crash. Report this log; the failing "
                             "names above say exactly which mappings are wrong on this runtime.",
                             bad, results.size());
        }

        return bad == 0;
    }
}
