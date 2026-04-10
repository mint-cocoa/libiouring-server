#include <serverweb/StaticFiles.h>
#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/Router.h>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace serverweb::middleware {

StaticFiles::StaticFiles(StaticFilesOptions opts)
    : opts_(std::move(opts)) {
    std::error_code ec;
    auto canonical = std::filesystem::canonical(opts_.root, ec);
    if (!ec) {
        resolved_root_ = canonical.string();
    } else {
        resolved_root_ = opts_.root.string();
        spdlog::warn("StaticFiles: root path '{}' cannot be resolved: {}",
                     opts_.root.string(), ec.message());
    }

    // Ensure prefix ends with /
    if (!opts_.prefix.empty() && opts_.prefix.back() != '/') {
        opts_.prefix += '/';
    }
    // Ensure prefix starts with /
    if (opts_.prefix.empty() || opts_.prefix.front() != '/') {
        opts_.prefix = "/" + opts_.prefix;
    }
}

void StaticFiles::Process(RequestContext& ctx, NextFn next) {
    // Only handle GET and HEAD
    if (ctx.request.method != HttpMethod::kGet &&
        ctx.request.method != HttpMethod::kHead) {
        next();
        return;
    }

    std::string_view path = ctx.request.path;

    // Check if path matches our prefix
    if (path.substr(0, opts_.prefix.size()) != opts_.prefix) {
        next();
        return;
    }

    // Extract relative path after prefix
    std::string rel_path(path.substr(opts_.prefix.size()));

    // Build filesystem path
    std::filesystem::path fs_path = std::filesystem::path(resolved_root_) / rel_path;

    // Check if it's a directory -- try index files
    std::error_code ec;
    if (std::filesystem::is_directory(fs_path, ec)) {
        bool found = false;
        for (auto& idx : opts_.index_files) {
            auto idx_path = fs_path / idx;
            if (std::filesystem::is_regular_file(idx_path, ec)) {
                fs_path = idx_path;
                found = true;
                break;
            }
        }
        if (!found) {
            next();
            return;
        }
    }

    // Resolve canonical path for traversal check
    auto resolved = std::filesystem::canonical(fs_path, ec);
    if (ec) {
        // File doesn't exist
        next();
        return;
    }

    // Path traversal defense
    std::string resolved_str = resolved.string();
    if (resolved_str.substr(0, resolved_root_.size()) != resolved_root_) {
        spdlog::warn("StaticFiles: path traversal attempt blocked: {}", ctx.request.path);
        ctx.response
            .Status(HttpStatus::kForbidden)
            .ContentType("text/plain")
            .Body("Forbidden")
            .Send();
        return;
    }

    // Must be a regular file
    if (!std::filesystem::is_regular_file(resolved, ec)) {
        next();
        return;
    }

    // Get file stats
    struct stat st{};
    if (::stat(resolved_str.c_str(), &st) != 0) {
        next();
        return;
    }

    auto file_size = static_cast<std::uint64_t>(st.st_size);
    auto mtime = st.st_mtime;

    // Size check
    if (file_size > opts_.max_file_size) {
        ctx.response
            .Status(HttpStatus::kPayloadTooLarge)
            .ContentType("text/plain")
            .Body("File Too Large")
            .Send();
        return;
    }

    // ETag support
    if (opts_.enable_etag) {
        std::string etag = GenerateETag(file_size, mtime);
        auto if_none_match = ctx.request.GetHeader("If-None-Match");
        if (!if_none_match.empty() && if_none_match == etag) {
            ctx.response
                .Status(HttpStatus::kNotModified)
                .Header("ETag", etag)
                .Send();
            return;
        }
        ctx.response.Header("ETag", etag);
    }

    // Last-Modified support
    if (opts_.enable_last_modified) {
        char buf[64];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&mtime));
        ctx.response.Header("Last-Modified", buf);
    }

    // Cache-Control
    ctx.response.Header("Cache-Control",
        "public, max-age=" + std::to_string(opts_.max_age));

    // MIME type
    auto ext = resolved.extension().string();
    auto mime = MimeFromExtension(ext);
    ctx.response.ContentType(mime);

    // Read file (for HEAD requests, we still set Content-Length via body size but skip reading)
    if (ctx.request.method == HttpMethod::kHead) {
        // Set Content-Length from file size without reading the body
        ctx.response
            .Status(HttpStatus::kOk)
            .Header("Content-Length", std::to_string(file_size))
            .Send();
        return;
    }

    std::ifstream file(resolved_str, std::ios::binary);
    if (!file) {
        next();
        return;
    }

    std::string body;
    body.resize(file_size);
    file.read(body.data(), static_cast<std::streamsize>(file_size));

    ctx.response
        .Status(HttpStatus::kOk)
        .Body(std::move(body))
        .Send();
}

std::string_view StaticFiles::MimeFromExtension(std::string_view ext) {
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")   return "text/css; charset=utf-8";
    if (ext == ".js")    return "application/javascript; charset=utf-8";
    if (ext == ".json")  return "application/json; charset=utf-8";
    if (ext == ".xml")   return "application/xml; charset=utf-8";
    if (ext == ".txt")   return "text/plain; charset=utf-8";
    if (ext == ".csv")   return "text/csv; charset=utf-8";

    if (ext == ".png")   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")   return "image/gif";
    if (ext == ".svg")   return "image/svg+xml";
    if (ext == ".ico")   return "image/x-icon";
    if (ext == ".webp")  return "image/webp";
    if (ext == ".avif")  return "image/avif";
    if (ext == ".bmp")   return "image/bmp";

    if (ext == ".woff")  return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf")   return "font/ttf";
    if (ext == ".otf")   return "font/otf";
    if (ext == ".eot")   return "application/vnd.ms-fontobject";

    if (ext == ".wasm")  return "application/wasm";
    if (ext == ".pdf")   return "application/pdf";
    if (ext == ".zip")   return "application/zip";
    if (ext == ".gz")    return "application/gzip";

    if (ext == ".mp4")   return "video/mp4";
    if (ext == ".webm")  return "video/webm";
    if (ext == ".mp3")   return "audio/mpeg";
    if (ext == ".ogg")   return "audio/ogg";
    if (ext == ".wav")   return "audio/wav";

    if (ext == ".map")   return "application/json";

    return "application/octet-stream";
}

std::string StaticFiles::GenerateETag(std::uint64_t size, std::time_t mtime) {
    // Simple ETag: W/"size-mtime" (weak validator)
    return "W/\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"";
}

} // namespace serverweb::middleware
