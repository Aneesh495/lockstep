#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

namespace lockstep {

// Integer histogram for latency measurements
// Fixed bucket boundaries for reproducibility

class Histogram {
public:
    // Bucket boundaries in nanoseconds
    static constexpr std::array<std::uint64_t, 19> BUCKET_BOUNDS = {
        100,      // 100ns
        200,      // 200ns
        500,      // 500ns
        1000,     // 1us
        2000,     // 2us
        5000,     // 5us
        10000,    // 10us
        20000,    // 20us
        50000,    // 50us
        100000,   // 100us
        200000,   // 200us
        500000,   // 500us
        1000000,  // 1ms
        2000000,  // 2ms
        5000000,  // 5ms
        10000000, // 10ms
        50000000, // 50ms
        100000000,// 100ms
        UINT64_MAX
    };
    
    explicit Histogram(std::size_t maxSamples = 10000000)
        : maxSamples_(maxSamples)
        , buckets_(BUCKET_BOUNDS.size(), 0)
        , sampleCount_(0)
        , sum_(0)
        , sumSq_(0)
        , min_(UINT64_MAX)
        , max_(0)
    {}
    
    void record(std::uint64_t value) {
        if (sampleCount_ >= maxSamples_) return;
        
        // Find bucket
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            if (value < BUCKET_BOUNDS[i]) {
                buckets_[i]++;
                break;
            }
        }
        
        rawSamples_.push_back(value);
        sum_ += value;
        sumSq_ += static_cast<double>(value) * static_cast<double>(value);
        min_ = std::min(min_, value);
        max_ = std::max(max_, value);
        sampleCount_++;
    }
    
    void reset() {
        std::fill(buckets_.begin(), buckets_.end(), 0);
        rawSamples_.clear();
        sampleCount_ = 0;
        sum_ = 0;
        sumSq_ = 0;
        min_ = UINT64_MAX;
        max_ = 0;
    }
    
    // Percentile (nearest-rank)
    std::uint64_t percentile(double p) const {
        if (rawSamples_.empty()) return 0;
        
        std::vector<std::uint64_t> sorted = rawSamples_;
        std::sort(sorted.begin(), sorted.end());
        
        std::size_t idx = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size()))) - 1;
        idx = std::min(idx, sorted.size() - 1);
        
        return sorted[idx];
    }
    
    std::uint64_t p50() const { return percentile(50); }
    std::uint64_t p95() const { return percentile(95); }
    std::uint64_t p99() const { return percentile(99); }
    std::uint64_t p999() const { return percentile(99.9); }
    
    std::uint64_t min() const { return min_; }
    std::uint64_t max() const { return max_; }
    double mean() const { return sampleCount_ > 0 ? static_cast<double>(sum_) / static_cast<double>(sampleCount_) : 0; }
    double stddev() const {
        if (sampleCount_ < 2) return 0;
        double m = mean();
        return std::sqrt(static_cast<double>(sumSq_) / static_cast<double>(sampleCount_) - m * m);
    }
    
    std::size_t count() const { return sampleCount_; }
    const std::vector<std::uint64_t>& buckets() const { return buckets_; }
    const std::vector<std::uint64_t>& samples() const { return rawSamples_; }

private:
    std::size_t maxSamples_;
    std::vector<std::uint64_t> buckets_;
    std::vector<std::uint64_t> rawSamples_;
    std::size_t sampleCount_;
    std::uint64_t sum_;
    double sumSq_;
    std::uint64_t min_;
    std::uint64_t max_;
};

} // namespace lockstep
