#pragma once

#include <string>

namespace llt 
{

    // Small synchronous WSS client built on Boost.Beast.
    //
    // This is intentionally only the external venue boundary.
    // After a raw JSON frame is received, we normalize it into MarketDataUpdate
    // and return to our own deterministic structs/queues.
    class WssClient 
    {
        public:
            WssClient(std::string host, std::string port, std::string target);

            bool connect();
            bool write_text(const std::string& msg);
            bool read_text(std::string& out);
            void close();

        private:
            std::string host_;
            std::string port_;
            std::string target_;

            struct Impl;
            Impl* impl_{nullptr};
    };

}