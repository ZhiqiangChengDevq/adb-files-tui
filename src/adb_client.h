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
};

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

class AdbClient {
public:
    explicit AdbClient(std::string serial);

    static bool IsAvailable();
    static std::optional<std::string> FirstDeviceSerial();

    CommandResult ListDirectory(const std::string& remote_path) const;
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
    std::string serial_;
};

std::vector<RemoteEntry> ParseDirectoryEntries(const std::string& output);
std::string JoinRemotePath(const std::string& base, const std::string& name);
std::string ParentRemotePath(const std::string& path);
