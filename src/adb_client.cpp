#include "adb_client.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <signal.h>

namespace {

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string BuildCommand(const std::vector<std::string>& args) {
    std::string command;
    for (const auto& arg : args) {
        if (!command.empty()) {
            command += " ";
        }
        command += ShellQuote(arg);
    }
    command += " 2>&1";
    return command;
}

CommandResult RunCapture(const std::vector<std::string>& args) {
    CommandResult result;
    const std::string command = BuildCommand(args);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = std::strerror(errno);
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    const int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    return result;
}

std::string RemoteQuote(const std::string& value) {
    return ShellQuote(value);
}

std::string ListScript(const std::string& remote_path) {
    std::ostringstream script;
    script << "cd " << RemoteQuote(remote_path) << " 2>/dev/null || exit 2; "
           << "for entry in * .[!.]* ..?*; do "
           << "[ -e \"$entry\" ] || continue; "
           << "if [ -d \"$entry\" ]; then printf 'd\\t%s\\n' \"$entry\"; "
           << "elif [ -f \"$entry\" ]; then printf 'f\\t%s\\n' \"$entry\"; "
           << "else printf 'o\\t%s\\n' \"$entry\"; fi; "
           << "done";
    return script.str();
}

int RunCancellable(const std::vector<std::string>& args,
                   std::atomic_bool& cancel_requested,
                   std::atomic<int>& current_pid) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        int dev_null = open("/dev/null", O_RDWR);
        if (dev_null >= 0) {
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }

        execvp(argv[0], argv.data());
        _exit(127);
    }

    setpgid(pid, pid);
    current_pid.store(static_cast<int>(pid));

    int status = 0;
    while (true) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }
        if (waited < 0) {
            current_pid.store(-1);
            return -1;
        }
        if (cancel_requested.load()) {
            kill(-pid, SIGTERM);
            waitpid(pid, &status, 0);
            current_pid.store(-1);
            return 130;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    current_pid.store(-1);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

}  // namespace

AdbClient::AdbClient(std::string serial) : serial_(std::move(serial)) {}

bool AdbClient::IsAvailable() {
    return RunCapture({"adb", "version"}).exit_code == 0;
}

std::optional<std::string> AdbClient::FirstDeviceSerial() {
    CommandResult result = RunCapture({"adb", "devices"});
    if (result.exit_code != 0) {
        return std::nullopt;
    }

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.find("List of devices") != std::string::npos) {
            continue;
        }
        std::istringstream line_stream(line);
        std::string serial;
        std::string state;
        line_stream >> serial >> state;
        if (!serial.empty() && state == "device") {
            return serial;
        }
    }
    return std::nullopt;
}

CommandResult AdbClient::ListDirectory(const std::string& remote_path) const {
    return RunCapture({"adb", "-s", serial_, "shell", ListScript(remote_path)});
}

int AdbClient::Pull(const std::string& remote_path,
                    const std::string& local_dir,
                    std::atomic_bool& cancel_requested,
                    std::atomic<int>& current_pid) const {
    return RunCancellable({"adb", "-s", serial_, "pull", remote_path, local_dir},
                          cancel_requested,
                          current_pid);
}

int AdbClient::Push(const std::string& local_path,
                    const std::string& remote_dir,
                    std::atomic_bool& cancel_requested,
                    std::atomic<int>& current_pid) const {
    return RunCancellable({"adb", "-s", serial_, "push", local_path, remote_dir},
                          cancel_requested,
                          current_pid);
}

std::vector<RemoteEntry> ParseDirectoryEntries(const std::string& output) {
    std::vector<RemoteEntry> entries;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 3 || line[1] != '\t') {
            continue;
        }

        RemoteEntry entry;
        entry.name = line.substr(2);
        if (entry.name.empty() || entry.name == "." || entry.name == "..") {
            continue;
        }

        if (line[0] == 'd') {
            entry.type = EntryType::Directory;
        } else if (line[0] == 'f') {
            entry.type = EntryType::File;
        } else {
            entry.type = EntryType::Other;
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& lhs, const RemoteEntry& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type == EntryType::Directory;
        }
        return lhs.name < rhs.name;
    });
    return entries;
}

std::string JoinRemotePath(const std::string& base, const std::string& name) {
    if (base.empty() || base == "/") {
        return "/" + name;
    }
    return base + "/" + name;
}

std::string ParentRemotePath(const std::string& path) {
    if (path.empty() || path == "/") {
        return "/";
    }
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    const auto slash = trimmed.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return trimmed.substr(0, slash);
}
