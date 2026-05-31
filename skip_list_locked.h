#pragma once

#include "skip_list_sequential.h"
#include <mutex>

class LockedSkipList : public SkipList {
    std::mutex mtx;

public:
    bool find(int key) {
        std::lock_guard<std::mutex> lock(mtx);
        return SkipList::find(key);
    }

    bool insert(int key) {
        std::lock_guard<std::mutex> lock(mtx);
        return SkipList::insert(key);
    }

    bool remove(int key) {
        std::lock_guard<std::mutex> lock(mtx);
        return SkipList::remove(key);
    }
};
