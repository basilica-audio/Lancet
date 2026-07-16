#include "AllocationGuard.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Global operator new/delete replacement for the Tests binary, scoped by
// `guardActive` below so only allocations made while a
// TestAlloc::AllocationGuard is alive are counted. Forwards to
// std::malloc/std::free - the standard-compliant way to implement a
// replacement (C++ [new.delete.single]) - so this is behaviourally
// transparent to the rest of the Tests binary outside a guard's lifetime;
// it never changes what gets allocated, only observes it.
namespace
{
    std::atomic<bool> guardActive { false };
    std::atomic<std::size_t> allocationCount { 0 };

    inline void noteAllocation() noexcept
    {
        if (guardActive.load (std::memory_order_relaxed))
            allocationCount.fetch_add (1, std::memory_order_relaxed);
    }
}

namespace TestAlloc
{
    std::size_t currentAllocationCount() noexcept
    {
        return allocationCount.load (std::memory_order_relaxed);
    }

    AllocationGuard::AllocationGuard() noexcept
    {
        allocationCount.store (0, std::memory_order_relaxed);
        guardActive.store (true, std::memory_order_relaxed);
    }

    AllocationGuard::~AllocationGuard() noexcept
    {
        guardActive.store (false, std::memory_order_relaxed);
    }
}

void* operator new (std::size_t size)
{
    noteAllocation();

    if (auto* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return std::malloc (size == 0 ? 1 : size);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new (size, std::nothrow);
}

void operator delete (void* ptr) noexcept
{
    std::free (ptr);
}

void operator delete[] (void* ptr) noexcept
{
    std::free (ptr);
}

void operator delete (void* ptr, std::size_t) noexcept
{
    std::free (ptr);
}

void operator delete[] (void* ptr, std::size_t) noexcept
{
    std::free (ptr);
}
