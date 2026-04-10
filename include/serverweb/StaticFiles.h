#pragma once

#include <serverweb/Middleware.h>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace serverweb::middleware {

struct StaticFilesOptions {
    std::filesystem::path root;
    std::string prefix = "/";
    bool enable_etag = true;
    bool enable_last_modified = true;
    std::uint32_t max_age = 3600;
    std::vector<std::string> index_files = {"index.html"};
    std::uint64_t max_file_size = 50 * 1024 * 1024;  // 50MB
};

class StaticFiles : public IMiddleware {
public:
    explicit StaticFiles(StaticFilesOptions opts);
    void Process(RequestContext& ctx, NextFn next) override;

private:
    static std::string_view MimeFromExtension(std::string_view ext);
    static std::string GenerateETag(std::uint64_t size, std::time_t mtime);

    StaticFilesOptions opts_;
    std::string resolved_root_;  // canonical root path for traversal checks
};

} // namespace serverweb::middleware
