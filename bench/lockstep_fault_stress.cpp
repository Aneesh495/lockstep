#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "lockstep/network/client_book.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/protocol/codec.hpp"
using namespace lockstep;
namespace {
struct State {
    std::array<Quantity, 42> quantities{};
    std::uint64_t sequence = 0, trades = 0;
};
using Counts = std::map<std::string, std::uint64_t>;
std::mutex errorMutex;
bool counterexampleSaved = false;
void writeCounts(std::ostream& out, const Counts& counts) {
    out << "{";
    bool first = true;
    for (const auto& [key, value] : counts) {
        if (!first)
            out << ",";
        first = false;
        out << "\"" << key << "\":" << value;
    }
    out << "}";
}
bool compare(const ClientBook& client, const State& expected) {
    if (client.lastEngineSeq() != expected.sequence || client.totalTrades() != expected.trades)
        return false;
    for (std::size_t i = 0; i < 21; ++i)
        if (client.bidQuantity(140 + static_cast<Price>(i)) != expected.quantities[i] ||
            client.askQuantity(140 + static_cast<Price>(i)) != expected.quantities[i + 21])
            return false;
    return true;
}
Counts shard(std::uint32_t id, std::uint64_t total, std::uint64_t seed,
             const std::filesystem::path& output) {
    Counts c;
    c["seed"] = seed;
    c["shard"] = id;
    std::mt19937_64 rng(seed);
    FaultProxy a(seed + 101), b(seed + 202);
    for (auto* proxy : {&a, &b}) {
        proxy->setLossProbability(.005);
        proxy->setDuplicateProbability(.003);
        proxy->setReorderProbability(.004);
        proxy->setCorruptionProbability(.001);
        proxy->setDelayProbability(.003, 3000);
    }
    FeedArbiter arbiter(2048);
    arbiter.setSessionId(id + 1);
    arbiter.setExpectedSeq(1);
    ClientBook client(1);
    State authority;
    std::array<State, 64> history{};
    std::vector<MarketEvent> events;
    events.reserve(10);
    std::vector<uint8_t> recentPacket;
    auto check = [&](const State& expected) {
        ++c["state_comparisons"];
        if (compare(client, expected))
            return;
        ++c["mismatches"];
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!counterexampleSaved) {
            counterexampleSaved = true;
            std::ofstream out(output / "fault_counterexample.json");
            out << "{\"seed\":" << seed << ",\"shard\":" << id
                << ",\"sequence\":" << expected.sequence
                << ",\"client_sequence\":" << client.lastEngineSeq() << ",\"packet_hex\":\"";
            constexpr char hex[] = "0123456789abcdef";
            for (auto byte : recentPacket)
                out << hex[byte >> 4] << hex[byte & 15];
            out << "\",\"expected_quantities\":[";
            for (std::size_t i = 0; i < 42; ++i) {
                if (i)
                    out << ",";
                out << expected.quantities[i];
            }
            out << "],\"actual_quantities\":[";
            for (std::size_t i = 0; i < 42; ++i) {
                if (i)
                    out << ",";
                out << (i < 21 ? client.bidQuantity(140 + static_cast<Price>(i))
                               : client.askQuantity(140 + static_cast<Price>(i - 21)));
            }
            out << "]}\n";
        }
    };
    auto apply = [&]() {
        while (auto e = arbiter.nextEvent()) {
            client.applyEvent(e->type, e->payload.data(), e->payload.size());
            ++c["events_applied"];
        }
        if (client.lastEngineSeq() > 0) {
            const auto& expected = history[((client.lastEngineSeq() - 1) / 10) % history.size()];
            if (expected.sequence != client.lastEngineSeq())
                throw std::runtime_error("Oracle history boundary missing");
            check(expected);
        }
    };
    auto install = [&]() {
        std::map<Price, Quantity, std::greater<Price>> bids;
        std::map<Price, Quantity> asks;
        for (std::size_t i = 0; i < 21; ++i) {
            if (authority.quantities[i])
                bids[140 + static_cast<Price>(i)] = authority.quantities[i];
            if (authority.quantities[i + 21])
                asks[140 + static_cast<Price>(i)] = authority.quantities[i + 21];
        }
        c["snapshot_covered_events"] += authority.sequence - client.lastEngineSeq();
        client.installSnapshot(bids, asks, authority.sequence, authority.trades);
        arbiter.applySnapshot(authority.sequence, id + 1);
        check(authority);
    };
    const auto packets = (total + 9) / 10;
    for (std::uint64_t p = 0; p < packets; ++p) {
        a.setChannelOutage('A', p % 1000 >= 100 && p % 1000 < 110);
        b.setChannelOutage('B', p % 1000 >= 600 && p % 1000 < 610);
        events.clear();
        for (std::uint64_t e = 0; e < 10 && authority.sequence < total; ++e) {
            auto type = rng() % 10;
            auto index = static_cast<std::size_t>(rng() % 42);
            auto side = index < 21 ? Side::Buy : Side::Sell;
            Price price = 140 + static_cast<Price>(index % 21);
            Quantity qty = 1 + static_cast<Quantity>(rng() % 50);
            auto seq = ++authority.sequence;
            MarketEvent event;
            event.eventSeq = seq;
            event.payload.resize(64);
            std::size_t size = 0;
            if (type < 6) {
                BookAddPayload value{};
                value.instrumentId = 1;
                value.side = side;
                value.price = price;
                value.quantity = qty;
                value.engineSeq = seq;
                event.type = MessageType::BookAdd;
                size = Codec::encodeBookAdd(value, event.payload.data(), event.payload.size());
                authority.quantities[index] += qty;
            } else if (type < 8) {
                BookChangePayload value{};
                value.instrumentId = 1;
                value.side = side;
                value.price = price;
                value.newQuantity = qty;
                value.engineSeq = seq;
                event.type = MessageType::BookChange;
                size = Codec::encodeBookChange(value, event.payload.data(), event.payload.size());
                authority.quantities[index] = qty;
            } else if (type == 8) {
                BookDeletePayload value{};
                value.instrumentId = 1;
                value.side = side;
                value.price = price;
                value.engineSeq = seq;
                event.type = MessageType::BookDelete;
                size = Codec::encodeBookDelete(value, event.payload.data(), event.payload.size());
                authority.quantities[index] = 0;
            } else {
                TradePayload value{};
                value.instrumentId = 1;
                value.matchId = seq;
                value.aggressorSide = side;
                value.price = price;
                value.quantity = qty;
                value.engineSeq = seq;
                event.type = MessageType::Trade;
                size = Codec::encodeTrade(value, event.payload.data(), event.payload.size());
                ++authority.trades;
            }
            if (!size)
                throw std::runtime_error("Encoding failed");
            event.payload.resize(size);
            events.push_back(std::move(event));
            ++c["logical_events"];
        }
        history[p % history.size()] = authority;
        auto env = UdpPublisher::encodePacket('A', id + 1, p + 1, events);
        recentPacket = env.payload;
        c["emitted_packets"] += 2;
        auto other = env;
        other.channel = 'B';
        for (const auto& d : a.submit(std::move(env), p * 1000)) {
            arbiter.onEnvelope(d);
            ++c["actual_deliveries"];
            ++c["deliveries_a"];
        }
        for (const auto& d : b.submit(std::move(other), p * 1000)) {
            arbiter.onEnvelope(d);
            ++c["actual_deliveries"];
            ++c["deliveries_b"];
        }
        apply();
        if (authority.sequence - client.lastEngineSeq() > 100)
            install();
    }
    for (const auto& d : a.drain()) {
        arbiter.onEnvelope(d);
        ++c["actual_deliveries"];
        ++c["deliveries_a"];
    }
    for (const auto& d : b.drain()) {
        arbiter.onEnvelope(d);
        ++c["actual_deliveries"];
        ++c["deliveries_b"];
    }
    apply();
    if (client.lastEngineSeq() < authority.sequence)
        install();
    check(authority);
    c["decoded_records"] = arbiter.decodedRecords();
    c["duplicates_discarded"] = arbiter.duplicatesDiscarded();
    c["corrupt_rejections"] = arbiter.corruptDiscarded();
    c["gaps_detected"] = arbiter.gapsDetected();
    c["snapshots_installed"] = arbiter.snapshotsInstalled();
    c["buffer_overflows"] = arbiter.bufferOverflows();
    c["mismatches"] += 0;
    for (auto pair : {std::pair{&a, "a"}, std::pair{&b, "b"}}) {
        auto* f = pair.first;
        std::string suffix = pair.second;
        c["dropped_" + suffix] = f->packetsDropped();
        c["duplicated_" + suffix] = f->packetsDuplicated();
        c["reordered_" + suffix] = f->packetsReordered();
        c["corrupted_" + suffix] = f->packetsCorrupted();
        c["delayed_" + suffix] = f->packetsDelayed();
        c["outages_" + suffix] = f->packetsOutage();
    }
    if (c["events_applied"] + c["snapshot_covered_events"] != total)
        ++c["mismatches"];
    std::ofstream raw(output / ("fault-shard-" + std::to_string(id) + ".json"));
    writeCounts(raw, c);
    raw << "\n";
    raw.close();
    if (!raw)
        throw std::runtime_error("Shard output failed");
    return c;
}
}  // namespace
int main(int argc, char** argv) {
    try {
        std::uint64_t events = 100000000, seed = 12345;
        std::uint32_t shards = 4;
        bool allowIncomplete = false;
        std::filesystem::path output = "artifacts/stress";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--allow-incomplete") {
                allowIncomplete = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::invalid_argument("Missing argument");
            std::string value = argv[++i];
            if (arg == "--events")
                events = std::stoull(value);
            else if (arg == "--shards")
                shards = static_cast<std::uint32_t>(std::stoul(value));
            else if (arg == "--seed")
                seed = std::stoull(value);
            else if (arg == "--output")
                output = value;
            else
                throw std::invalid_argument("Unknown fault argument");
        }
        if (!events || !shards || shards > 16 || events < shards * 10ULL)
            throw std::invalid_argument("Invalid fault workload");
        std::filesystem::create_directories(output);
        std::vector<Counts> results(shards);
        std::vector<std::thread> threads;
        std::exception_ptr failure;
        std::mutex failureMutex;
        auto start = std::chrono::steady_clock::now();
        for (std::uint32_t s = 0; s < shards; ++s)
            threads.emplace_back([&, s]() {
                try {
                    results[s] = shard(s, events / shards + (s == shards - 1 ? events % shards : 0),
                                       seed + s * 10007, output);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(failureMutex);
                    failure = std::current_exception();
                }
            });
        for (auto& t : threads)
            t.join();
        if (failure)
            std::rethrow_exception(failure);
        Counts totals;
        for (const auto& c : results)
            for (const auto& [k, v] : c)
                if (k != "seed" && k != "shard")
                    totals[k] += v;
        bool pass = totals["mismatches"] == 0 && totals["logical_events"] == events;
        for (const auto& channel : {"a", "b"})
            for (const auto& kind : {"dropped", "duplicated", "reordered", "corrupted", "delayed",
                                     "outages", "deliveries"})
                if (!totals[std::string(kind) + "_" + channel])
                    pass = false;
        bool complete = pass && events >= 100000000;
        std::ofstream out(output / "fault_stress.json");
        out << "{\"schema\":2,\"complete\":" << (complete ? "true" : "false")
            << ",\"requested_events\":" << events << ",\"shards\":" << shards
            << ",\"base_seed\":" << seed << ",\"elapsed_seconds\":"
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
            << ",\"totals\":";
        writeCounts(out, totals);
        out << "}\n";
        out.close();
        if (!out)
            throw std::runtime_error("Fault summary output failed");
        std::cout << (complete ? "PASS" : "INCOMPLETE") << ": " << events << " logical events, "
                  << totals["state_comparisons"] << " comparisons, " << totals["mismatches"]
                  << " mismatches\n";
        return complete || (allowIncomplete && totals["mismatches"] == 0) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
