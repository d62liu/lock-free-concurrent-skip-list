#pragma once

#include <vector>
#include <memory>
#include <limits>
#include <random>

struct Node {
    int key;
    std::vector<std::shared_ptr<Node>> next;

    Node(int key, int height) : key(key), next(height, nullptr) {}
};

class SkipList {
    static constexpr int MAX_LEVEL = 16;
    static constexpr float P = 0.5f;

    std::shared_ptr<Node> head;
    int current_level;

    int random_level() {
        static std::mt19937 rng(std::random_device{}());
        static std::bernoulli_distribution dist(P);
        int level = 1;
        while (level < MAX_LEVEL && dist(rng))
            level++;
        return level;
    }

public:
    SkipList()
        : head(std::make_shared<Node>(std::numeric_limits<int>::min(), MAX_LEVEL)),
          current_level(1) {}

    ~SkipList() {
        auto curr = head->next[0];
        while (curr) {
            auto next = curr->next[0];
            curr->next.clear();
            curr = next;
        }
        head->next.clear();
    }

    bool find(int key) {
        auto curr = head;
        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->next[i] && curr->next[i]->key < key)
                curr = curr->next[i];
        }
        curr = curr->next[0];
        return curr && curr->key == key;
    }

    bool insert(int key) {
        std::vector<std::shared_ptr<Node>> update(MAX_LEVEL);
        auto curr = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->next[i] && curr->next[i]->key < key)
                curr = curr->next[i];
            update[i] = curr;
        }

        if (curr->next[0] && curr->next[0]->key == key)
            return false;

        int lvl = random_level();
        if (lvl > current_level) {
            for (int i = current_level; i < lvl; i++)
                update[i] = head;
            current_level = lvl;
        }

        auto node = std::make_shared<Node>(key, lvl);
        for (int i = 0; i < lvl; i++) {
            node->next[i] = update[i]->next[i];
            update[i]->next[i] = node;
        }
        return true;
    }

    bool remove(int key) {
        std::vector<std::shared_ptr<Node>> update(MAX_LEVEL);
        auto curr = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->next[i] && curr->next[i]->key < key)
                curr = curr->next[i];
            update[i] = curr;
        }

        curr = curr->next[0];
        if (!curr || curr->key != key)
            return false;

        for (int i = 0; i < current_level; i++) {
            if (update[i]->next[i] != curr)
                break;
            update[i]->next[i] = curr->next[i];
        }

        while (current_level > 1 && !head->next[current_level - 1])
            current_level--;

        return true;
    }
};
