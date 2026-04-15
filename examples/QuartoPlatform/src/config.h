#pragma once
#include <string>
namespace quarto {
struct Config {
    static Config Load(const std::string& path);
    std::string mode;
};
}
