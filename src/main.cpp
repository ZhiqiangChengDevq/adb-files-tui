#include "adb_client.h"
#include "app.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

int main(int argc, char** argv) {
    std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();

    std::error_code ec;
    if (!std::filesystem::exists(output_dir, ec)) {
        std::filesystem::create_directories(output_dir, ec);
    }
    if (ec || !std::filesystem::is_directory(output_dir)) {
        std::cerr << "Output directory is not available: " << output_dir << "\n";
        return 1;
    }

    if (!AdbClient::IsAvailable()) {
        std::cerr << "adb is not available in PATH.\n";
        return 1;
    }

    std::string serial;
    if (argc > 2) {
        serial = argv[2];
    } else {
        std::optional<std::string> detected = AdbClient::FirstDeviceSerial();
        if (!detected) {
            std::cerr << "No adb device with state 'device' was found.\n";
            return 1;
        }
        serial = *detected;
    }

    return RunAdbFilesTui(std::filesystem::absolute(output_dir), serial);
}
