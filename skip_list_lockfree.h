#pragma once

#include <atomic>
#include <limits>
#include <random>
#include <cstdint>

struct LFNode {
    int key;
    int height;
    std::atomic<uint64_t> next[16];

    LFNode(int key, int height) : key(key), height(height) {
        for (int i = 0; i < 16; i++)
            next[i].store(0, std::memory_order_relaxed);
    }
};

static LFNode* get_ptr(uint64_t val) {
    return reinterpret_cast<LFNode*>(val & ~uint64_t(1));
}

static bool get_mark(uint64_t val) {
    return val & 1;
}

static uint64_t pack(LFNode* ptr, bool mark) {
    return reinterpret_cast<uint64_t>(ptr) | uint64_t(mark);
}

class LockFreeSkipList {
    static constexpr int MAX_LEVEL = 16;
    static constexpr float P = 0.5f;

    LFNode* head;
    std::atomic<int> current_level;

    int random_level() {
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::bernoulli_distribution dist(P);
        int level = 1;
        while (level < MAX_LEVEL && dist(rng))
            level++;
        return level;
    }

public:
    LockFreeSkipList()
        : head(new LFNode(std::numeric_limits<int>::min(), MAX_LEVEL)),
          current_level(1) {}

    ~LockFreeSkipList() {
        LFNode* curr = get_ptr(head->next[0].load());
        while (curr) {
            LFNode* next = get_ptr(curr->next[0].load());
            delete curr;
            curr = next;
        }
        delete head;
    }

    bool find(int key) {
        LFNode* preds[MAX_LEVEL];
        LFNode* succs[MAX_LEVEL];
        return find(key, preds, succs);
    }

    bool find(int key, LFNode** preds, LFNode** succs) {
    retry:
        LFNode* pred = head;
        for (int i = current_level.load(std::memory_order_relaxed) - 1; i >= 0; i--) {
            LFNode* curr = get_ptr(pred->next[i].load(std::memory_order_acquire));
            while (curr) {
                uint64_t raw = curr->next[i].load(std::memory_order_acquire);
                LFNode* succ = get_ptr(raw);
                bool marked = get_mark(raw);

                while (marked) {
                    uint64_t expected = pack(curr, false);
                    if (!pred->next[i].compare_exchange_strong(
                            expected,
                            pack(succ, false),
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        goto retry;
                    }
                    curr = succ;
                    if (!curr) break;
                    raw = curr->next[i].load(std::memory_order_acquire);
                    succ = get_ptr(raw);
                    marked = get_mark(raw);
                }

                if (!curr || curr->key >= key) break;
                pred = curr;
                curr = succ;
            }
            preds[i] = pred;
            succs[i] = curr;
        }
        return succs[0] && succs[0]->key == key;
    }

    bool insert(int key) {
        LFNode* preds[MAX_LEVEL];
        LFNode* succs[MAX_LEVEL];

        int lvl = random_level();

        while (true) {
            if (find(key, preds, succs))
                return false;

            int cl = current_level.load(std::memory_order_relaxed);
            for (int i = cl; i < lvl; i++) {
                preds[i] = head;
                succs[i] = nullptr;
            }

            LFNode* node = new LFNode(key, lvl);
            for (int i = 0; i < lvl; i++)
                node->next[i].store(pack(succs[i], false), std::memory_order_relaxed);

            uint64_t expected = pack(succs[0], false);
            if (!preds[0]->next[0].compare_exchange_strong(
                    expected,
                    pack(node, false),
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                delete node;
                continue;
            }

            for (int i = 1; i < lvl; i++) {
                while (true) {
                    expected = pack(succs[i], false);
                    if (preds[i]->next[i].compare_exchange_strong(
                            expected,
                            pack(node, false),
                            std::memory_order_acq_rel,
                            std::memory_order_acquire))
                        break;
                    find(key, preds, succs);
                }
            }

            int observed = current_level.load(std::memory_order_relaxed);
            while (observed < lvl && !current_level.compare_exchange_weak(observed, lvl, std::memory_order_relaxed));

            return true;
        }
    }

    bool remove(int key) {
        LFNode* preds[MAX_LEVEL];
        LFNode* succs[MAX_LEVEL];

        if (!find(key, preds, succs))
            return false;

        LFNode* target = succs[0];

        for (int i = target->height - 1; i >= 1; i--) {
            uint64_t raw = target->next[i].load(std::memory_order_acquire);
            while (!get_mark(raw)) {
                target->next[i].compare_exchange_strong(
                    raw,
                    pack(get_ptr(raw), true),
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
            }
        }

        uint64_t raw = target->next[0].load(std::memory_order_acquire);
        while (true) {
            bool marked = get_mark(raw);
            if (marked)
                return false;
            if (target->next[0].compare_exchange_strong(
                    raw,
                    pack(get_ptr(raw), true),
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                find(key, preds, succs);
                return true;
            }
        }
    }
};
