#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Epoch-based reclamation (EBR), Fraser-style with three epochs.
//
// In a lock-free structure a node may be unreachable from the list yet still be
// referenced by a thread that loaded its pointer a moment ago, so we cannot free
// it the instant it is unlinked. EBR's contract: each thread "pins" itself for
// the duration of one operation, publishing the global epoch it observed. A
// retired (already-unlinked) node is tagged with the epoch it died in and freed
// only once the global epoch has advanced two steps past it -- by then every
// thread that could still hold the pointer has un-pinned and re-traversed from
// the head, so none can reach it. Three limbo bags (current, one-behind,
// two-behind) are enough to hold everything that is not yet provably safe.
class EBR {
public:
    static constexpr int NUM_BAGS = 3;

    struct Retired {
        void* ptr;
        void (*deleter)(void*);
    };

    // One per (thread, EBR) pair. Only the owning thread writes its bags, so the
    // bags need no synchronization; epoch/active are read by other threads when
    // they try to advance the global epoch, hence atomic.
    struct ThreadCtl {
        std::atomic<uint64_t> epoch{0};
        std::atomic<bool> active{false};
        std::vector<Retired> bags[NUM_BAGS];
        unsigned since_advance = 0;
        ThreadCtl* next = nullptr;
    };

    EBR() : global_epoch(0), registry(nullptr) {}

    EBR(const EBR&) = delete;
    EBR& operator=(const EBR&) = delete;

    ~EBR() {
        // Destruction assumes no concurrent operations, so every bag is now safe
        // to flush regardless of epoch.
        ThreadCtl* t = registry.load(std::memory_order_relaxed);
        while (t) {
            for (int b = 0; b < NUM_BAGS; b++)
                free_bag(t->bags[b]);
            ThreadCtl* next = t->next;
            delete t;
            t = next;
        }
    }

    // RAII guard: pins the calling thread for the duration of one operation.
    class Guard {
        EBR* ebr;
        ThreadCtl* tc;
    public:
        explicit Guard(EBR* e) : ebr(e), tc(e->local()) {
            // The pin protocol must be sequentially consistent: announcing
            // `active`, reading the global epoch, and publishing our epoch all
            // share one total order with try_advance's loads. That ordering is
            // what guarantees a thread pinned at epoch v blocks any advance past
            // v+1 -- so no pinned thread is ever more than one epoch behind, which
            // is exactly what the three-bag window tolerates. Using relaxed loads
            // here breaks the guarantee: the global-epoch read can observe a stale
            // value and pin the thread two-or-more epochs back, and a reader that
            // far behind references nodes another thread is already freeing.
            tc->active.store(true, std::memory_order_seq_cst);
            uint64_t g = ebr->global_epoch.load(std::memory_order_seq_cst);
            tc->epoch.store(g, std::memory_order_seq_cst);
            // Now safely pinned, reclaim our oldest bag. The slot two epochs behind
            // (== two ahead, modulo three) holds nodes retired at or before epoch
            // g-2; since no thread can be pinned that far back, global >= g proves
            // those nodes are unreachable.
            ebr->free_bag(tc->bags[(g + 1) % NUM_BAGS]);
        }

        ~Guard() {
            tc->active.store(false, std::memory_order_seq_cst);
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        // Hand a fully-unlinked node to the reclaimer. Must be called at most once
        // per node, by whichever thread observed it leave the structure.
        void retire(void* p, void (*deleter)(void*)) {
            // Tag with the epoch *now*, not tc->epoch (this thread's own epoch as
            // of when it pinned, possibly long ago). A slow remove() -- stuck in
            // CAS retries -- can still be unlinking the node well after some other
            // thread has since pinned at a much later epoch and legitimately read
            // a valid pointer into it. Tagging with the stale pin epoch starts the
            // 2-epoch clock from that earlier point and can let the free fire
            // before that other thread ever finishes. A fresh read, taken after
            // the physical unlink this same thread just performed, is guaranteed
            // >= the pin epoch of any thread that could still hold a pointer
            // grabbed before the unlink -- global_epoch only moves forward, and
            // such a thread must have pinned before this point in real time.
            uint64_t g = ebr->global_epoch.load(std::memory_order_seq_cst);
            tc->bags[g % NUM_BAGS].push_back({p, deleter});
            if (++tc->since_advance >= kAdvanceEvery) {
                tc->since_advance = 0;
                ebr->try_advance();
            }
        }
    };

private:
    static constexpr unsigned kAdvanceEvery = 64;

    // Identifies this EBR instance for the thread-local cache in local(). Must
    // not be the instance's address: a short-lived EBR (e.g. a stack-allocated
    // skip list inside a loop) can be destroyed and a new one constructed at the
    // exact same address, which would make a stale thread_local cache believe
    // it already has a valid ThreadCtl for the new instance -- it doesn't, that
    // ThreadCtl was freed by the old instance's destructor. A monotonically
    // increasing id, never reused, closes that hole.
    inline static std::atomic<uint64_t> next_id{1};
    const uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);

    std::atomic<uint64_t> global_epoch;
    std::atomic<ThreadCtl*> registry;

    static void free_bag(std::vector<Retired>& bag) {
        for (auto& r : bag)
            r.deleter(r.ptr);
        bag.clear();
    }

    // Lazily obtain this thread's control block for this EBR instance. A thread
    // that touches a different EBR gets a fresh block; the old one stays in its
    // original registry and is freed by that EBR's destructor.
    ThreadCtl* local() {
        thread_local ThreadCtl* tc = nullptr;
        thread_local uint64_t owner_id = 0;
        if (tc == nullptr || owner_id != id) {
            tc = new ThreadCtl();
            ThreadCtl* head = registry.load(std::memory_order_relaxed);
            do {
                tc->next = head;
            } while (!registry.compare_exchange_weak(
                head, tc, std::memory_order_release, std::memory_order_relaxed));
            owner_id = id;
        }
        return tc;
    }

    // Advance the global epoch from g to g+1, but only if every pinned thread is
    // already at g. If some thread still lingers at g-1 we abstain; it will be
    // retried after the next batch of retires. Using CAS means exactly one thread
    // wins the bump and the rest simply observe the new value next time.
    void try_advance() {
        // All seq_cst, sharing the total order with the pin protocol in Guard: a
        // thread read as inactive here has not yet pinned and will observe an
        // epoch >= g when it does; a thread pinned at g-1 is seen and blocks us.
        uint64_t g = global_epoch.load(std::memory_order_seq_cst);
        for (ThreadCtl* t = registry.load(std::memory_order_acquire); t; t = t->next) {
            if (t->active.load(std::memory_order_seq_cst) &&
                t->epoch.load(std::memory_order_seq_cst) != g) {
                return;
            }
        }
        global_epoch.compare_exchange_strong(g, g + 1, std::memory_order_seq_cst);
    }
};
