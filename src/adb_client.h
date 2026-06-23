#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

enum class EntryType {
    Directory,
    File,
    Other,
};

struct RemoteEntry {
    std::string name;
    EntryType type = EntryType::Other;
    long long modified_time = 0;
};

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

class AdbClient {
public:
    AdbClient(std::string adb_command, std::string serial, std::string verity_key = {});

    static bool IsAvailable(const std::string& adb_command);
    static std::optional<std::string> FirstDeviceSerial(const std::string& adb_command);

    CommandResult ListDirectory(const std::string& remote_path) const;
    CommandResult ListDirectory(const std::string& remote_path,
                                std::atomic_bool& cancel_requested,
                                std::atomic<int>& current_pid) const;
    int Pull(const std::string& remote_path,
             const std::string& local_dir,
             std::atomic_bool& cancel_requested,
             std::atomic<int>& current_pid) const;
    int Push(const std::string& local_path,
             const std::string& remote_dir,
             std::atomic_bool& cancel_requested,
             std::atomic<int>& current_pid) const;

    const std::string& serial() const { return serial_; }

private:
    CommandResult DisableVerity() const;
    CommandResult DisableVerity(std::atomic_bool& cancel_requested, std::atomic<int>& current_pid) const;

    std::string adb_command_;
    std::string serial_;
    std::string verity_key_;
};

std::vector<RemoteEntry> ParseDirectoryEntries(const std::string& output);
std::string JoinRemotePath(const std::string& base, const std::string& name);
std::string ParentRemotePath(const std::string& path);
