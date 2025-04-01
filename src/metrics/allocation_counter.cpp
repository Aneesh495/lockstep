#include "lockstep/metrics/allocation_counter.hpp"

namespace lockstep {

std::atomic<std::uint64_t> AllocationCounter::total_{0};

} // namespace lockstep
