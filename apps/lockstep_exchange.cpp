#include <unistd.h>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include "lockstep/network/tcp_gateway.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/persistence/recovery.hpp"
#include "lockstep/protocol/codec.hpp"
using namespace lockstep;
namespace {
volatile std::sig_atomic_t running = 1;
void stop(int) {
    running = 0;
}
bool authenticated(const CommandMessage& cmd) {
    const auto* data = cmd.payload.data();
    auto size = cmd.payload.size();
    switch (cmd.type) {
        case MessageType::NewOrder: {
            auto p = Codec::decodeNewOrder(data, size);
            return p && size == sizeof(*p) && p->clientId == cmd.clientId;
        }
        case MessageType::CancelOrder: {
            auto p = Codec::decodeCancelOrder(data, size);
            return p && size == sizeof(*p) && p->clientId == cmd.clientId;
        }
        case MessageType::ReplaceOrder: {
            auto p = Codec::decodeReplaceOrder(data, size);
            return p && size == sizeof(*p) && p->clientId == cmd.clientId;
        }
        case MessageType::MassCancel: {
            auto p = Codec::decodeMassCancel(data, size);
            return p && size == sizeof(*p) && p->clientId == cmd.clientId;
        }
        default:
            return false;
    }
}
}  // namespace
int main(int argc, char** argv) {
    try {
        std::filesystem::path state = "artifacts/exchange";
        uint16_t tcp = 9999, a = 10001, b = 10002;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::invalid_argument("Missing option");
            std::string value = argv[++i];
            if (arg == "--state-dir")
                state = value;
            else if (arg == "--tcp-port")
                tcp = static_cast<uint16_t>(std::stoul(value));
            else if (arg == "--udp-a")
                a = static_cast<uint16_t>(std::stoul(value));
            else if (arg == "--udp-b")
                b = static_cast<uint16_t>(std::stoul(value));
            else
                throw std::invalid_argument("Unknown exchange option");
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::filesystem::create_directories(state);
        auto wal = state / "commands.wal", snap = state / "snapshot.bin";
        MatchingEngine::Config config;
        config.useReferenceBook = false;
        InstrumentConfig instr;
        instr.id = 1;
        instr.minPrice = 100;
        instr.maxPrice = 1000;
        config.instruments.push_back(instr);
        MatchingEngine engine(config);
        RiskEngine risk;
        if (std::filesystem::exists(wal)) {
            RecoveryManager recovery(std::filesystem::exists(snap) ? snap.string() : "",
                                     wal.string());
            if (!recovery.recover(engine, risk))
                throw std::runtime_error(recovery.error());
            if (recovery.finalTailIncomplete())
                std::filesystem::resize_file(wal, recovery.validWalBytes());
        }
        WalWriter writer(wal.string());
        if (!writer.isOpen())
            throw std::runtime_error("WAL open failed");
        TcpGateway gateway(tcp, 100);
        UdpPublisher publisher(a, b);
        publisher.setSessionId(static_cast<uint32_t>(::getpid()));
        if (!gateway.start() || !publisher.start())
            throw std::runtime_error("Network startup failed");
        std::cout << "Exchange port " << gateway.port() << "; durable next command "
                  << engine.currentCommandSeq() << "\n"
                  << std::flush;
        uint64_t marketSequence = 1;
        auto publish = [&](MessageType type, const auto& payload, auto encode) {
            MarketEvent e;
            e.type = type;
            e.eventSeq = marketSequence++;
            e.payload.resize(sizeof(payload));
            if (encode(payload, e.payload.data(), e.payload.size()) != e.payload.size() ||
                !publisher.eventQueue().tryPush(e))
                throw std::runtime_error("Market data encoding/queue capacity failure");
        };
        while (running) {
            CommandMessage cmd;
            while (gateway.commandQueue().tryPop(cmd)) {
                ResponseMessage response;
                response.clientId = cmd.clientId;
                if (cmd.type == MessageType::Heartbeat) {
                    response.type = MessageType::HeartbeatAck;
                    if (!gateway.responseQueue().tryPush(response))
                        throw std::runtime_error("Response queue full");
                    continue;
                }
                MatchingEngine::Result result;
                if (!authenticated(cmd)) {
                    result.reason = RejectionReason::InvalidMessage;
                } else {
                    WalRecord record;
                    record.commandSeq = engine.currentCommandSeq();
                    record.timestamp = record.commandSeq;
                    record.recordKind = static_cast<uint8_t>(cmd.type);
                    record.payload = cmd.payload;
                    std::vector<std::pair<Quantity, Quantity>> before;
                    before.reserve(901);
                    auto* book = engine.getBook(1);
                    for (Price p = 100; p <= 1000; ++p)
                        before.emplace_back(book->bidQuantity(p), book->askQuantity(p));
                    if (!writer.append(record.commandSeq, record.timestamp, record.payload.data(),
                                       record.payload.size(), record.recordKind) ||
                        !writer.sync())
                        throw std::runtime_error("Durable command write failed");
                    std::string error;
                    if (!RecoveryManager::applyRecord(engine, risk, record, error, &result))
                        throw std::runtime_error(error);
                    for (Price p = 100; p <= 1000; ++p)
                        for (auto side : {Side::Buy, Side::Sell}) {
                            Quantity old = side == Side::Buy
                                               ? before[static_cast<size_t>(p - 100)].first
                                               : before[static_cast<size_t>(p - 100)].second;
                            Quantity quantity =
                                side == Side::Buy ? book->bidQuantity(p) : book->askQuantity(p);
                            if (quantity != old) {
                                BookChangePayload payload{};
                                payload.instrumentId = 1;
                                payload.side = side;
                                payload.price = p;
                                payload.newQuantity = quantity;
                                payload.engineSeq = marketSequence;
                                publish(MessageType::BookChange, payload, Codec::encodeBookChange);
                            }
                        }
                    for (const auto& m : result.matches) {
                        TradePayload payload{};
                        payload.instrumentId = 1;
                        payload.matchId = m.matchId;
                        payload.price = m.price;
                        payload.quantity = m.quantity;
                        payload.engineSeq = marketSequence;
                        publish(MessageType::Trade, payload, Codec::encodeTrade);
                    }
                }
                response.engineSeq = result.commandSeq;
                if (result.success) {
                    OrderAcceptedPayload p{};
                    p.clientId = cmd.clientId;
                    p.engineSeq = result.commandSeq;
                    response.type = MessageType::OrderAccepted;
                    response.payload.resize(sizeof(p));
                    Codec::encodeOrderAccepted(p, response.payload.data(), response.payload.size());
                } else {
                    OrderRejectedPayload p{};
                    p.clientId = cmd.clientId;
                    p.engineSeq = result.commandSeq;
                    p.reason = result.reason;
                    response.type = MessageType::OrderRejected;
                    response.payload.resize(sizeof(p));
                    Codec::encodeOrderRejected(p, response.payload.data(), response.payload.size());
                }
                if (!gateway.responseQueue().tryPush(response))
                    throw std::runtime_error("Response queue capacity failure");
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        gateway.stop();
        publisher.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
