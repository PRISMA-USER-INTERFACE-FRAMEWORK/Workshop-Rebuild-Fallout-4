#pragma once

#include <chrono>

namespace crafting
{
    class WorkshopCatalogDeadline
    {
    public:
        using Clock = std::chrono::steady_clock;

        explicit constexpr WorkshopCatalogDeadline(Clock::duration limit) noexcept : m_limit(limit) {}

        void Arm(Clock::time_point now) noexcept
        {
            m_start = now;
            m_armed = true;
        }

        void Disarm() noexcept { m_armed = false; }

        bool Armed() const noexcept { return m_armed; }

        bool Expired(Clock::time_point now) const noexcept { return m_armed && now - m_start >= m_limit; }

        Clock::duration Elapsed(Clock::time_point now) const noexcept
        {
            return m_armed ? now - m_start : Clock::duration::zero();
        }

    private:
        Clock::duration m_limit;
        Clock::time_point m_start{};
        bool m_armed = false;
    };
}
