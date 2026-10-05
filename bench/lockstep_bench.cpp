#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/fault/fault_proxy.hpp"
#include "lockstep/metrics/allocation_counter.hpp"
#include "lockstep/metrics/system_info.hpp"
using namespace lockstep;
namespace {
using Clock = std::chrono::steady_clock;
struct Command {
    int type;
    Order order;
};
std::vector<Command> workload(std::size_t count, std::uint64_t seed) {
    std::vector<Command> commands;
    commands.reserve(count);
    Pcg32 rng(seed);
    for (std::size_t cycle = 0; commands.size() < count; ++cycle) {
        Price bid = 140 + rng.next(10), ask = 150 + rng.next(10);
        OrderId id = cycle * 2 + 1;
        for (int op = 0; op < 8 && commands.size() < count; ++op) {
            Command c{};
            c.type = op;
            auto& o = c.order;
            o.instrumentId = 1;
            o.tif = TimeInForce::GTC;
            o.quantity = 20;
            o.clientId = op == 1 || op == 4 ? 2 : 1;
            o.orderId = op == 1 || op == 4 ? id + 1 : id;
            o.side = op == 1 ? Side::Sell : Side::Buy;
            o.price = op == 1 ? ask : bid;
            if (op == 2)
                o.quantity = 10;
            if (op == 3) {
                o.clientId = 3;
                o.orderId = id + 1000000000;
                o.side = Side::Sell;
                o.tif = TimeInForce::IOC;
                o.quantity = 5;
            }
            if (op == 6) {
                o.tif = TimeInForce::FOK;
                o.price = ask;
            }
            if (op == 7) {
                o.clientId = 4;
                o.orderId = UINT64_MAX - 1;
            }
            commands.push_back(c);
        }
    }
    return commands;
}
struct Run {
    std::uint64_t duration = 0, digest = 1469598103934665603ULL, state = 0, allocations = 0,
                  matches = 0, accepted = 0, rejected = 0;
    std::uint64_t adds = 0, cancels = 0, modifies = 0, marketable = 0;
};
Run execute(MatchingEngine& engine, const std::vector<Command>& cmds,
            std::vector<std::uint64_t>* latency = nullptr) {
    Run run;
    auto allocationStart = AllocationCounter::total();
    auto start = Clock::now();
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const auto& c = cmds[i];
        const auto& o = c.order;
        auto before = latency ? Clock::now() : Clock::time_point{};
        MatchingEngine::Result r;
        if (c.type == 2) {
            r = engine.replaceOrder(o.clientId, o.orderId, o.orderId, 1, o.price, o.quantity);
            if (r.success)
                ++run.modifies;
        } else if (c.type == 4 || c.type == 5 || c.type == 7) {
            r = engine.cancelOrder(o.clientId, o.orderId, 1);
            if (r.success)
                ++run.cancels;
        } else {
            r = engine.newOrder(o);
            if (r.success) {
                if (c.type == 3)
                    ++run.marketable;
                else
                    ++run.adds;
            }
        }
        if (latency)
            (*latency)[i] = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before)
                    .count());
        r.success ? ++run.accepted : ++run.rejected;
        run.digest =
            (run.digest ^ r.commandSeq ^ static_cast<std::uint64_t>(r.reason)) * 1099511628211ULL;
        for (const auto& m : r.matches) {
            ++run.matches;
            run.digest = (run.digest ^ m.matchId ^ m.passiveOrderId ^ m.aggressiveOrderId ^
                          m.quantity ^ static_cast<std::uint64_t>(m.price)) *
                         1099511628211ULL;
        }
    }
    run.duration = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    run.allocations = AllocationCounter::total() - allocationStart;
    run.state = engine.computeStateDigest();
    return run;
}
void writeRun(std::ostream& out, const Run& r, std::size_t count) {
    out << "\"operations\":" << count << ",\"duration_ns\":" << r.duration << ",\"throughput\":"
        << (static_cast<double>(count) * 1e9 / static_cast<double>(r.duration))
        << ",\"allocations\":" << r.allocations << ",\"state_digest\":" << r.state
        << ",\"output_digest\":" << r.digest << ",\"matches\":" << r.matches
        << ",\"accepted\":" << r.accepted << ",\"rejected\":" << r.rejected
        << ",\"resting_adds\":" << r.adds << ",\"successful_cancels\":" << r.cancels
        << ",\"successful_modifies\":" << r.modifies << ",\"marketable_commands\":" << r.marketable;
}
}  // namespace
int main(int argc, char** argv) {
    std::size_t operations = 1000000, repetitions = 10, latencyOps = 100000, warmup = 100000;
    std::uint64_t seed = 12345;
    std::filesystem::path output = "artifacts/benchmarks/raw";
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::invalid_argument("Missing argument");
            std::string value = argv[++i];
            if (arg == "--output")
                output = value;
            else if (arg == "--operations")
                operations = std::stoull(value);
            else if (arg == "--repetitions")
                repetitions = std::stoull(value);
            else if (arg == "--latency-operations")
                latencyOps = std::stoull(value);
            else if (arg == "--seed")
                seed = std::stoull(value);
            else
                throw std::invalid_argument("Unknown benchmark argument");
        }
        if (!operations || !repetitions || !latencyOps)
            throw std::invalid_argument("Empty benchmark");
        const auto countBefore = AllocationCounter::total();
        auto* probe = ::operator new(17);
        ::operator delete(probe);
        const bool probeConnected = AllocationCounter::total() > countBefore;
        if (!probeConnected)
            throw std::runtime_error("Allocation hook disconnected");
        MatchingEngine::Config config;
        config.useReferenceBook = false;
        InstrumentConfig cfg;
        cfg.id = 1;
        cfg.minPrice = 100;
        cfg.maxPrice = 200;
        cfg.maxOrdersPerLevel = 10;
        cfg.maxPriceLevels = 101;
        config.instruments.push_back(cfg);
        auto warm = workload(warmup, seed);
        std::filesystem::create_directories(output);
        auto info = SystemInfo::collect();
        std::ofstream out(output / "benchmark.json");
        out.precision(17);
        out << "{\"schema\":2,\"clock\":\"steady_clock\",\"clock_steady\":"
            << (Clock::is_steady ? "true" : "false")
            << ",\"clock_period_num\":" << Clock::period::num
            << ",\"clock_period_den\":" << Clock::period::den
            << ",\"allocation_hook_connected\":true,\"compiler\":\"" << info.compiler
            << "\",\"compiler_version\":\"" << info.compilerVersion << "\",\"os\":\"" << info.os
            << "\",\"cpu_arch\":\"" << info.cpuArch << "\",\"warmup_operations\":" << warmup
            << ",\"seed\":" << seed << ",\"repetitions\":" << repetitions << ",\"runs\":[\n";
        for (std::size_t rep = 0; rep < repetitions; ++rep) {
            auto commands = workload(operations, seed + rep);
            auto latencyCommands = workload(latencyOps, seed + rep);
            std::vector<std::uint64_t> samples(latencyOps);
            MatchingEngine engine(config);
            execute(engine, warm);
            engine.reset();
            auto throughput = execute(engine, commands);
            engine.reset();
            execute(engine, warm);
            engine.reset();
            auto latency = execute(engine, latencyCommands, &samples);
            auto name = "latency-" + std::to_string(rep) + ".bin";
            std::ofstream raw(output / name, std::ios::binary);
            raw.write(reinterpret_cast<const char*>(samples.data()),
                      static_cast<std::streamsize>(samples.size() * sizeof(samples[0])));
            raw.close();
            auto sorted = samples;
            std::sort(sorted.begin(), sorted.end());
            auto percentile = [&](double q) {
                return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
            };
            if (rep)
                out << ",\n";
            out << "{\"repetition\":" << rep << ",\"seed\":" << seed + rep << ",\"throughput\":{";
            writeRun(out, throughput, operations);
            out << "},\"latency\":{";
            writeRun(out, latency, latencyOps);
            out << ",\"samples\":\"" << name << "\",\"sample_byte_order\":\""
                << (std::endian::native == std::endian::little ? "little" : "big")
                << "\",\"p50_ns\":" << percentile(.50) << ",\"p95_ns\":" << percentile(.95)
                << ",\"p99_ns\":" << percentile(.99) << ",\"p999_ns\":" << percentile(.999)
                << ",\"max_ns\":" << sorted.back() << "}}";
            std::cout << "Run " << rep << ": "
                      << (static_cast<double>(operations) * 1e9 /
                          static_cast<double>(throughput.duration))
                      << " commands/s; p99=" << percentile(.99)
                      << " ns; allocations=" << throughput.allocations + latency.allocations << "\n"
                      << std::flush;
        }
        out << "\n]}\n";
        out.close();
        if (!out)
            throw std::runtime_error("Benchmark output failed");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
