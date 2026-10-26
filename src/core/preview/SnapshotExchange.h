#pragma once

// Hands an immutable snapshot from the message thread to the audio thread without locks and without
// ever freeing on the audio thread (PRD 7.8: "shared_ptr swapped via atomic, deferred release").
//
// Protocol: a single hazard pointer. The audio thread announces the snapshot it is about to read;
// the message thread owns every published snapshot through shared_ptr and only drops one that is
// neither active nor announced. All atomics are seq_cst on purpose; the protocol relies on it.
//
//   Message thread: publish(), collect(), clear(). Never call these from the audio thread.
//   Audio thread:   acquire() at block start, release() at block end. Wait-free in practice.

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace so::preview
{

template <typename T>
class SnapshotExchange
{
public:
    SnapshotExchange() = default;
    SnapshotExchange (const SnapshotExchange&) = delete;
    SnapshotExchange& operator= (const SnapshotExchange&) = delete;

    /** Message thread. Makes `next` (may be null) the snapshot the audio thread sees from now on. */
    void publish (std::shared_ptr<const T> next)
    {
        if (next != nullptr)
            owned.push_back (next);
        active.store (next.get());
        collect();
    }

    /** Message thread. Frees retired snapshots the audio thread can no longer be reading. */
    void collect()
    {
        const T* keepActive = active.load();
        const T* keepHazard = hazard.load();
        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [&] (const auto& p) { return p.get() != keepActive && p.get() != keepHazard; }),
                     owned.end());
    }

    /** Message thread. Number of snapshots still owned (tests). */
    std::size_t ownedCount() const noexcept { return owned.size(); }

    /** Message thread. The snapshot currently published, if any. */
    std::shared_ptr<const T> current() const
    {
        const T* a = active.load();
        for (const auto& p : owned)
            if (p.get() == a)
                return p;
        return {};
    }

    /** Audio thread. Returns the active snapshot and protects it until release(). */
    const T* acquire() noexcept
    {
        const T* p = active.load();
        for (;;)
        {
            hazard.store (p);
            const T* again = active.load();
            if (again == p)
                return p;
            p = again;
        }
    }

    /** Audio thread. Ends the protection started by acquire(). */
    void release() noexcept { hazard.store (nullptr); }

private:
    std::atomic<const T*> active { nullptr };
    std::atomic<const T*> hazard { nullptr };
    std::vector<std::shared_ptr<const T>> owned;   // message thread only
};

} // namespace so::preview
