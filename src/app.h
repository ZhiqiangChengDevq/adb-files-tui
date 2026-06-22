#pragma once

#include <filesystem>
#include <string>

int RunAdbFilesTui(const std::filesystem::path& output_dir, const std::string& serial);
