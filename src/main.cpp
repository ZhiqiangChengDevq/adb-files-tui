#include "adb_client.h"
#include "app.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::string ResolveAdbCommand(int argc, char** argv) {
    if (argc <= 3 || std::string(argv[3]).empty()) {
        return "adb";
    }

    std::filesystem::path adb_path(argv[3]);
    std::error_code ec;
    if (std::filesystem::is_directory(adb_path, ec)) {
        adb_path /= "adb";
    }
    return adb_path.string();
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const std::string adb_command = ResolveAdbCommand(argc, argv);

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
    if (argc > 2 && !std::string(argv[2]).empty()) {
        serial = argv[2];
    } else {
        std::optional<std::string> detected = AdbClient::FirstDeviceSerial(adb_command);
        if (!detected) {
            std::cerr << "No adb device with state 'device' was found.\n";
            return 1;
        }
        serial = *detected;
    }

    return RunAdbFilesTui(std::filesystem::absolute(output_dir), serial, adb_command);
}
