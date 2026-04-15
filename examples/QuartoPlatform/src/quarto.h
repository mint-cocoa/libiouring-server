#pragma once

#include <string>

#include <sys/types.h>

namespace quarto {

struct RenderResult {
    bool success = false;
    std::string output_path;
    std::string error;
};

class QuartoRunner {
public:
    QuartoRunner(const std::string& workspace, const std::string& published,
                 const std::string& user_id, const std::string& profile = "production",
                 int timeout = 120);

    bool StartPreview(int port = 4000);
    void StopPreview();
    bool IsPreviewRunning() const;

    RenderResult RenderProduction(const std::string& slug);
    void RemovePublished(const std::string& slug);

    const std::string& UserId() const { return user_id_; }

private:
    std::string workspace_;
    std::string published_;
    std::string user_id_;
    std::string profile_;
    int timeout_;
    pid_t preview_pid_ = -1;
};

} // namespace quarto
