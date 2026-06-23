#include "adb_client.h"
#include "app.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    std::vector<std::string> positional;
    std::string verity_key;
    std::string error;
};

CliOptions ParseArgs(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p") {
            if (i + 1 >= argc) {
                options.error = "-p requires a permission key value.";
                return options;
            }
            options.verity_key = argv[++i];
            continue;
        }
        options.positional.push_back(std::move(arg));
    }
    return options;
}

std::string ResolveAdbCommand(const std::vector<std::string>& positional) {
    if (positional.size() < 3 || positional[2].empty()) {
        return "adb";
    }

    std::filesystem::path adb_path(positional[2]);
    std::error_code ec;
    if (std::filesystem::is_directory(adb_path, ec)) {
        adb_path /= "adb";
    }
    return adb_path.string();
}

}  // namespace

int main(int argc, char** argv) {
    CliOptions options = ParseArgs(argc, argv);
    if (!options.error.empty()) {
        std::cerr << options.error << "\n";
        return 1;
    }

    std::filesystem::path output_dir =
        !options.positional.empty() ? std::filesystem::path(options.positional[0]) : std::filesystem::current_path();
    const std::string adb_command = ResolveAdbCommand(options.positional);

    std::error_code ec;
    if (!std::filesystem::exists(output_dir, ec)) {
        std::filesystem::create_directories(output_dir, ec);
    }
    if (ec || !std::filesystem::is_directory(output_dir)) {
        std::cerr << "Output directory is not available: " << output_dir << "\n";
        return 1;
    }

    if (!AdbClient::IsAvailable(adb_command)) {
        std::cerr << "adb is not available: " << adb_command << "\n";
        return 1;
    }

    std::string serial;
    if (options.positional.size() > 1 && !options.positional[1].empty()) {
        serial = options.positional[1];
    } else {
        std::optional<std::string> detected = AdbClient::FirstDeviceSerial(adb_command);
        if (!detected) {
            std::cerr << "No adb device with state 'device' was found.\n";
            return 1;
        }
        serial = *detected;
    }

    return RunAdbFilesTui(std::filesystem::absolute(output_dir), serial, adb_command, options.verity_key);
}
