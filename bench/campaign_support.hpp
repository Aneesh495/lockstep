#pragma once
#include <algorithm>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "lockstep/engine/reference_model.hpp"
#include "lockstep/persistence/recovery.hpp"
#include "lockstep/protocol/codec.hpp"
namespace campaign {
using namespace lockstep;
inline MatchingEngine::Config config() {
    MatchingEngine::Config c;
    c.useReferenceBook = false;
    InstrumentConfig i;
    i.id = 1;
    i.minPrice = 100;
    i.maxPrice = 200;
    i.maxOrdersPerLevel = 4;
    i.maxPriceLevels = 101;
    c.instruments.push_back(i);
    return c;
}
struct Spec {
    MessageType type = MessageType::NewOrder;
    Order order;
    ClientId client = 1;
    OrderId oldId = 0, newId = 0;
    Price price = 150;
    Quantity quantity = 10;
    WalRecord record;
};
inline std::vector<Spec> commands(std::uint64_t seed, std::size_t count = 24) {
    std::mt19937_64 rng(seed);
    std::vector<Spec> list;
    OrderId nextId = 3;
    for (std::size_t index = 0; index < count; ++index) {
        Spec s;
        auto seq = index + 1;
        auto choice = index < 2 ? 0 : rng() % 10;
        s.client = 1 + static_cast<ClientId>(rng() % 4);
        s.oldId = 1 + rng() % (nextId - 1);
        s.newId = rng() % 2 ? s.oldId : nextId++;
        s.price = 140 + static_cast<Price>(rng() % 21);
        s.quantity = 5 + static_cast<Quantity>(rng() % 30);
        s.record.commandSeq = seq;
        s.record.timestamp = seq * 1000;
        s.record.payload.resize(64);
        std::size_t length = 0;
        if (choice < 6) {
            s.type = MessageType::NewOrder;
            auto& o = s.order;
            o.clientId = s.client;
            o.orderId = nextId++;
            o.instrumentId = 1;
            o.side = rng() % 2 ? Side::Buy : Side::Sell;
            o.tif = static_cast<TimeInForce>(rng() % 3);
            o.price = s.price;
            o.quantity = s.quantity;
            if (index < 2) {
                o.orderId = index + 1;
                o.clientId = static_cast<ClientId>(index + 1);
                o.side = index == 0 ? Side::Buy : Side::Sell;
                o.price = 150;
                o.quantity = index == 0 ? 100 : 40;
                o.tif = TimeInForce::GTC;
            }
            o.engineSeq = seq;
            o.clientSeq = seq;
            o.createTime = seq * 1000;
            NewOrderPayload p{};
            p.clientId = o.clientId;
            p.orderId = o.orderId;
            p.instrumentId = o.instrumentId;
            p.side = o.side;
            p.tif = o.tif;
            p.price = o.price;
            p.quantity = o.quantity;
            p.clientSeq = seq;
            length = Codec::encodeNewOrder(p, s.record.payload.data(), s.record.payload.size());
        } else if (choice < 8) {
            s.type = MessageType::CancelOrder;
            CancelOrderPayload p{};
            p.clientId = s.client;
            p.orderId = s.oldId;
            p.clientSeq = seq;
            length = Codec::encodeCancelOrder(p, s.record.payload.data(), s.record.payload.size());
        } else if (choice == 8) {
            s.type = MessageType::ReplaceOrder;
            ReplaceOrderPayload p{};
            p.clientId = s.client;
            p.oldOrderId = s.oldId;
            p.newOrderId = s.newId;
            p.newPrice = s.price;
            p.newQuantity = s.quantity;
            p.clientSeq = seq;
            length = Codec::encodeReplaceOrder(p, s.record.payload.data(), s.record.payload.size());
        } else {
            s.type = MessageType::MassCancel;
            MassCancelPayload p{};
            p.clientId = s.client;
            p.clientSeq = seq;
            length = Codec::encodeMassCancel(p, s.record.payload.data(), s.record.payload.size());
        }
        if (!length)
            throw std::runtime_error("Command encoding failed");
        s.record.payload.resize(length);
        s.record.recordKind = static_cast<uint8_t>(s.type);
        list.push_back(s);
    }
    return list;
}
inline IndependentReferenceModel::Result apply(IndependentReferenceModel& ref, const Spec& s) {
    if (s.type == MessageType::NewOrder)
        return ref.newOrder(s.order);
    if (s.type == MessageType::CancelOrder)
        return ref.cancelOrder(s.client, s.oldId, 1);
    if (s.type == MessageType::ReplaceOrder)
        return ref.replaceOrder(s.client, s.oldId, s.newId, 1, s.price, s.quantity);
    ref.massCancel(s.client);
    IndependentReferenceModel::Result r;
    r.success = true;
    return r;
}
inline std::string wide(Notional n) {
    if (n == 0)
        return "0";
    bool negative = n < 0;
    if (negative)
        n = -n;
    std::string text;
    while (n) {
        text.push_back(static_cast<char>('0' + n % 10));
        n /= 10;
    }
    if (negative)
        text.push_back('-');
    std::reverse(text.begin(), text.end());
    return text;
}
inline void orderRow(std::ostream& out, const Order& o) {
    out << "order " << o.instrumentId << ' ' << o.clientId << ' ' << o.orderId << ' '
        << static_cast<int>(o.side) << ' ' << static_cast<int>(o.tif) << ' ' << o.price << ' '
        << o.quantity << ' ' << o.executedQuantity << ' ' << o.clientSeq << ' ' << o.engineSeq
        << ' ' << o.createTime << ' ' << o.updateTime << '\n';
}
inline void riskRow(std::ostream& out, ClientId id, Position pos, uint32_t count, Quantity buy,
                    Quantity sell, Notional buyN, Notional sellN, const RiskLimits& l) {
    out << "risk " << id << ' ' << pos << ' ' << count << ' ' << buy << ' ' << sell << ' '
        << wide(buyN) << ' ' << wide(sellN) << ' ' << l.maxOrderQuantity << ' ' << l.maxOpenOrders
        << ' ' << l.maxOpenQuantity << ' ' << wide(l.maxOrderNotional) << ' '
        << wide(l.maxOpenNotional) << ' ' << l.maxPosition << ' ' << l.killSwitchActive << '\n';
}
inline void eventRow(std::ostream& out, const Match& m) {
    out << "trade " << m.matchId << ' ' << m.passiveClientId << ' ' << m.passiveOrderId << ' '
        << m.aggressiveClientId << ' ' << m.aggressiveOrderId << ' ' << m.price << ' ' << m.quantity
        << ' ' << m.timestamp << '\n';
}
inline std::string observed(const MatchingEngine& engine, const RiskEngine& risk,
                            const std::vector<Match>& events) {
    const auto* book = engine.getBook(1);
    if (!book)
        throw std::runtime_error("Production campaign instrument missing");
    std::ostringstream out;
    out << "state " << engine.currentCommandSeq() << ' ' << engine.currentEventSeq() << ' '
        << engine.totalMatchCount() << ' ' << engine.clock().now() << ' ' << book->nextMatchId()
        << '\n';
    book->forEachOrderInPriceTimeOrder([&](const Order& o) { orderRow(out, o); });
    std::map<ClientId, const ClientState*> clients;
    for (const auto& [id, c] : risk.clients())
        clients[id] = &c;
    for (const auto& [id, c] : clients) {
        auto it = c->positions.find(1);
        riskRow(out, id, it == c->positions.end() ? 0 : it->second, c->openOrderCount,
                c->openBuyQuantity, c->openSellQuantity, c->openBuyNotional, c->openSellNotional,
                c->limits);
    }
    for (const auto& m : events)
        eventRow(out, m);
    return out.str();
}
inline std::string expected(const std::vector<Spec>& cmds, std::size_t count, std::size_t covered) {
    IndependentReferenceModel ref(true);
    ref.addInstrument(config().instruments.front());
    std::vector<Match> events;
    uint64_t matches = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto r = apply(ref, cmds[i]);
        matches += r.matches.size();
        if (i >= covered)
            for (auto m : r.matches) {
                m.timestamp = cmds[i].record.timestamp;
                events.push_back(m);
            }
    }
    const auto* book = ref.getBook(1);
    if (!book)
        throw std::runtime_error("Reference campaign instrument missing");
    std::ostringstream out;
    out << "state " << count + 1 << ' ' << matches + 1 << ' ' << matches << ' '
        << (count ? count * 1000 : 0) << ' ' << book->nextMatchId() << '\n';
    book->forEachOrderInPriceTimeOrder([&](const Order& o) { orderRow(out, o); });
    std::map<ClientId, const RefClientExposure*> clients;
    for (const auto& [id, c] : ref.clients())
        clients[id] = &c;
    for (const auto& [id, c] : clients)
        riskRow(out, id, c->position, c->openOrderCount, c->openBuyQuantity, c->openSellQuantity,
                c->openBuyNotional, c->openSellNotional, c->limits);
    for (const auto& m : events)
        eventRow(out, m);
    return out.str();
}
}  // namespace campaign
