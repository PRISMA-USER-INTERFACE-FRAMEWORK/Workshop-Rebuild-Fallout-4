#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace placement
{
    enum class CmdType : std::uint8_t {
        Spawn,
        DestroyForReselect,
        Destroy,
    };

    struct Command {
        CmdType       type{ CmdType::Destroy };
        std::uint32_t recipeId{ 0 };
        std::uint32_t objectId{ 0 };
        std::uint64_t session{ 0 };
    };

    class Queue
    {
    public:

        static constexpr std::size_t kMaxPending = 8;

        using OverflowFn = void (*)(const Command& a_dropped, const Command& a_incoming);

        static void SetOverflowHandler(OverflowFn a_fn)
        {
            s_overflow.store(a_fn, std::memory_order_release);
        }

        static const char* TypeName(const Command& cmd)
        {
            switch (cmd.type) {
            case CmdType::Spawn:              return "Spawn";
            case CmdType::DestroyForReselect: return "DestroyForReselect";
            case CmdType::Destroy:            return "Destroy";
            }
            return "?";
        }

        void Post(const Command& cmd)
        {
            std::lock_guard<std::mutex> lk(m_lock);
            if (m_pending.size() >= kMaxPending) {
                auto drop = m_pending.end();
                for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                    if (it->type == CmdType::Spawn) {
                        drop = it;
                        break;
                    }
                }

                if (drop == m_pending.end() && cmd.type != CmdType::Destroy) {
                    if (auto* fn = s_overflow.load(std::memory_order_acquire)) fn(cmd, cmd);
                    return;
                }

                if (drop == m_pending.end()) {
                    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                        if (it->type == CmdType::DestroyForReselect) {
                            drop = it;
                            break;
                        }
                    }
                    if (drop == m_pending.end()) drop = m_pending.begin();
                }

                const Command dropped = *drop;
                m_pending.erase(drop);
                if (auto* fn = s_overflow.load(std::memory_order_acquire)) fn(dropped, cmd);
            }
            m_pending.push_back(cmd);
        }

        std::optional<Command> Take()
        {
            std::lock_guard<std::mutex> lk(m_lock);
            if (m_pending.empty()) return std::nullopt;
            std::optional<Command> out{ std::move(m_pending.front()) };
            m_pending.pop_front();
            return out;
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lk(m_lock);
            m_pending.clear();
        }

        std::uint64_t BeginSession()
        {
            std::lock_guard<std::mutex> lk(m_lock);
            m_currentSession = ++m_sessionCounter;
            return m_currentSession;
        }

        void InvalidateSession()
        {
            std::lock_guard<std::mutex> lk(m_lock);
            m_currentSession = 0;
        }

        bool InvalidateSessionIf(std::uint64_t expected)
        {
            std::lock_guard<std::mutex> lk(m_lock);
            if (m_currentSession != expected || expected == 0) return false;
            m_currentSession = 0;
            return true;
        }

        std::uint64_t CurrentSession() const
        {
            std::lock_guard<std::mutex> lk(m_lock);
            return m_currentSession;
        }

        bool SpawnIsLive(const Command& cmd) const
        {
            std::lock_guard<std::mutex> lk(m_lock);
            return cmd.session != 0 && cmd.session == m_currentSession;
        }

    private:
        static inline std::atomic<OverflowFn> s_overflow{ nullptr };

        mutable std::mutex      m_lock;
        std::deque<Command>     m_pending;
        std::uint64_t           m_sessionCounter{ 0 };
        std::uint64_t           m_currentSession{ 0 };
    };
}
