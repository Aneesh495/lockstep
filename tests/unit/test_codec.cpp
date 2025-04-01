#include "lockstep/protocol/codec.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

namespace {

void testNewOrderRoundTrip() {
    lockstep::NewOrderPayload original;
    original.clientId = 12345;
    original.orderId = 9876543210ULL;
    original.instrumentId = 1;
    original.side = lockstep::Side::Buy;
    original.tif = lockstep::TimeInForce::GTC;
    original.price = 100500;
    original.quantity = 1000;
    original.clientSeq = 42;
    original.clientTimestamp = 1234567890ULL;
    
    uint8_t buffer[64];
    size_t written = lockstep::Codec::encodeNewOrder(original, buffer, sizeof(buffer));
    assert(written > 0);
    (void)written;
    
    auto parsed = lockstep::Codec::decodeNewOrder(buffer, written);
    assert(parsed.has_value());
    assert(parsed->clientId == original.clientId);
    assert(parsed->orderId == original.orderId);
    assert(parsed->instrumentId == original.instrumentId);
    assert(parsed->side == original.side);
    assert(parsed->tif == original.tif);
    assert(parsed->price == original.price);
    assert(parsed->quantity == original.quantity);
    (void)parsed;
    
    std::cout << "  [PASS] Codec new order round-trip\n";
}

void testFrameHeader() {
    lockstep::FrameHeader header;
    header.setMessageType(lockstep::MessageType::NewOrder);
    header.setSessionId(123);
    header.setSequence(456);
    header.setPayloadLength(50);
    
    uint8_t buffer[40];
    size_t written = header.serialize(buffer, sizeof(buffer));
    assert(written == 40);
    
    auto parsed = lockstep::FrameHeader::parse(buffer, written);
    assert(parsed.has_value());
    assert(parsed->messageType() == lockstep::MessageType::NewOrder);
    assert(parsed->sessionId() == 123);
    assert(parsed->sequence() == 456);
    assert(parsed->payloadLength() == 50);
    (void)parsed;
    
    std::cout << "  [PASS] Codec frame header\n";
}

}

int runCodecTests() {
    testNewOrderRoundTrip();
    testFrameHeader();
    return 0;
}
