#include "config.h"
#include "gateway.h"
#include "editor.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>

static void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --config <path>\n"
              << "\nRuns as gateway or editor based on config file 'mode' field.\n"
              << "\nOptions:\n"
              << "  --config <path>   Path to YAML config file\n"
              << "  --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    if (config_path.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    try {
        auto config = quarto::Config::Load(config_path);
        spdlog::info("quarto-server starting in {} mode", config.mode);

        if (config.mode == "gateway") {
            return quarto::run_gateway(config);
        } else if (config.mode == "editor") {
            return quarto::run_editor(config);
        } else {
            spdlog::error("Unknown mode: {}", config.mode);
            return 1;
        }
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }
}
