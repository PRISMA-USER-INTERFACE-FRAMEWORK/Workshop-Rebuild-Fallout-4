#include "PCH.h"

#include "AnchorCheck.h"

#include "WorkshopIDs.h"

#include <algorithm>
#include <bcrypt.h>
#include <format>

namespace crafting::anchors
{
    bool ExecutableIsKnown()
    {
        // Constraint: these are the SHA-256 of the stock executables for the runtimes the loader accepts.
        static constexpr std::string_view kKnown[] = {
            "55f57947db9e05575122fae1088f0b0247442f11e566b56036caa0ac93329c36",
            "5b2a58004f1856e51235132ab304b20c1434017067703e4282e8b0d5bc539623",
            "f4b921adce32c357b56f90b92bd9ecfef4f3572412d1a32a3d35c76930440ce8",
            "bcb8f9fe660ef4c33712b873fdc24e5ecbd6a77e629d6419f803c2c09c63eaf2",
            "6c54e6152b69cc976c3f2aa7bae2b99b86c056c596cbabce09ab8d60974bb537",
            "81694b37816c8045855905a52c5fb13583c5803121fabb5024760892014e9bc6",
            "428f9996cc4248e26c0f62f9fdd3eaf0e5eb305834b67ee5996538e593218b61",
            "fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8",
        };

        std::wstring path(32768, L'\0');
        path.resize(::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
        std::ifstream file{ std::filesystem::path(path), std::ios::binary };

        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::uint8_t digest[32]{};
        bool hashed = file.is_open() &&
                      BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
        if (hashed) {
            std::vector<char> buf(1 << 20);
            for (std::streamsize n = 0;
                 hashed && (file.read(buf.data(), static_cast<std::streamsize>(buf.size())), (n = file.gcount()) > 0);)
                hashed = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(n), 0) == 0;
            hashed = hashed && file.eof() && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
        }
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);

        if (!hashed) {
            logger::warn("PrismaCrafting: [ANCHORS] could not hash the game executable; not blocking on it");
            return true;
        }

        std::string hex;
        for (const auto b : digest) hex += std::format("{:02x}", b);
        const bool known = std::ranges::find(kKnown, hex) != std::end(kKnown);
        if (known)
            logger::info("PrismaCrafting: [ANCHORS] game executable matches a stock build ({})", hex);
        else
            logger::critical("PrismaCrafting: [ANCHORS] game executable {} is not a known stock build; "
                             "the vanilla workshop stays. Set [Compat]AllowUnknownExecutable=1 to override.", hex);
        return known;
    }

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
