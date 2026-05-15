#include "llt/journal.hpp"
#include "llt/logging.hpp"
#include "llt/time.hpp"
#include "llt/metrics.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace llt 
{

    namespace 
    {

        std::uint32_t payload_size_for_type(MsgType type) 
        {
            switch (type) 
            {
                case MsgType::MarketData: return sizeof(MarketDataUpdate);
                case MsgType::Signal: return sizeof(Signal);
                case MsgType::NewOrder: return sizeof(NewOrder);
                case MsgType::Ack: return sizeof(Ack);
                case MsgType::Fill: return sizeof(Fill);
                case MsgType::Reject: return sizeof(Reject);
                case MsgType::Heartbeat: return sizeof(Heartbeat);
                case MsgType::RiskState: return sizeof(RiskState);
                default: return 0;
            }
        }

        const void* payload_ptr(const Envelope& env) 
        {
            switch (env.type) 
            {
                case MsgType::MarketData: return &env.payload.market_data;
                case MsgType::Signal: return &env.payload.signal;
                case MsgType::NewOrder: return &env.payload.new_order;
                case MsgType::Ack: return &env.payload.ack;
                case MsgType::Fill: return &env.payload.fill;
                case MsgType::Reject: return &env.payload.reject;
                case MsgType::Heartbeat: return &env.payload.heartbeat;
                case MsgType::RiskState: return &env.payload.risk_state;
                default: return nullptr;
            }
        }

        void* mutable_payload_ptr(Envelope& env, MsgType type) 
        {
            switch (type) 
            {
                case MsgType::MarketData: return &env.payload.market_data;
                case MsgType::Signal: return &env.payload.signal;
                case MsgType::NewOrder: return &env.payload.new_order;
                case MsgType::Ack: return &env.payload.ack;
                case MsgType::Fill: return &env.payload.fill;
                case MsgType::Reject: return &env.payload.reject;
                case MsgType::Heartbeat: return &env.payload.heartbeat;
                case MsgType::RiskState: return &env.payload.risk_state;
                default: return nullptr;
            }
        }

    } // namespace

    struct JournalWriter::Impl 
    {
        std::ofstream out;
    };

    std::uint32_t journal_checksum(const void* data, std::size_t len) 
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);

        std::uint32_t hash = 2166136261u;

        for (std::size_t i = 0; i < len; ++i) 
        {
            hash ^= bytes[i];
            hash *= 16777619u;
        }

        return hash;
    }

    const char* to_string(JournalRecordType type) 
    {
        switch (type) 
        {
            case JournalRecordType::OrderIntent: return "OrderIntent";
            case JournalRecordType::GatewayAck: return "GatewayAck";
            case JournalRecordType::GatewayReject: return "GatewayReject";
            case JournalRecordType::Fill: return "Fill";
            case JournalRecordType::RuntimeMarker: return "RuntimeMarker";
            default: return "Unknown";
        }
    }

    JournalWriter::JournalWriter(std::string path)
        : path_(std::move(path)),
        impl_(new Impl{}) {}

    JournalWriter::~JournalWriter() 
    {
        close();
        delete impl_;
    }

    bool JournalWriter::open() 
    {
        if (opened_) 
        {
            return true;
        }

        const auto parent = std::filesystem::path(path_).parent_path();

        if (!parent.empty()) 
        {
            std::filesystem::create_directories(parent);
        }

        // Append-only binary journal.
        //
        // We intentionally do not rewrite old records.
        // Append-only logs are easier to reason about during crash recovery.
        impl_->out.open(path_, std::ios::binary | std::ios::app);

        if (!impl_->out.is_open()) 
        {
            log(LogLevel::Error, "journal", "failed to open journal file");
            return false;
        }

        opened_ = true;
        log(LogLevel::Info, "journal", "opened journal file");
        return true;
    }

    bool JournalWriter::append(
        JournalRecordType record_type,
        const Envelope& envelope,
        Sequence sequence
    ) 
    {
        if (!opened_ && !open()) 
        {
            return false;
        }

        const std::uint32_t payload_size = payload_size_for_type(envelope.type);
        const void* payload = payload_ptr(envelope);

        if (payload == nullptr || payload_size == 0) 
        {
            log(LogLevel::Warn, "journal", "attempted to journal invalid envelope");
            return false;
        }

        JournalRecordHeader header{};
        header.magic = JOURNAL_MAGIC;
        header.version = JOURNAL_VERSION;
        header.header_size = sizeof(JournalRecordHeader);
        header.record_type = record_type;
        header.message_type = envelope.type;
        header.sequence = sequence;
        header.ts_ns = now_ns();
        header.payload_size = payload_size;
        header.checksum = journal_checksum(payload, payload_size);

        impl_->out.write(
            reinterpret_cast<const char*>(&header),
            sizeof(header)
        );

        impl_->out.write(
            reinterpret_cast<const char*>(payload),
            payload_size
        );

        // For the demo, flush every record for stronger durability.
        //
        // In a real HFT system, this is a tradeoff:
        //
        //   flush every record:
        //      stronger crash safety, higher latency
        //
        //   batch flush:
        //      lower latency, small durability window
        //
        // Production systems often use dedicated journal threads, mmap,
        // O_DIRECT, battery-backed storage, or replicated logs.
        impl_->out.flush();

        if (!impl_->out.good()) 
        {
            log(LogLevel::Error, "journal", "failed to append journal record");
            return false;
        }

        log(LogLevel::Info, "journal", "appended journal record");
        metric_inc(MetricCounter::JournalWrites);
        return true;
    }

    void JournalWriter::flush() 
    {
        if (opened_) 
        {
            impl_->out.flush();
        }
    }

    void JournalWriter::close() 
    {
        if (opened_) 
        {
            impl_->out.flush();
            impl_->out.close();
            opened_ = false;
            log(LogLevel::Info, "journal", "closed journal file");
        }
    }

    JournalReader::JournalReader(std::string path)
        : path_(std::move(path)) {}

    std::vector<JournalRecord> JournalReader::read_all() 
    {
        std::vector<JournalRecord> records;

        std::ifstream in(path_, std::ios::binary);

        if (!in.is_open()) 
        {
            log(LogLevel::Warn, "journal_reader", "journal file not found");
            return records;
        }

        while (true) 
        {
            JournalRecordHeader header{};

            in.read(reinterpret_cast<char*>(&header), sizeof(header));

            if (in.eof()) 
            {
                break;
            }

            if (!in.good()) 
            {
                log(LogLevel::Warn, "journal_reader", "incomplete journal header");
                break;
            }

            if (header.magic != JOURNAL_MAGIC ||
                header.version != JOURNAL_VERSION ||
                header.header_size != sizeof(JournalRecordHeader)) 
            {
                log(LogLevel::Warn, "journal_reader", "invalid journal header");
                break;
            }

            const std::uint32_t expected_size =
                payload_size_for_type(header.message_type);

            if (expected_size == 0 || expected_size != header.payload_size) 
            {
                log(LogLevel::Warn, "journal_reader", "invalid journal payload size");
                break;
            }

            JournalRecord record{};
            record.header = header;
            record.envelope.type = header.message_type;

            void* payload =
                mutable_payload_ptr(record.envelope, header.message_type);

            if (payload == nullptr) 
            {
                log(LogLevel::Warn, "journal_reader", "invalid payload pointer");
                break;
            }

            in.read(reinterpret_cast<char*>(payload), header.payload_size);

            if (!in.good()) 
            {
                log(LogLevel::Warn, "journal_reader", "incomplete journal payload");
                break;
            }

            const auto actual_checksum =
                journal_checksum(payload, header.payload_size);

            if (actual_checksum != header.checksum) 
            {
                log(LogLevel::Warn, "journal_reader", "journal checksum mismatch");
                break;
            }

            records.push_back(record);
        }

        return records;
    }
}