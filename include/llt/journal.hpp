#pragma once

#include "llt/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llt 
{

    // ============================================================
    // Persistent OMS Journal
    // ============================================================
    //
    // The journal is the durability boundary of the OMS.
    //
    // In a real trading system, the OMS should persist order intent BEFORE
    // sending the order to an exchange gateway.
    //
    // Why?
    //
    // If the OMS sends an order and then crashes, recovery needs to know:
    //
    //   - Did we intend to send this order?
    //   - What client_order_id did we assign?
    //   - What was the risk-checked order?
    //   - Did we receive an ack/reject/fill?
    //
    // Without a journal, the system can lose its own decision history.
    //
    // Phase 3 introduces append-only binary journaling.
    // ============================================================

    constexpr std::uint32_t JOURNAL_MAGIC = 0x4A4E4C31; // "JNL1"
    constexpr std::uint16_t JOURNAL_VERSION = 1;

    enum class JournalRecordType : std::uint8_t 
    {
        Unknown = 0,

        // OMS accepted a strategy signal and created a risk-checked order.
        OrderIntent = 1,

        // Gateway acknowledged the order.
        GatewayAck = 2,

        // Gateway or OMS rejected the order.
        GatewayReject = 3,

        // Fill received from gateway/exchange.
        Fill = 4,

        // Useful marker for clean shutdown / startup boundaries.
        RuntimeMarker = 5
    };

    #pragma pack(push, 1)
    struct JournalRecordHeader 
    {
        std::uint32_t magic{JOURNAL_MAGIC};
        std::uint16_t version{JOURNAL_VERSION};
        std::uint16_t header_size{sizeof(JournalRecordHeader)};

        JournalRecordType record_type{JournalRecordType::Unknown};
        MsgType message_type{MsgType::Unknown};

        Sequence sequence{0};
        TimestampNs ts_ns{0};

        std::uint32_t payload_size{0};
        std::uint32_t checksum{0};
    };
    #pragma pack(pop)

    struct JournalRecord 
    {
        JournalRecordHeader header{};
        Envelope envelope{};
    };

    class JournalWriter 
    {
        public:
            explicit JournalWriter(std::string path);
            ~JournalWriter();

            JournalWriter(const JournalWriter&) = delete;
            JournalWriter& operator=(const JournalWriter&) = delete;

            bool open();

            bool append(
                JournalRecordType record_type,
                const Envelope& envelope,
                Sequence sequence
            );

            void flush();
            void close();

        private:
            std::string path_;
            bool opened_{false};

            // Opaque pointer would be overkill here; we keep implementation in .cpp
            // by using a unique_ptr to an ofstream.
            struct Impl;
            Impl* impl_{nullptr};
    };

    class JournalReader 
    {
        public:
            explicit JournalReader(std::string path);

            // Reads all valid journal records.
            // Stops at the first corrupt/incomplete record.
            std::vector<JournalRecord> read_all();

        private:
            std::string path_;
    };

    std::uint32_t journal_checksum(const void* data, std::size_t len);

    const char* to_string(JournalRecordType type);
}