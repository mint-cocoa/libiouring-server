#include "quarto.h"

#include <serverweb/ProcessExecutor.h>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace quarto {

namespace fs = std::filesystem;

QuartoRunner::QuartoRunner(const std::string& workspace,
                           const std::string& published,
                           const std::string& user_id,
                           const std::string& profile,
                           int timeout)
    : workspace_(workspace)
    , published_(published)
    , user_id_(user_id)
    , profile_(profile)
    , timeout_(timeout) {}

bool QuartoRunner::StartPreview(int port) {
    if (IsPreviewRunning()) return true;

    auto result = serverweb::ProcessExecutor::Start({
        "quarto", "preview", workspace_,
        "--no-browser",
        "--host", "0.0.0.0",
        "--port", std::to_string(port),
    }, workspace_);

    if (!result) {
        spdlog::error("[Quarto] Failed to start preview: {}", result.error());
        return false;
    }

    preview_pid_ = *result;
    spdlog::info("[Quarto] Preview started (pid={}, port={})", preview_pid_, port);
    return true;
}

void QuartoRunner::StopPreview() {
    if (!IsPreviewRunning()) return;
    serverweb::ProcessExecutor::Stop(preview_pid_);
    spdlog::info("[Quarto] Preview stopped (pid={})", preview_pid_);
    preview_pid_ = -1;
}

bool QuartoRunner::IsPreviewRunning() const {
    return preview_pid_ > 0 && serverweb::ProcessExecutor::IsRunning(preview_pid_);
}

RenderResult QuartoRunner::RenderProduction(const std::string& slug) {
    auto source = fs::path(workspace_) / (slug + ".qmd");
    if (!fs::exists(source)) {
        return {false, "", "Document '" + slug + "' not found"};
    }

    auto output_dir = fs::path(published_) / user_id_ / slug;
    fs::create_directories(output_dir);

    auto result = serverweb::ProcessExecutor::Run({
        "quarto", "render", source.string(),
        "--profile", profile_,
        "--output-dir", output_dir.string(),
    }, workspace_, timeout_);

    if (!result) {
        return {false, "", result.error()};
    }

    if (result->exit_code != 0) {
        return {false, "", result->stderr_output};
    }

    return {true, output_dir.string(), ""};
}

void QuartoRunner::RemovePublished(const std::string& slug) {
    auto dir = fs::path(published_) / user_id_ / slug;
    if (fs::exists(dir)) {
        fs::remove_all(dir);
        spdlog::info("[Quarto] Removed published: {}", dir.string());
    }
}

} // namespace quarto
