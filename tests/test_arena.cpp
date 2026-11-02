#include "c0/arena.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct DestructTracker {
    static int destruct_count;
    int id;
    std::string data;

    DestructTracker(int i, std::string d) : id(i), data(std::move(d)) {}
    ~DestructTracker() {
        ++destruct_count;
    }
};

int DestructTracker::destruct_count = 0;

int main() {
    std::cout << "Testing ArenaAllocator..." << std::endl;

    // 1. Basic 2MB allocation and verification
    {
        c0::Arena arena;
        assert(arena.chunk_count() == 1);
        assert(arena.total_allocated_bytes() == 2 * 1024 * 1024);

        int* a = arena.create<int>(42);
        assert(*a == 42);
        (void)a;

        // Alignment check
        void* p = arena.allocate(17, alignof(double));
        assert(reinterpret_cast<std::uintptr_t>(p) % alignof(double) == 0);
        (void)p;
    }
    std::cout << "  [PASS] 2MB chunk initialization and alignment" << std::endl;

    // 2. OOM growth test (exceeding 2MB triggers additional chunks)
    {
        c0::Arena arena;
        assert(arena.chunk_count() == 1);

        // Allocate 1.5 MB
        void* p1 = arena.allocate(1500 * 1024);
        assert(p1 != nullptr);
        assert(arena.chunk_count() == 1);
        (void)p1;

        // Allocate another 1.5 MB -> exceeds remaining 0.5MB in first chunk, triggers chunk 2
        void* p2 = arena.allocate(1500 * 1024);
        assert(p2 != nullptr);
        assert(arena.chunk_count() == 2);
        assert(arena.total_allocated_bytes() >= 3 * 1024 * 1024);
        (void)p2;
    }
    std::cout << "  [PASS] Out-of-memory multi-chunk growth" << std::endl;

    // 3. Non-trivial destructor execution on reset
    {
        DestructTracker::destruct_count = 0;
        {
            c0::Arena arena;
            for (int i = 0; i < 50; ++i) {
                arena.create<DestructTracker>(i, "test-string-longer-than-sso-buffer-abcdefghijklmnopqrstuvwxyz");
            }
            assert(DestructTracker::destruct_count == 0);
        }
        // Exiting scope destructed the arena, which must execute all 50 destructors
        assert(DestructTracker::destruct_count == 50);
    }
    std::cout << "  [PASS] Automatic non-trivial destructor execution on arena cleanup" << std::endl;

    std::cout << "All ArenaAllocator tests passed successfully!" << std::endl;
    return 0;
}
