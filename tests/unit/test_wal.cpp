#include <cstdlib>
#include "lockstep/persistence/wal.hpp"

#define TEST_ASSERT(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                                  \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)
#include <filesystem>
#include <iostream>

namespace {

void testWalWriterAndReaderLifecycle() {
    std::string path = "/tmp/lockstep_unit_wal.wal";
    std::filesystem::remove(path);

    // 1. Write records
    {
        lockstep::WalWriter writer(path);
        TEST_ASSERT(writer.isOpen());
        TEST_ASSERT(writer.position() == 0);

        lockstep::NewOrderPayload nop{};
        nop.clientId = 10;
        nop.orderId = 100;
        nop.instrumentId = 1;
        nop.price = 500;
        nop.quantity = 25;
        TEST_ASSERT(writer.appendNewOrder(1, 1000, nop));
        TEST_ASSERT(writer.position() > 0);

        lockstep::CancelOrderPayload cop{};
        cop.clientId = 10;
        cop.orderId = 100;
        TEST_ASSERT(writer.appendCancelOrder(2, 2000, cop));

        TEST_ASSERT(writer.sync());
        writer.close();
    }

    // 2. Read records
    {
        lockstep::WalReader reader(path);
        TEST_ASSERT(reader.isOpen());
        TEST_ASSERT(reader.size() > 0);

        lockstep::WalRecord r1;
        TEST_ASSERT(reader.readRecord(r1) == lockstep::WalStatus::Ok);
        TEST_ASSERT(r1.commandSeq == 1);
        TEST_ASSERT(r1.timestamp == 1000);
        TEST_ASSERT(r1.recordKind == static_cast<uint8_t>(lockstep::MessageType::NewOrder));

        lockstep::WalRecord r2;
        TEST_ASSERT(reader.readRecord(r2) == lockstep::WalStatus::Ok);
        TEST_ASSERT(r2.commandSeq == 2);
        TEST_ASSERT(r2.timestamp == 2000);
        TEST_ASSERT(r2.recordKind == static_cast<uint8_t>(lockstep::MessageType::CancelOrder));

        lockstep::WalRecord r3;
        TEST_ASSERT(reader.readRecord(r3) == lockstep::WalStatus::CleanEof);

        // Seek back to start
        TEST_ASSERT(reader.seek(0));
        TEST_ASSERT(reader.readRecord(r1) == lockstep::WalStatus::Ok);
        TEST_ASSERT(r1.commandSeq == 1);
    }

    std::filesystem::remove(path);
    std::cout << "  [PASS] WAL writer and reader lifecycle\n";
}

void testWalEmptyFile() {
    std::string path = "/tmp/lockstep_empty_wal.wal";
    std::filesystem::remove(path);

    {
        lockstep::WalWriter writer(path);
        writer.close();
    }

    lockstep::WalReader reader(path);
    TEST_ASSERT(reader.isOpen());
    lockstep::WalRecord rec;
    TEST_ASSERT(reader.readRecord(rec) == lockstep::WalStatus::CleanEof);

    std::filesystem::remove(path);
    std::cout << "  [PASS] WAL empty file read\n";
}

}  // namespace

int runWalTests() {
    testWalWriterAndReaderLifecycle();
    testWalEmptyFile();
    return 0;
}
