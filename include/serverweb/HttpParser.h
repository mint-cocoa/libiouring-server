#pragma once

#include <serverweb/HttpRequest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Forward-declare llhttp types to avoid leaking the header.
struct llhttp__internal_s;
typedef struct llhttp__internal_s llhttp_t;
struct llhttp_settings_s;
typedef struct llhttp_settings_s llhttp_settings_t;

namespace serverweb {

// Callback invoked when a complete HTTP request has been parsed.
// Returns true to continue processing (pipelining), false to stop.
using OnRequestCallback = std::function<bool(HttpRequest&)>;

// llhttp wrapper for HTTP/1.1 request parsing.
//
// Feed() drives the state machine. On on_message_complete the parser
// pauses (HPE_PAUSED) so the caller can process the request and then
// resume for the next pipelined request.
class HttpParser {
public:
    HttpParser();
    ~HttpParser();

    HttpParser(const HttpParser&) = delete;
    HttpParser& operator=(const HttpParser&) = delete;

    void SetOnRequest(OnRequestCallback cb) { on_request_ = std::move(cb); }

    // Feed data into the parser. Returns the number of bytes consumed.
    // After each complete request, on_request_ is invoked.
    // Parsing stops on error or when on_request_ returns false.
    std::uint32_t Feed(const char* data, std::uint32_t len);

    bool HasError() const;
    void Reset();

private:
    // llhttp static callbacks -- forward to instance via parser->data
    static int OnMessageBegin(llhttp_t* parser);
    static int OnUrl(llhttp_t* parser, const char* at, std::size_t len);
    static int OnHeaderField(llhttp_t* parser, const char* at, std::size_t len);
    static int OnHeaderValue(llhttp_t* parser, const char* at, std::size_t len);
    static int OnHeaderFieldComplete(llhttp_t* parser);
    static int OnHeaderValueComplete(llhttp_t* parser);
    static int OnHeadersComplete(llhttp_t* parser);
    static int OnBody(llhttp_t* parser, const char* at, std::size_t len);
    static int OnMessageComplete(llhttp_t* parser);
    static int OnUrlComplete(llhttp_t* parser);

    void ParseUrl(const std::string& url);

    std::unique_ptr<llhttp_t> parser_;
    std::unique_ptr<llhttp_settings_t> settings_;
    HttpRequest request_;
    OnRequestCallback on_request_;
    bool error_ = false;

    // Temporary accumulation during header parsing
    std::string current_header_field_;
    std::string current_header_value_;
    std::string raw_url_;
};

} // namespace serverweb
