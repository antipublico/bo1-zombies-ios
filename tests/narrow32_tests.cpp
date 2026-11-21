// Adapted from ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e,
// tools/port/tests/narrow32_tests.cpp. Includes additional heap and region checks.

#include <universal/narrow32.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

static std::atomic<unsigned int> checks{};

static void Check(bool condition, int line, const char* expression)
{
    checks.fetch_add(1, std::memory_order_relaxed);
    if (!condition)
    {
        std::fprintf(stderr, "FAIL at line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(condition) Check((condition), __LINE__, #condition)

static_assert(sizeof(BO1_NARROW(char)) == 4);
static_assert(alignof(BO1_NARROW(char)) == 4);
#if BO1_NATIVE_64BIT
static_assert(std::is_trivially_copyable_v<Narrow32<char>>);
#endif

static void TestRealPointers()
{
    CHECK(BO1_NARROW_ENCODE(nullptr) == 0);
    CHECK(BO1_NARROW_DECODE(char, 0) == nullptr);
    char stack[32]{};
    auto heap = std::make_unique<char[]>(32);
    char* arrays[] = {stack, heap.get()};
    for (char* array : arrays)
    {
        for (unsigned int index = 0; index < 32; ++index)
        {
            BO1_NARROW(char) slot{};
            slot = array + index;
            CHECK(static_cast<char*>(slot) == array + index);
            *static_cast<char*>(slot) = static_cast<char>(index);
            CHECK(array[index] == static_cast<char>(index));
            const unsigned int raw = Bo1_NarrowBits(slot);
            BO1_NARROW(char) copied{};
            std::memcpy(&copied, &raw, sizeof(raw));
            CHECK(static_cast<char*>(copied) == array + index);
            Bo1_NarrowSetBits(copied, 0);
            CHECK(static_cast<char*>(copied) == nullptr);
        }
    }

    struct Node { int value; } node{42};
    BO1_NARROW(Node) nodePointer{};
    nodePointer = &node;
    CHECK(nodePointer->value == 42);
    nodePointer = nullptr;
    CHECK(static_cast<Node*>(nodePointer) == nullptr);

#if BO1_NATIVE_64BIT
    CHECK(reinterpret_cast<uintptr_t>(stack) > UINT32_MAX);
    CHECK(reinterpret_cast<uintptr_t>(heap.get()) > UINT32_MAX);
    std::printf("Verified real high addresses: stack=%p heap=%p\n", static_cast<void*>(stack), static_cast<void*>(heap.get()));
#endif
}

#if BO1_NATIVE_64BIT && BO1_NARROW_HANDLES
static void TestRegions()
{
    // These addresses are only encoded and compared, never dereferenced.
    const uintptr_t base = uintptr_t(0x12AB000000ULL);
    void* first = reinterpret_cast<void*>(base);
    void* last = reinterpret_cast<void*>(base + 0xFFFFFF);
    void* next = reinterpret_cast<void*>(base + 0x1000000);
    const unsigned int firstHandle = Bo1_NarrowEncode(first);
    const unsigned int lastHandle = Bo1_NarrowEncode(last);
    const unsigned int nextHandle = Bo1_NarrowEncode(next);
    CHECK((firstHandle >> 24) != 0);
    CHECK((firstHandle >> 24) == (lastHandle >> 24));
    CHECK(lastHandle - firstHandle == 0xFFFFFF);
    CHECK((nextHandle >> 24) != (firstHandle >> 24));
    CHECK(Bo1_NarrowDecode(firstHandle) == first);
    CHECK(Bo1_NarrowDecode(lastHandle) == last);
    CHECK(Bo1_NarrowDecode(nextHandle) == next);
    CHECK(Bo1_NarrowEncode(first) == firstHandle);
}

static void TestConcurrentPublication()
{
    constexpr unsigned int regionTotal = 32;
    std::atomic<unsigned int> published[regionTotal]{};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (unsigned int threadIndex = 0; threadIndex < 8; ++threadIndex)
    {
        threads.emplace_back([&, threadIndex] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (unsigned int iteration = 0; iteration < 20000; ++iteration)
            {
                const unsigned int region = (iteration + threadIndex) % regionTotal;
                const uintptr_t address = uintptr_t(0x4000000000ULL) + uintptr_t(region) * 0x1000000 + 0xABC;
                void* pointer = reinterpret_cast<void*>(address);
                const unsigned int handle = Bo1_NarrowEncode(pointer);
                CHECK(Bo1_NarrowDecode(handle) == pointer);
                unsigned int expected = 0;
                if (!published[region].compare_exchange_strong(expected, handle, std::memory_order_acq_rel, std::memory_order_acquire))
                    CHECK(expected == handle);
                CHECK(Bo1_NarrowDecode(published[region].load(std::memory_order_acquire)) == pointer);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads)
        thread.join();
}

static int ExhaustRegions()
{
    // Run in a fresh process. The 256th distinct region must abort explicitly.
    for (unsigned int index = 0; index < 255; ++index)
    {
        void* pointer = reinterpret_cast<void*>(uintptr_t(0x6000000000ULL) + uintptr_t(index) * 0x1000000);
        CHECK(Bo1_NarrowDecode(Bo1_NarrowEncode(pointer)) == pointer);
    }
    std::fprintf(stderr, "Registered all 255 regions; checking exhaustion.\n");
    Bo1_NarrowEncode(reinterpret_cast<void*>(uintptr_t(0x6000000000ULL) + uintptr_t(255) * 0x1000000));
    return 99;
}
#endif

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--exhaust") == 0)
    {
#if BO1_NATIVE_64BIT && BO1_NARROW_HANDLES
        return ExhaustRegions();
#else
        return 2;
#endif
    }
    if (argc != 1)
        return 2;

    TestRealPointers();
#if BO1_NATIVE_64BIT && BO1_NARROW_HANDLES
    TestRegions();
    TestConcurrentPublication();
#endif
    std::printf("Passed %u checks; native pointer %zu bytes, slot %zu bytes.\n",
        checks.load(std::memory_order_relaxed), sizeof(void*), sizeof(BO1_NARROW(char)));
    return 0;
}
