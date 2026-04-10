#include <serverweb/HttpParser.h>

#include <llhttp.h>
#include <spdlog/spdlog.h>

namespace serverweb {

HttpParser::HttpParser()
    : parser_(std::make_unique<llhttp_t>())
    , settings_(std::make_unique<llhttp_settings_t>()) {
    llhttp_settings_init(settings_.get());

    settings_->on_message_begin    = OnMessageBegin;
    settings_->on_url              = OnUrl;
    settings_->on_url_complete     = OnUrlComplete;
    settings_->on_header_field     = OnHeaderField;
    settings_->on_header_value     = OnHeaderValue;
    settings_->on_header_field_complete = OnHeaderFieldComplete;
    settings_->on_header_value_complete = OnHeaderValueComplete;
    settings_->on_headers_complete = OnHeadersComplete;
    settings_->on_body             = OnBody;
    settings_->on_message_complete = OnMessageComplete;

    llhttp_init(parser_.get(), HTTP_REQUEST, settings_.get());
    parser_->data = this;
}

HttpParser::~HttpParser() = default;

std::uint32_t HttpParser::Feed(const char* data, std::uint32_t len) {
    const char* current = data;
    std::uint32_t remaining = len;

    while (remaining > 0 && !error_) {
        auto err = llhttp_execute(parser_.get(), current, remaining);

        if (err == HPE_OK) {
            // All data consumed normally
            return len;
        }

        if (err == HPE_PAUSED) {
            // on_message_complete paused the parser.
            // Calculate consumed bytes up to the pause point.
            const char* error_pos = llhttp_get_error_pos(parser_.get());
            auto consumed = static_cast<std::uint32_t>(error_pos - current);
            current += consumed;
            remaining -= consumed;

            // Resume parser for next pipelined request
            llhttp_resume(parser_.get());
            continue;
        }

        // Real parse error
        error_ = true;
        const char* error_pos = llhttp_get_error_pos(parser_.get());
        return static_cast<std::uint32_t>(error_pos - data);
    }

    return static_cast<std::uint32_t>(current - data);
}

bool HttpParser::HasError() const {
    return error_;
}

void HttpParser::Reset() {
    llhttp_init(parser_.get(), HTTP_REQUEST, settings_.get());
    parser_->data = this;
    request_.Clear();
    current_header_field_.clear();
    current_header_value_.clear();
    raw_url_.clear();
    error_ = false;
}

// -- Static callbacks ----

int HttpParser::OnMessageBegin(llhttp_t* parser) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->request_.Clear();
    self->current_header_field_.clear();
    self->current_header_value_.clear();
    self->raw_url_.clear();
    return 0;
}

int HttpParser::OnUrl(llhttp_t* parser, const char* at, std::size_t len) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->raw_url_.append(at, len);
    return 0;
}

int HttpParser::OnUrlComplete(llhttp_t* parser) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->ParseUrl(self->raw_url_);
    return 0;
}

int HttpParser::OnHeaderField(llhttp_t* parser, const char* at, std::size_t len) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->current_header_field_.append(at, len);
    return 0;
}

int HttpParser::OnHeaderFieldComplete([[maybe_unused]] llhttp_t* parser) {
    return 0;
}

int HttpParser::OnHeaderValue(llhttp_t* parser, const char* at, std::size_t len) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->current_header_value_.append(at, len);
    return 0;
}

int HttpParser::OnHeaderValueComplete(llhttp_t* parser) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->request_.AddHeader(
        std::move(self->current_header_field_),
        std::move(self->current_header_value_));
    self->current_header_field_.clear();
    self->current_header_value_.clear();
    return 0;
}

int HttpParser::OnHeadersComplete(llhttp_t* parser) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->request_.method = HttpMethodFromLlhttp(llhttp_get_method(parser));
    self->request_.keep_alive = llhttp_should_keep_alive(parser) != 0;
    return 0;
}

int HttpParser::OnBody(llhttp_t* parser, const char* at, std::size_t len) {
    auto* self = static_cast<HttpParser*>(parser->data);
    self->request_.body.append(at, len);
    return 0;
}

int HttpParser::OnMessageComplete(llhttp_t* parser) {
    auto* self = static_cast<HttpParser*>(parser->data);

    if (self->on_request_) {
        bool cont = self->on_request_(self->request_);
        if (!cont) {
            self->error_ = true;
        }
    }

    // Return HPE_PAUSED directly instead of calling llhttp_pause() + return 0.
    // When a callback returns non-zero, the generated parser code sets
    // error_pos = p (current buffer position), which Feed() relies on.
    // llhttp_pause() does NOT set error_pos, so if the input buffer ends
    // exactly at the message boundary, error_pos remains NULL and Feed()
    // computes garbage pointer arithmetic.
    return HPE_PAUSED;
}

void HttpParser::ParseUrl(const std::string& url) {
    // Split URL into path and query string
    auto qpos = url.find('?');
    if (qpos != std::string::npos) {
        request_.path = url.substr(0, qpos);
        request_.query = url.substr(qpos + 1);
    } else {
        request_.path = url;
        request_.query.clear();
    }
}

} // namespace serverweb
