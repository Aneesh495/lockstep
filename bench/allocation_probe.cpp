#include <cstdlib>
#include <new>
#include "lockstep/metrics/allocation_counter.hpp"
namespace {
void* allocate(std::size_t n) {
    auto p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    lockstep::AllocationCounter::increment();
    return p;
}
void* aligned(std::size_t n, std::size_t a) {
    void* p = nullptr;
    if (posix_memalign(&p, a, n ? n : 1) != 0)
        throw std::bad_alloc();
    lockstep::AllocationCounter::increment();
    return p;
}
}  // namespace
void* operator new(std::size_t n) {
    return allocate(n);
}
void* operator new[](std::size_t n) {
    return allocate(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new(std::size_t n, std::align_val_t a) {
    return aligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return aligned(n, static_cast<std::size_t>(a));
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
