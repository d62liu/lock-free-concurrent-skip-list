#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <atomic>
#include <iomanip>
#include "skip_list_sequential.h"
#include "skip_list_locked.h"
#include "skip_list_lockfree.h"

static constexpr int DURATION_MS = 2000;
static constexpr int KEY_RANGE = 1000;
static constexpr int INITIAL_SIZE = 500;

template<typename List>
uint64_t run(List& list, int num_threads) {
    std::atomic<uint64_t> total_ops{0};
    std::atomic<bool> running{true};

    auto worker = [&]() {
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::uniform_int_distribution<int> key_dist(0, KEY_RANGE - 1);
        thread_local std::uniform_int_distribution<int> op_dist(0, 9);

        uint64_t ops = 0;
        while (running.load(std::memory_order_relaxed)) {
            int key = key_dist(rng);
            int op = op_dist(rng);
            if (op < 7)      list.find(key);
            else if (op < 9) list.insert(key);
            else             list.remove(key);
            ops++;
        }
        total_ops.fetch_add(ops, std::memory_order_relaxed);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++)
        threads.emplace_back(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(DURATION_MS));
    running.store(false, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    return total_ops.load() * 1000 / DURATION_MS;
}

template<typename List>
void prepopulate(List& list) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, KEY_RANGE - 1);
    for (int i = 0; i < INITIAL_SIZE; i++)
        list.insert(dist(rng));
}

template<typename List>
void benchmark(const std::string& name, const std::vector<int>& thread_counts) {
    std::cout << "\n" << name << "\n";
    std::cout << std::setw(10) << "threads" << std::setw(20) << "ops/sec" << "\n";
    std::cout << std::string(30, '-') << "\n";

    for (int n : thread_counts) {
        List list;
        prepopulate(list);
        uint64_t ops = run(list, n);
        std::cout << std::setw(10) << n << std::setw(20) << ops << "\n";
    }
}

int main() {
    std::vector<int> single_thread = {1};
    std::vector<int> multi_thread = {1, 2, 4, 8, 16};

    benchmark<SkipList>("Sequential (single-threaded only)", single_thread);
    std::cout << std::flush;

    benchmark<LockedSkipList>("Locked", multi_thread);
    std::cout << std::flush;

    benchmark<LockFreeSkipList>("Lock-Free", multi_thread);
    std::cout << std::flush;

    return 0;
}
