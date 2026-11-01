#include "RuntimeInternal.h"
#include "WeaponBench.h"

namespace WeaponBench
{
    namespace detail
    {
        bool InstallHook();
        void SetEnabled(bool a_value);
        bool EnabledValue();
        void SetDev(bool a_value);
        bool DevValue();
        bool SessionActiveValue();
        void HandleMenuEvent(const RE::MenuOpenCloseEvent& a_event);
    }

    void Initialize(bool a_flag)
    {
        detail::SetDev(LoadWeaponBenchDev());
        if (!a_flag || detail::EnabledValue()) return;
        if (!crafting::ids::WorkshopDataResolvable()) {
            logger::warn("PrismaCrafting: [Experimental]NativeWeaponBench ignored -- unsupported runtime");
            return;
        }
        if (!detail::InstallHook()) return;
        detail::SetEnabled(true);
        logger::info("PrismaCrafting: native weapon bench enabled ([Experimental]NativeWeaponBench=1)");
        if (DevEnabled()) logger::warn("PrismaCrafting: weapon bench DEV mode on -- all attachments listed as virtual owned parts; no items added");
    }

    bool Enabled()
    {
        return detail::EnabledValue();
    }

    bool DevEnabled()
    {
        return detail::DevValue();
    }

    bool SessionActive()
    {
        return detail::SessionActiveValue();
    }

    void OnExamineMenuEvent(const RE::MenuOpenCloseEvent& a_event)
    {
        detail::HandleMenuEvent(a_event);
    }
}
