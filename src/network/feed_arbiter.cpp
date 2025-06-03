#include "lockstep/network/feed_arbiter.hpp"
#include <algorithm>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/protocol/codec.hpp"

namespace lockstep {

FeedArbiter::FeedArbiter(std::uint32_t maxBufferSize) : maxBufferSize_(maxBufferSize) {}

void FeedArbiter::onEnvelope(const DeliveryEnvelope& env) {
    auto h = FrameHeader::parse(env.payload.data(), env.payload.size());
    if (!h || h->totalSize() != env.payload.size() ||
        h->messageType() != MessageType::SnapshotBegin || h->payloadLength() < 20) {
        ++corruptDiscarded_;
        return;
    }
    const auto* md = env.payload.data() + FrameHeader::SIZE;
    onPacket(env.channel, h->sessionId(), h->sequence(), readBeU64(md), readBeU16(md + 8),
             env.payload.data(), env.payload.size());
}

void FeedArbiter::onPacket(char channel, std::uint32_t sessionId, std::uint64_t packetSeq,
                           std::uint64_t firstEventSeq, std::uint16_t eventCount,
                           const std::uint8_t* data, std::size_t length) {
    if ((channel != 'A' && channel != 'B') || !data || firstEventSeq == 0 || eventCount == 0 ||
        eventCount > 100 || firstEventSeq > UINT64_MAX - eventCount) {
        ++corruptDiscarded_;
        return;
    }
    // Check if packet is encapsulated with a FrameHeader
    const std::uint8_t* eventData = data;
    std::size_t eventLenTotal = length;

    if (length >= FrameHeader::SIZE && data[0] == 'L' && data[1] == 'K' && data[2] == 'S' &&
        data[3] == 'T') {
        auto hdr = FrameHeader::parse(data, length);
        if (!hdr.has_value()) {
            corruptDiscarded_++;
            return;
        }

        if (hdr->totalSize() != length) {
            ++corruptDiscarded_;
            return;
        }
        // CRC verification is mandatory for wire packets.
        {
            std::vector<std::uint8_t> temp(data, data + hdr->totalSize());
            temp[32] = 0;
            temp[33] = 0;
            temp[34] = 0;
            temp[35] = 0;
            std::uint32_t computed = Crc32C::compute(temp.data(), temp.size());
            if (computed != hdr->crc32c()) {
                corruptDiscarded_++;
                return;
            }
        }

        eventData = data + FrameHeader::SIZE;
        eventLenTotal = length - FrameHeader::SIZE;
        if (hdr->messageType() == MessageType::SnapshotBegin) {
            if (eventLenTotal < 20) {
                ++corruptDiscarded_;
                return;
            }
            eventData += 20;
            eventLenTotal -= 20;
        }
    }

    std::vector<BufferedEvent> parsed;
    parsed.reserve(eventCount);
    // Parse the entire packet before exposing any event
    std::uint64_t eventSeq = firstEventSeq;
    std::size_t offset = 0;

    for (std::uint16_t i = 0; i < eventCount; ++i) {
        if (offset + 3 > eventLenTotal) {
            corruptDiscarded_++;
            return;
        }

        std::uint8_t type = eventData[offset++];
        std::uint16_t eventLen = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(eventData[offset]) << 8) | eventData[offset + 1]);
        offset += 2;

        if (offset + eventLen > eventLenTotal) {
            corruptDiscarded_++;
            return;
        }

        BufferedEvent event;
        event.eventSeq = eventSeq++;
        event.type = static_cast<MessageType>(type);
        event.payload.assign(eventData + offset, eventData + offset + eventLen);
        event.channel = channel;

        const bool framed = eventData != data;
        if (framed) {
            bool valid = false;
            switch (event.type) {
                case MessageType::BookAdd:
                    valid = eventLen == sizeof(BookAddPayload) &&
                            Codec::decodeBookAdd(event.payload.data(), eventLen).has_value();
                    break;
                case MessageType::BookChange:
                    valid = eventLen == sizeof(BookChangePayload) &&
                            Codec::decodeBookChange(event.payload.data(), eventLen).has_value();
                    break;
                case MessageType::BookDelete:
                    valid = eventLen == sizeof(BookDeletePayload) &&
                            Codec::decodeBookDelete(event.payload.data(), eventLen).has_value();
                    break;
                case MessageType::Trade:
                    valid = eventLen == sizeof(TradePayload) &&
                            Codec::decodeTrade(event.payload.data(), eventLen).has_value();
                    break;
                default:
                    break;
            }
            if (!valid) {
                ++corruptDiscarded_;
                return;
            }
        }
        parsed.push_back(std::move(event));
        offset += eventLen;
    }

    if (offset != eventLenTotal) {
        ++corruptDiscarded_;
        return;
    }
    // Validate session
    if (sessionId_ == 0) {
        sessionId_ = sessionId;
    } else if (sessionId != sessionId_) {
        // Session changed - need to resnapshot
        state_ = State::Stale;
        sessionChanges_++;
        return;
    }

    if (state_ == State::Stale) {
        return;
    }

    // Update channel state
    auto& state = (channel == 'A') ? channelA_ : channelB_;
    state.lastPacketSeq = packetSeq;
    state.receivedPackets++;

    decodedRecords_ += parsed.size();
    for (const auto& event : parsed)
        bufferEvent(event);
    // Try to emit events in order
    tryEmitEvents();
}

void FeedArbiter::bufferEvent(const BufferedEvent& event) {
    if (nextExpectedSeq_ == 0) {
        nextExpectedSeq_ = event.eventSeq;
    }

    if (event.eventSeq < nextExpectedSeq_) {
        // Duplicate: already emitted
        duplicatesDiscarded_++;
        return;
    }

    if (eventBuffer_.find(event.eventSeq) != eventBuffer_.end()) {
        // Duplicate: already in buffer from other channel
        duplicatesDiscarded_++;
        return;
    }

    if (event.eventSeq - nextExpectedSeq_ >= maxBufferSize_ ||
        readyEvents_.size() + eventBuffer_.size() >= maxBufferSize_) {
        // Buffer overflow
        state_ = State::Gap;
        bufferOverflows_++;
        return;
    }

    eventBuffer_[event.eventSeq] = event;
}

void FeedArbiter::tryEmitEvents() {
    while (true) {
        auto it = eventBuffer_.find(nextExpectedSeq_);
        if (it == eventBuffer_.end()) {
            // Check for gap
            if (!eventBuffer_.empty()) {
                std::uint64_t minSeq = eventBuffer_.begin()->first;
                if (minSeq > nextExpectedSeq_) {
                    if (state_ != State::Gap) {
                        gapsDetected_++;
                    }
                    gapStart_ = nextExpectedSeq_;
                    gapEnd_ = minSeq;
                    state_ = State::Gap;
                }
            }
            break;
        }

        // Emit event
        readyEvents_.push_back(std::move(it->second));
        eventBuffer_.erase(it);
        nextExpectedSeq_++;

        // Update state
        if (state_ == State::Gap && gapEnd_ > 0 && nextExpectedSeq_ >= gapEnd_) {
            state_ = State::Healthy;
            gapStart_ = 0;
            gapEnd_ = 0;
        }
    }
}

bool FeedArbiter::hasReadyEvents() const {
    return !readyEvents_.empty();
}

std::optional<BufferedEvent> FeedArbiter::nextEvent() {
    if (readyEvents_.empty()) {
        return std::nullopt;
    }

    BufferedEvent event = std::move(readyEvents_.front());
    readyEvents_.pop_front();

    processedEvents_++;

    return event;
}

void FeedArbiter::applySnapshot(std::uint64_t snapshotSeq, std::uint32_t newSessionId) {
    if (newSessionId != 0) {
        if (newSessionId != sessionId_) {
            readyEvents_.clear();
            eventBuffer_.clear();
        }
        sessionId_ = newSessionId;
    }

    // Ready events after the snapshot boundary must remain ordered and deduplicated.
    for (auto& e : readyEvents_)
        if (e.eventSeq > snapshotSeq)
            eventBuffer_.emplace(e.eventSeq, std::move(e));
    readyEvents_.clear();

    // Discard buffered events that are obsolete (<= snapshotSeq)
    std::erase_if(eventBuffer_,
                  [snapshotSeq](const auto& pair) { return pair.first <= snapshotSeq; });

    nextExpectedSeq_ = snapshotSeq + 1;
    gapStart_ = 0;
    gapEnd_ = 0;
    snapshotsInstalled_++;
    state_ = State::Recovering;

    // Try to emit buffered events starting from snapshotSeq + 1
    tryEmitEvents();

    if (state_ != State::Gap) {
        state_ = State::Healthy;
    }
}

void FeedArbiter::resetStats() {
    decodedRecords_ = 0;
    processedEvents_ = 0;
    duplicatesDiscarded_ = 0;
    gapsDetected_ = 0;
    snapshotsInstalled_ = 0;
    corruptDiscarded_ = 0;
    bufferOverflows_ = 0;
    sessionChanges_ = 0;
    gapStart_ = 0;
    gapEnd_ = 0;
    state_ = State::Healthy;
    eventBuffer_.clear();
    readyEvents_.clear();
    channelA_ = ChannelState{};
    channelB_ = ChannelState{};
}

}  // namespace lockstep
