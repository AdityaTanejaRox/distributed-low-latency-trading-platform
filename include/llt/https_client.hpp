#pragma once

#include <map>
#include <string>

namespace llt
{
    struct HttpResponse
    {
        int status_code{0};
        std::string body{};
    };

    class HttpsClient
    {
    public:
        HttpsClient(std::string host, std::string port);

        HttpResponse post_form(
            const std::string& target,
            const std::string& body,
            const std::map<std::string, std::string>& headers
        );

        HttpResponse get(
            const std::string& target,
            const std::map<std::string, std::string>& headers = {}
        );

    private:
        std::string host_;
        std::string port_;
    };
}