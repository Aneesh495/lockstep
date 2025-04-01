#include "lockstep/common/endian.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

namespace {

void testRoundTrip() {
    uint8_t buffer[8];
    
    lockstep::writeBeU32(buffer, 0x12345678);
    uint32_t value = lockstep::readBeU32(buffer);
    (void)value;
    assert(value == 0x12345678);
    
    lockstep::writeBeU64(buffer, 0x0123456789ABCDEFULL);
    uint64_t value64 = lockstep::readBeU64(buffer);
    (void)value64;
    assert(value64 == 0x0123456789ABCDEFULL);
    
    std::cout << "  [PASS] Endian round-trip\n";
}

void testWriteRead() {
    uint8_t buffer[8];
    
    lockstep::writeBeU32(buffer, 0x12345678);
    assert(buffer[0] == 0x12);
    assert(buffer[1] == 0x34);
    assert(buffer[2] == 0x56);
    assert(buffer[3] == 0x78);
    
    uint32_t value = lockstep::readBeU32(buffer);
    (void)value;
    assert(value == 0x12345678);
    
    std::cout << "  [PASS] Endian write/read\n";
}

void testByteReaderWriter() {
    uint8_t buffer[32];
    lockstep::ByteWriter writer(buffer, sizeof(buffer));
    
    assert(writer.writeU8(0x12));
    assert(writer.writeU16(0x3456));
    assert(writer.writeU32(0x789ABCDE));
    assert(writer.writeU64(0x0123456789ABCDEFULL));
    
    assert(writer.pos() == 15);
    
    lockstep::ByteReader reader(buffer, writer.pos());
    
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    
    (void)u8; (void)u16; (void)u32; (void)u64;
    
    assert(reader.readU8(u8) && u8 == 0x12);
    assert(reader.readU16(u16) && u16 == 0x3456);
    assert(reader.readU32(u32) && u32 == 0x789ABCDE);
    assert(reader.readU64(u64) && u64 == 0x0123456789ABCDEFULL);
    
    std::cout << "  [PASS] ByteReader/ByteWriter\n";
}

}

int runEndianTests() {
    testRoundTrip();
    testWriteRead();
    testByteReaderWriter();
    return 0;
}
