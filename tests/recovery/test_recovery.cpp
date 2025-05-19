#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                                  \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/persistence/recovery.hpp"
#include "lockstep/persistence/snapshot.hpp"
#include "lockstep/persistence/wal.hpp"
#include "lockstep/risk/risk_engine.hpp"

namespace {

lockstep::MatchingEngine::Config makeConfig() {
    lockstep::MatchingEngine::Config config;
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.tickSize = 1;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    config.instruments.push_back(instr);
    config.useReferenceBook = true;
    return config;
}

// Regression fixture: two commands create distinct book and risk changes.
// This must fail on the original counting-only replay implementation.
void testTwoCommandsRegressionFixture() {
    std::string walPath = "/tmp/lockstep_two_commands.wal";
    std::filesystem::remove(walPath);

    auto config = makeConfig();

    // Set up and write 2 commands to WAL
    {
        lockstep::WalWriter writer(walPath);
        TEST_ASSERT(writer.isOpen());

        lockstep::NewOrderPayload p1{};
        p1.clientId = 1;
        p1.orderId = 101;
        p1.instrumentId = 1;
        p1.side = lockstep::Side::Buy;
        p1.tif = lockstep::TimeInForce::GTC;
        p1.price = 150;
        p1.quantity = 100;
        p1.clientSeq = 1;
        TEST_ASSERT(writer.appendNewOrder(1, 1000000, p1));

        lockstep::NewOrderPayload p2{};
        p2.clientId = 2;
        p2.orderId = 201;
        p2.instrumentId = 1;
        p2.side = lockstep::Side::Sell;
        p2.tif = lockstep::TimeInForce::GTC;
        p2.price = 150;
        p2.quantity = 40;
        p2.clientSeq = 1;
        TEST_ASSERT(writer.appendNewOrder(2, 2000000, p2));

        writer.close();
    }

    // Recover using RecoveryManager
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    lockstep::RecoveryManager recovery("", walPath);
    bool ok = recovery.recover(engine, risk);
    TEST_ASSERT(ok);
    TEST_ASSERT(recovery.replayedRecords() == 2);
    TEST_ASSERT(recovery.recoveredCommandSeq() == 2);

    // Book verification: 1 resting order of 60 lots at price 150
    const lockstep::OrderBook* book = engine.getBook(1);
    TEST_ASSERT(book != nullptr);
    TEST_ASSERT(book->orderCount() == 1);
    TEST_ASSERT(book->bestBid() == 150);
    TEST_ASSERT(book->bidQuantity(150) == 60);
    TEST_ASSERT(book->bestAsk() == 0);

    // Risk verification:
    // Client 1 bought 40 executed, has 60 open buy
    const lockstep::ClientState* cs1 = risk.getClientState(1);
    TEST_ASSERT(cs1 != nullptr);
    TEST_ASSERT(cs1->positions.at(1) == 40);
    TEST_ASSERT(cs1->openBuyQuantity == 60);

    // Client 2 sold 40 executed, 0 open sell
    const lockstep::ClientState* cs2 = risk.getClientState(2);
    TEST_ASSERT(cs2 != nullptr);
    TEST_ASSERT(cs2->positions.at(1) == -40);
    TEST_ASSERT(cs2->openSellQuantity == 0);

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Two commands regression fixture\n";
}

void testSnapshotSaveAndRestore() {
    std::string snapPath = "/tmp/lockstep_test_snapshot.snap";
    std::filesystem::remove(snapPath);

    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;

    lockstep::RiskLimits l1;
    l1.clientId = 1;
    l1.maxOrderQuantity = 1000;
    l1.maxPosition = 5000;
    risk.setClientLimits(1, l1);

    // Submit resting buy
    lockstep::Order b1;
    b1.clientId = 1;
    b1.orderId = 10;
    b1.instrumentId = 1;
    b1.side = lockstep::Side::Buy;
    b1.price = 140;
    b1.quantity = 50;
    b1.tif = lockstep::TimeInForce::GTC;
    TEST_ASSERT(engine.newOrder(b1).success);
    risk.reserveOrder(1, 1, lockstep::Side::Buy, 140, 50);

    // Submit resting sell
    lockstep::Order s1;
    s1.clientId = 2;
    s1.orderId = 20;
    s1.instrumentId = 1;
    s1.side = lockstep::Side::Sell;
    s1.price = 160;
    s1.quantity = 30;
    s1.tif = lockstep::TimeInForce::GTC;
    TEST_ASSERT(engine.newOrder(s1).success);
    risk.reserveOrder(2, 1, lockstep::Side::Sell, 160, 30);

    uint64_t engineDigest = engine.computeStateDigest();
    uint64_t riskDigest = risk.computeDigest();

    // Write snapshot
    {
        lockstep::SnapshotWriter writer(snapPath);
        TEST_ASSERT(writer.write(engine, risk));
    }

    // Read into fresh engine and risk
    lockstep::MatchingEngine restoredEngine(config);
    lockstep::RiskEngine restoredRisk;
    {
        lockstep::SnapshotReader reader(snapPath);
        bool ok = reader.read(restoredEngine, restoredRisk);
        if (!ok) {
            std::cerr << "SnapshotReader error: " << reader.error() << "\n";
        }
        TEST_ASSERT(ok);
        TEST_ASSERT(reader.commandSeq() == engine.currentCommandSeq());
    }

    TEST_ASSERT(restoredEngine.computeStateDigest() == engineDigest);
    TEST_ASSERT(restoredRisk.computeDigest() == riskDigest);
    TEST_ASSERT(restoredEngine.totalOrderCount() == 2);
    TEST_ASSERT(restoredEngine.getBook(1)->bestBid() == 140);
    TEST_ASSERT(restoredEngine.getBook(1)->bestAsk() == 160);

    std::filesystem::remove(snapPath);
    std::cout << "  [PASS] Snapshot save and restore\n";
}

void testSnapshotPlusSubsequentWalReplay() {
    std::string snapPath = "/tmp/lockstep_snap_plus_wal.snap";
    std::string walPath = "/tmp/lockstep_snap_plus_wal.wal";
    std::filesystem::remove(snapPath);
    std::filesystem::remove(walPath);

    auto config = makeConfig();
    lockstep::MatchingEngine liveEngine(config);
    lockstep::RiskEngine liveRisk;
    lockstep::RiskLimits l1, l2;
    l1.clientId = 1;
    l2.clientId = 2;
    liveRisk.setClientLimits(1, l1);
    liveRisk.setClientLimits(2, l2);

    // Command 1: Buy 100 @ 150
    lockstep::Order o1;
    o1.clientId = 1;
    o1.orderId = 1;
    o1.instrumentId = 1;
    o1.side = lockstep::Side::Buy;
    o1.price = 150;
    o1.quantity = 100;
    o1.tif = lockstep::TimeInForce::GTC;
    TEST_ASSERT(liveEngine.newOrder(o1).success);
    liveRisk.reserveOrder(1, 1, lockstep::Side::Buy, 150, 100);

    // Save snapshot at seq 1
    {
        lockstep::SnapshotWriter snapWriter(snapPath);
        TEST_ASSERT(snapWriter.write(liveEngine, liveRisk));
    }

    // Now write WAL with Command 1 (covered by snapshot) and Command 2 (subsequent)
    {
        lockstep::WalWriter walWriter(walPath);
        lockstep::NewOrderPayload p1{};
        p1.clientId = 1;
        p1.orderId = 1;
        p1.instrumentId = 1;
        p1.side = lockstep::Side::Buy;
        p1.price = 150;
        p1.quantity = 100;
        TEST_ASSERT(walWriter.appendNewOrder(1, 1000, p1));

        // Command 2: Sell 40 @ 150
        lockstep::NewOrderPayload p2{};
        p2.clientId = 2;
        p2.orderId = 2;
        p2.instrumentId = 1;
        p2.side = lockstep::Side::Sell;
        p2.price = 150;
        p2.quantity = 40;
        TEST_ASSERT(walWriter.appendNewOrder(2, 2000, p2));
        walWriter.close();
    }

    // Apply Command 2 on live engine too so we have expected reference state
    lockstep::Order o2;
    o2.clientId = 2;
    o2.orderId = 2;
    o2.instrumentId = 1;
    o2.side = lockstep::Side::Sell;
    o2.price = 150;
    o2.quantity = 40;
    o2.tif = lockstep::TimeInForce::GTC;
    auto r2 = liveEngine.newOrder(o2);
    TEST_ASSERT(r2.success);
    liveRisk.updatePosition(2, 1, lockstep::Side::Sell, 40);
    liveRisk.updatePosition(1, 1, lockstep::Side::Buy, 40);

    // Recover using both snapshot and WAL
    lockstep::MatchingEngine recoveredEngine(config);
    lockstep::RiskEngine recoveredRisk;
    lockstep::RecoveryManager recovery(snapPath, walPath);
    TEST_ASSERT(recovery.recover(recoveredEngine, recoveredRisk));
    TEST_ASSERT(recovery.replayedRecords() ==
                1);  // Command 1 skipped (covered by snapshot), Command 2 replayed

    TEST_ASSERT(recoveredEngine.computeStateDigest() == liveEngine.computeStateDigest());
    TEST_ASSERT(recoveredRisk.computeDigest() == liveRisk.computeDigest());

    std::filesystem::remove(snapPath);
    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Snapshot plus subsequent WAL replay\n";
}

void testReplayTwiceDeterministic() {
    std::string walPath = "/tmp/lockstep_replay_twice.wal";
    std::filesystem::remove(walPath);

    auto config = makeConfig();

    {
        lockstep::WalWriter writer(walPath);
        for (int i = 0; i < 20; ++i) {
            lockstep::NewOrderPayload p{};
            p.clientId = static_cast<lockstep::ClientId>((i % 2) + 1);
            p.orderId = static_cast<lockstep::OrderId>(i + 1);
            p.instrumentId = 1;
            p.side = (i % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
            p.price = 150;
            p.quantity = 10;
            p.clientSeq = static_cast<uint64_t>(i + 1);
            TEST_ASSERT(writer.appendNewOrder(static_cast<uint64_t>(i + 1),
                                              1000 + static_cast<uint64_t>(i), p));
        }
        writer.close();
    }

    uint64_t digest1 = 0, digest2 = 0;

    {
        lockstep::MatchingEngine e1(config);
        lockstep::RiskEngine r1;
        lockstep::RecoveryManager rec1("", walPath);
        TEST_ASSERT(rec1.recover(e1, r1));
        digest1 = e1.computeStateDigest();
    }

    {
        lockstep::MatchingEngine e2(config);
        lockstep::RiskEngine r2;
        lockstep::RecoveryManager rec2("", walPath);
        TEST_ASSERT(rec2.recover(e2, r2));
        digest2 = e2.computeStateDigest();
    }

    TEST_ASSERT(digest1 == digest2);

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Replay twice deterministic\n";
}

void testRecoveryFollowedByNewTrading() {
    std::string walPath = "/tmp/lockstep_recovery_trading.wal";
    std::filesystem::remove(walPath);

    auto config = makeConfig();

    // Persist a resting buy order in WAL
    {
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 10;
        p.instrumentId = 1;
        p.side = lockstep::Side::Buy;
        p.price = 155;
        p.quantity = 50;
        p.clientSeq = 1;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        writer.close();
    }

    // Recover engine
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    lockstep::RecoveryManager rec("", walPath);
    TEST_ASSERT(rec.recover(engine, risk));
    TEST_ASSERT(engine.totalOrderCount() == 1);

    // New trading: submit aggressive sell crossing spread
    lockstep::Order o;
    o.clientId = 2;
    o.orderId = 20;
    o.instrumentId = 1;
    o.side = lockstep::Side::Sell;
    o.price = 155;
    o.quantity = 50;
    o.tif = lockstep::TimeInForce::GTC;
    auto res = engine.newOrder(o);

    TEST_ASSERT(res.success);
    TEST_ASSERT(res.matches.size() == 1);
    TEST_ASSERT(res.matches[0].quantity == 50);
    TEST_ASSERT(res.matches[0].price == 155);
    TEST_ASSERT(res.matches[0].passiveOrderId == 10);
    TEST_ASSERT(res.matches[0].aggressiveOrderId == 20);
    TEST_ASSERT(engine.totalOrderCount() == 0);

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Recovery followed by new trading\n";
}

void testWalCorruptChecksumRejected() {
    std::string walPath = "/tmp/lockstep_corrupt_crc.wal";
    std::filesystem::remove(walPath);

    {
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 1;
        p.instrumentId = 1;
        p.price = 150;
        p.quantity = 10;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        writer.close();
    }

    // Corrupt one byte of payload in the file
    {
        std::fstream f(walPath, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(26);
        char b = 0x7F;
        f.write(&b, 1);
    }

    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    lockstep::RecoveryManager rec("", walPath);
    TEST_ASSERT(!rec.recover(engine, risk));
    TEST_ASSERT(!rec.error().empty());

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Corrupt checksum rejected\n";
}

void testWalInvalidMagicRejected() {
    std::string walPath = "/tmp/lockstep_bad_magic.wal";
    std::filesystem::remove(walPath);

    {
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 1;
        p.instrumentId = 1;
        p.price = 150;
        p.quantity = 10;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        writer.close();
    }

    // Corrupt magic
    {
        std::fstream f(walPath, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(0);
        uint32_t badMagic = 0xDEADBEEF;
        f.write(reinterpret_cast<char*>(&badMagic), 4);
    }

    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    lockstep::RecoveryManager rec("", walPath);
    TEST_ASSERT(!rec.recover(engine, risk));
    TEST_ASSERT(rec.error().find("magic") != std::string::npos ||
                rec.error().find("WAL") != std::string::npos);

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Invalid magic rejected\n";
}

void testWalSequenceAnomaliesRejected() {
    auto config = makeConfig();

    // Duplicate sequence
    {
        std::string walPath = "/tmp/lockstep_dup_seq.wal";
        std::filesystem::remove(walPath);
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 1;
        p.instrumentId = 1;
        p.price = 150;
        p.quantity = 10;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        p.orderId = 2;
        TEST_ASSERT(writer.appendNewOrder(1, 1001, p));  // Duplicate seq 1!
        writer.close();

        lockstep::MatchingEngine engine(config);
        lockstep::RiskEngine risk;
        lockstep::RecoveryManager rec("", walPath);
        TEST_ASSERT(!rec.recover(engine, risk));
        TEST_ASSERT(rec.error().find("Duplicate") != std::string::npos);
        std::filesystem::remove(walPath);
    }

    // Missing sequence gap
    {
        std::string walPath = "/tmp/lockstep_gap_seq.wal";
        std::filesystem::remove(walPath);
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 1;
        p.instrumentId = 1;
        p.price = 150;
        p.quantity = 10;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        p.orderId = 2;
        TEST_ASSERT(writer.appendNewOrder(3, 1002, p));  // Gap: jumped from 1 to 3!
        writer.close();

        lockstep::MatchingEngine engine(config);
        lockstep::RiskEngine risk;
        lockstep::RecoveryManager rec("", walPath);
        TEST_ASSERT(!rec.recover(engine, risk));
        TEST_ASSERT(rec.error().find("gap") != std::string::npos ||
                    rec.error().find("Missing") != std::string::npos);
        std::filesystem::remove(walPath);
    }

    std::cout << "  [PASS] WAL sequence anomalies rejected\n";
}

void testTornFinalTailRecoverable() {
    std::string walPath = "/tmp/lockstep_torn_tail.wal";
    std::filesystem::remove(walPath);

    {
        lockstep::WalWriter writer(walPath);
        lockstep::NewOrderPayload p{};
        p.clientId = 1;
        p.orderId = 1;
        p.instrumentId = 1;
        p.price = 150;
        p.quantity = 10;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, p));
        p.orderId = 2;
        TEST_ASSERT(writer.appendNewOrder(2, 1001, p));
        writer.close();
    }

    // Append a partial record header (e.g. 10 bytes) to simulate crash during write
    {
        std::ofstream f(walPath, std::ios::binary | std::ios::app);
        char partial[10] = "PARTHDR";
        f.write(partial, sizeof(partial));
    }

    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    lockstep::RecoveryManager rec("", walPath);
    TEST_ASSERT(rec.recover(engine, risk));
    TEST_ASSERT(rec.replayedRecords() == 2);
    TEST_ASSERT(rec.recoveredCommandSeq() == 2);
    TEST_ASSERT(rec.finalTailIncomplete());

    std::filesystem::remove(walPath);
    std::cout << "  [PASS] Torn final tail recoverable\n";
}

void testSnapshotCorruptRejected() {
    std::string snapPath = "/tmp/lockstep_corrupt_snap.snap";
    std::filesystem::remove(snapPath);

    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;

    lockstep::SnapshotWriter writer(snapPath);
    TEST_ASSERT(writer.write(engine, risk));

    // Corrupt one byte of snapshot
    {
        std::fstream f(snapPath, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(10);
        char b = static_cast<char>(0xAA);
        f.write(&b, 1);
    }

    lockstep::MatchingEngine e2(config);
    lockstep::RiskEngine r2;
    lockstep::SnapshotReader reader(snapPath);
    TEST_ASSERT(!reader.read(e2, r2));
    TEST_ASSERT(reader.error().find("CRC mismatch") != std::string::npos);

    std::filesystem::remove(snapPath);
    std::cout << "  [PASS] Corrupt snapshot rejected\n";
}

}  // namespace

int runRecoveryTests() {
    testTwoCommandsRegressionFixture();
    testSnapshotSaveAndRestore();
    testSnapshotPlusSubsequentWalReplay();
    testReplayTwiceDeterministic();
    testRecoveryFollowedByNewTrading();
    testWalCorruptChecksumRejected();
    testWalInvalidMagicRejected();
    testWalSequenceAnomaliesRejected();
    testTornFinalTailRecoverable();
    testSnapshotCorruptRejected();
    return 0;
}
