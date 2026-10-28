#include "memory_pool.h"
#include "test_support.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using hui::DeleteElement;
using hui::HashTable;
using hui::NewElement;

struct Lifetime {
    static std::atomic<int> constructions;
    static std::atomic<int> destructions;

    explicit Lifetime(int initial_value)
        : value(initial_value)
    {
        constructions.fetch_add(1, std::memory_order_relaxed);
    }

    ~Lifetime()
    {
        destructions.fetch_add(1, std::memory_order_relaxed);
    }

    int value;
};

std::atomic<int> Lifetime::constructions(0);
std::atomic<int> Lifetime::destructions(0);

struct StringHolder {
    StringHolder(int initial_id, const std::string& initial_text)
        : id(initial_id), text(initial_text) {}

    int id;
    std::string text;
};

struct LargeObject {
    std::uint64_t head;
    unsigned char payload[513];
    std::uint64_t tail;
};

struct alignas(16) Aligned16 {
    unsigned char bytes[16];
};

struct alignas(64) Aligned64 {
    unsigned char bytes[64];
};

struct alignas(128) Aligned128 {
    unsigned char bytes[128];
};

struct alignas(128) LargeAligned128 {
    unsigned char bytes[640];
};

struct ByteBlock {
    std::uint64_t tag;
    unsigned char payload[56];
};

struct ThrowingConstruction {
    explicit ThrowingConstruction(bool should_throw)
    {
        if (should_throw) {
            throw std::runtime_error("intentional constructor failure");
        }
    }
};

struct Range {
    std::uintptr_t begin;
    std::uintptr_t end;
    std::size_t size;
};

bool RangeLess(const Range& left, const Range& right)
{
    return left.begin < right.begin;
}

void FillBlock(ByteBlock* block, std::size_t index)
{
    block->tag = 0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(index);
    for (std::size_t i = 0; i < sizeof(block->payload); ++i) {
        block->payload[i] =
            static_cast<unsigned char>((index * 37U + i * 13U) & 0xffU);
    }
}

bool VerifyBlock(const ByteBlock* block, std::size_t index)
{
    const std::uint64_t expected_tag =
        0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(index);
    if (block->tag != expected_tag) {
        return false;
    }

    for (std::size_t i = 0; i < sizeof(block->payload); ++i) {
        const unsigned char expected =
            static_cast<unsigned char>((index * 37U + i * 13U) & 0xffU);
        if (block->payload[i] != expected) {
            return false;
        }
    }
    return true;
}

void TestConstructionDestructionAndArguments()
{
    Lifetime::constructions.store(0, std::memory_order_relaxed);
    Lifetime::destructions.store(0, std::memory_order_relaxed);

    Lifetime* value = NewElement<Lifetime>(42);
    CHECK(value != nullptr);
    CHECK_EQ(value->value, 42);
    CHECK_EQ(Lifetime::constructions.load(std::memory_order_relaxed), 1);
    CHECK_EQ(Lifetime::destructions.load(std::memory_order_relaxed), 0);

    DeleteElement(value);
    CHECK_EQ(Lifetime::destructions.load(std::memory_order_relaxed), 1);

    StringHolder* holder =
        NewElement<StringHolder>(7, std::string("perfect forwarding"));
    CHECK(holder != nullptr);
    CHECK_EQ(holder->id, 7);
    CHECK_EQ(holder->text, std::string("perfect forwarding"));
    DeleteElement(holder);
}

void TestLargeObjectFallback()
{
    LargeObject* object = NewElement<LargeObject>();
    CHECK(object != nullptr);
    object->head = 0x0123456789abcdefULL;
    object->payload[0] = 0x23;
    object->payload[sizeof(object->payload) - 1] = 0x45;
    object->tail = 0xfedcba9876543210ULL;

    CHECK_EQ(object->head, 0x0123456789abcdefULL);
    CHECK_EQ(static_cast<int>(object->payload[0]), 0x23);
    CHECK_EQ(
        static_cast<int>(object->payload[sizeof(object->payload) - 1]),
        0x45);
    CHECK_EQ(object->tail, 0xfedcba9876543210ULL);
    DeleteElement(object);
}

void TestSizeClassBoundariesDoNotOverlap()
{
    const std::size_t sizes[] = {
        1, 8, 9, 16, 17, 24, 63, 64, 127, 128,
        255, 256, 511, 512
    };

    std::vector<void*> allocations;
    std::vector<Range> ranges;
    allocations.reserve(sizeof(sizes) / sizeof(sizes[0]));
    ranges.reserve(sizeof(sizes) / sizeof(sizes[0]));

    for (std::size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        void* memory = HashTable::UseMemory(sizes[i]);
        CHECK(memory != nullptr);

        unsigned char* bytes = static_cast<unsigned char*>(memory);
        bytes[0] = static_cast<unsigned char>(0x80U + i);
        bytes[sizes[i] - 1] = static_cast<unsigned char>(0x40U + i);

        allocations.push_back(memory);
        Range range = {
            reinterpret_cast<std::uintptr_t>(bytes),
            reinterpret_cast<std::uintptr_t>(bytes) + sizes[i],
            sizes[i]
        };
        ranges.push_back(range);
    }

    std::sort(ranges.begin(), ranges.end(), RangeLess);
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        CHECK(ranges[i - 1].end <= ranges[i].begin);
    }

    for (std::size_t i = 0; i < allocations.size(); ++i) {
        HashTable::FreeMemory(sizes[i], allocations[i]);
    }
}

void TestManyLiveAllocationsDoNotAlias()
{
    const std::size_t count = 2048;
    std::vector<ByteBlock*> allocations;
    allocations.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        ByteBlock* block = NewElement<ByteBlock>();
        CHECK(block != nullptr);
        allocations.push_back(block);
        FillBlock(block, i);
    }

    for (std::size_t i = 0; i < allocations.size(); ++i) {
        CHECK(VerifyBlock(allocations[i], i));
    }

    for (std::size_t i = 0; i < allocations.size(); ++i) {
        DeleteElement(allocations[i]);
    }
}

void TestReleasedMemoryIsReused()
{
    const std::size_t count = 2048;
    const std::size_t object_size = sizeof(std::uint64_t);

    std::vector<void*> first;
    std::vector<void*> second;
    std::set<std::uintptr_t> released_addresses;
    first.reserve(count);
    second.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        void* memory = HashTable::UseMemory(object_size);
        CHECK(memory != nullptr);
        first.push_back(memory);
    }

    for (std::size_t i = 0; i < first.size(); ++i) {
        released_addresses.insert(reinterpret_cast<std::uintptr_t>(first[i]));
        HashTable::FreeMemory(object_size, first[i]);
    }

    std::size_t reused = 0;
    for (std::size_t i = 0; i < count; ++i) {
        void* memory = HashTable::UseMemory(object_size);
        CHECK(memory != nullptr);
        second.push_back(memory);
        if (released_addresses.count(
                reinterpret_cast<std::uintptr_t>(memory)) != 0) {
            ++reused;
        }
    }

    CHECK(reused > 0);

    for (std::size_t i = 0; i < second.size(); ++i) {
        HashTable::FreeMemory(object_size, second[i]);
    }
}

void TestObjectAlignment()
{
    Aligned16* aligned16 = NewElement<Aligned16>();
    Aligned64* aligned64 = NewElement<Aligned64>();
    Aligned128* aligned128 = NewElement<Aligned128>();
    LargeAligned128* large_aligned128 = NewElement<LargeAligned128>();

    CHECK(aligned16 != nullptr);
    CHECK(aligned64 != nullptr);
    CHECK(aligned128 != nullptr);
    CHECK(large_aligned128 != nullptr);
    CHECK_EQ(
        reinterpret_cast<std::uintptr_t>(aligned16) % alignof(Aligned16),
        static_cast<std::uintptr_t>(0));
    CHECK_EQ(
        reinterpret_cast<std::uintptr_t>(aligned64) % alignof(Aligned64),
        static_cast<std::uintptr_t>(0));
    CHECK_EQ(
        reinterpret_cast<std::uintptr_t>(aligned128) % alignof(Aligned128),
        static_cast<std::uintptr_t>(0));
    CHECK_EQ(
        reinterpret_cast<std::uintptr_t>(large_aligned128) %
            alignof(LargeAligned128),
        static_cast<std::uintptr_t>(0));

    DeleteElement(aligned16);
    DeleteElement(aligned64);
    DeleteElement(aligned128);
    DeleteElement(large_aligned128);
}

void TestConstructorExceptionPropagates()
{
    bool caught = false;
    try {
        NewElement<ThrowingConstruction>(true);
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "intentional constructor failure";
    }

    CHECK(caught);
}

void TestConcurrentAllocateVerifyAndRelease()
{
    const std::size_t thread_count = 8;
    const std::size_t iterations = 2000;
    std::atomic<int> ready(0);
    std::atomic<bool> start(false);
    std::atomic<std::size_t> failures(0);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0;
         thread_index < thread_count;
         ++thread_index) {
        threads.push_back(std::thread([&, thread_index]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            try {
                for (std::size_t i = 0; i < iterations; ++i) {
                    ByteBlock* block = NewElement<ByteBlock>();
                    Lifetime* value =
                        NewElement<Lifetime>(static_cast<int>(i & 0xffU));
                    if (block == nullptr || value == nullptr) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    const std::size_t unique_index =
                        thread_index * iterations + i;
                    FillBlock(block, unique_index);
                    if (!VerifyBlock(block, unique_index) ||
                        value->value != static_cast<int>(i & 0xffU)) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    DeleteElement(value);
                    DeleteElement(block);
                }
            } catch (...) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }));
    }

    while (ready.load(std::memory_order_acquire) != thread_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    CHECK_EQ(failures.load(std::memory_order_relaxed), static_cast<std::size_t>(0));
}

void TestConcurrentFreeListContention()
{
    const std::size_t thread_count = 8;
    const std::size_t iterations = 50000;
    const std::size_t object_size = sizeof(std::uint64_t);
    std::atomic<int> ready(0);
    std::atomic<bool> start(false);
    std::atomic<std::size_t> failures(0);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0;
         thread_index < thread_count;
         ++thread_index) {
        threads.push_back(std::thread([&, thread_index]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            try {
                for (std::size_t i = 0; i < iterations; ++i) {
                    std::uint64_t* memory = static_cast<std::uint64_t*>(
                        HashTable::UseMemory(object_size));
                    if (memory == nullptr) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    *memory =
                        0x5a5a000000000000ULL ^
                        static_cast<std::uint64_t>(
                            thread_index * iterations + i);
                    HashTable::FreeMemory(object_size, memory);
                }
            } catch (...) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }));
    }

    while (ready.load(std::memory_order_acquire) != thread_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    CHECK_EQ(failures.load(std::memory_order_relaxed), static_cast<std::size_t>(0));
}

void TestCrossThreadRelease()
{
    const std::size_t object_count = 4096;
    const std::size_t thread_count = 4;
    std::vector<ByteBlock*> allocations;
    std::vector<std::thread> threads;
    std::atomic<std::size_t> failures(0);
    allocations.reserve(object_count);
    threads.reserve(thread_count);

    for (std::size_t i = 0; i < object_count; ++i) {
        ByteBlock* block = NewElement<ByteBlock>();
        CHECK(block != nullptr);
        FillBlock(block, i);
        allocations.push_back(block);
    }

    for (std::size_t thread_index = 0;
         thread_index < thread_count;
         ++thread_index) {
        threads.push_back(std::thread([&, thread_index]() {
            try {
                for (std::size_t i = thread_index; i < allocations.size();
                     i += thread_count) {
                    if (!VerifyBlock(allocations[i], i)) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    DeleteElement(allocations[i]);
                }
            } catch (...) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }));
    }

    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    CHECK_EQ(failures.load(std::memory_order_relaxed), static_cast<std::size_t>(0));
}

}  // namespace

int main()
{
    HashTable::InitMemoryPools();

    const std::vector<memory_pool_test::TestCase> tests = {
        {"construction, destruction and arguments",
         TestConstructionDestructionAndArguments},
        {"large object fallback", TestLargeObjectFallback},
        {"size-class boundaries and non-overlap",
         TestSizeClassBoundariesDoNotOverlap},
        {"many live allocations do not alias",
         TestManyLiveAllocationsDoNotAlias},
        {"released memory is reused", TestReleasedMemoryIsReused},
        {"object alignment", TestObjectAlignment},
        {"constructor exception propagation",
         TestConstructorExceptionPropagates},
        {"concurrent allocate/verify/release",
         TestConcurrentAllocateVerifyAndRelease},
        {"concurrent free-list contention",
         TestConcurrentFreeListContention},
        {"cross-thread release", TestCrossThreadRelease}
    };

    return memory_pool_test::RunCases(tests);
}
