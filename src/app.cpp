#include "app.h"

#include "adb_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <signal.h>

namespace {

constexpr const char* kAppName = "adb-files-tui";
constexpr const char* kVersion = ADB_FILES_TUI_VERSION;

struct CopyState {
    bool active = false;
    bool finished = false;
    bool cancelled = false;
    int completed = 0;
    int total = 0;
    std::string title;
    std::string message;
};

struct TuiState {
    bool chinese = true;
    std::string current_path = "/";
    std::vector<RemoteEntry> entries;
    std::vector<bool> selected;
    int cursor = 0;
    std::string status;
    int path_scroll = 0;
    bool show_input = false;
    std::string import_path;
    CopyState copy;
};

std::string Tr(bool chinese, const std::string& zh, const std::string& en) {
    return chinese ? zh : en;
}

std::string Timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&raw, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string EntryPrefix(const RemoteEntry& entry) {
    switch (entry.type) {
        case EntryType::Directory:
            return "[D] ";
        case EntryType::File:
            return "[F] ";
        case EntryType::Other:
            return "[O] ";
    }
    return "[?] ";
}

ftxui::Element Header(const TuiState& state) {
    using namespace ftxui;
    return hbox({
               text(std::string("zh/en:") + (state.chinese ? "zh" : "en")) | size(WIDTH, EQUAL, 12),
               text(kAppName) | bold | hcenter | flex,
               text(std::string("v") + kVersion) | align_right | size(WIDTH, EQUAL, 12),
           }) |
           size(HEIGHT, EQUAL, 1);
}

std::string ScrolledPath(const std::string& path, int width, int offset) {
    if (width <= 0 || static_cast<int>(path.size()) <= width) {
        return path;
    }
    std::string loop = path + "   ";
    int start = offset % static_cast<int>(loop.size());
    std::string doubled = loop + loop;
    return doubled.substr(start, width);
}

ftxui::Element Footer(const TuiState& state, int width) {
    using namespace ftxui;
    return text(ScrolledPath(state.current_path, std::max(10, width), state.path_scroll)) |
           size(HEIGHT, EQUAL, 1);
}

ftxui::Element FileList(const TuiState& state) {
    using namespace ftxui;
    Elements rows;
    if (!state.status.empty()) {
        rows.push_back(text(state.status) | color(Color::Yellow));
    }
    if (state.entries.empty()) {
        rows.push_back(text(Tr(state.chinese, "空目录或无法读取目录", "Empty or unreadable directory")) | dim);
    }

    for (size_t i = 0; i < state.entries.size(); ++i) {
        const bool checked = i < state.selected.size() && state.selected[i];
        std::string row = std::string(checked ? "[x] " : "[ ] ") + EntryPrefix(state.entries[i]) +
                          state.entries[i].name;
        auto element = text(row);
        if (static_cast<int>(i) == state.cursor) {
            element = element | inverted | focus;
        }
        rows.push_back(element);
    }
    return vbox(std::move(rows)) | yframe | vscroll_indicator | flex;
}

ftxui::Element CopyModal(const TuiState& state) {
    using namespace ftxui;
    const float progress = state.copy.total <= 0
                               ? 0.0F
                               : static_cast<float>(state.copy.completed) /
                                     static_cast<float>(state.copy.total);
    std::ostringstream count;
    count << state.copy.completed << "/" << state.copy.total;
    return window(text(state.copy.title),
                  vbox({
                      text(state.copy.message),
                      gauge(progress),
                      text(count.str()),
                      text(Tr(state.chinese, "Esc 取消/关闭", "Esc cancel/close")) | dim,
                  }) | size(WIDTH, GREATER_THAN, 42));
}

ftxui::Element InputModal(const TuiState& state, ftxui::Element input_element) {
    using namespace ftxui;
    return window(text(Tr(state.chinese, "导入文件", "Import file")),
                  vbox({
                      text(Tr(state.chinese, "输入本地文件绝对路径，回车开始导入", "Enter a local absolute file path")),
                      input_element,
                      text(Tr(state.chinese, "Esc 关闭", "Esc close")) | dim,
                  }) | size(WIDTH, GREATER_THAN, 58));
}

void ResetSelection(TuiState& state) {
    state.selected.assign(state.entries.size(), false);
    if (state.cursor >= static_cast<int>(state.entries.size())) {
        state.cursor = state.entries.empty() ? 0 : static_cast<int>(state.entries.size()) - 1;
    }
}

void LoadDirectory(TuiState& state, const AdbClient& adb, const std::string& path) {
    CommandResult result = adb.ListDirectory(path);
    state.current_path = path;
    state.entries = ParseDirectoryEntries(result.output);
    state.cursor = 0;
    ResetSelection(state);
    if (result.exit_code == 0) {
        state.status.clear();
    } else {
        state.status = Tr(state.chinese, "目录读取失败: ", "Failed to read directory: ") + result.output;
    }
}

void StartExport(TuiState& state,
                 const AdbClient& adb,
                 const std::filesystem::path& output_dir,
                 ftxui::ScreenInteractive* screen,
                 std::atomic_bool& cancel_requested,
                 std::atomic<int>& current_pid,
                 std::thread& transfer_thread) {
    std::vector<std::string> paths;
    for (size_t i = 0; i < state.entries.size(); ++i) {
        if (i < state.selected.size() && state.selected[i]) {
            paths.push_back(JoinRemotePath(state.current_path, state.entries[i].name));
        }
    }
    if (paths.empty()) {
        state.status = Tr(state.chinese, "请先选择要导出的文件或文件夹", "Select files or folders before exporting");
        return;
    }

    const auto target_dir = output_dir / Timestamp();
    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        state.status = Tr(state.chinese, "创建输出目录失败: ", "Failed to create output directory: ") + ec.message();
        return;
    }

    state.copy = {};
    state.copy.active = true;
    state.copy.total = static_cast<int>(paths.size());
    state.copy.title = Tr(state.chinese, "导出", "Export");
    state.copy.message = target_dir.string();
    cancel_requested.store(false);

    if (transfer_thread.joinable()) {
        transfer_thread.join();
    }

    transfer_thread = std::thread([paths = std::move(paths), target_dir, &state, &adb, screen, &cancel_requested, &current_pid]() {
        int failures = 0;
        for (const auto& path : paths) {
            if (cancel_requested.load()) {
                break;
            }
            int code = adb.Pull(path, target_dir.string(), cancel_requested, current_pid);
            if (code != 0) {
                ++failures;
            }
            screen->Post([&state] { ++state.copy.completed; });
            screen->PostEvent(ftxui::Event::Custom);
        }
        const bool cancelled = cancel_requested.load();
        screen->Post([&state, cancelled, failures] {
            state.copy.finished = true;
            state.copy.cancelled = cancelled;
            if (state.copy.cancelled) {
                state.copy.message = "Cancelled";
            } else if (failures == 0) {
                state.copy.message = "Done";
            } else {
                state.copy.message = std::to_string(failures) + " failed";
            }
        });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

void StartImport(TuiState& state,
                 const AdbClient& adb,
                 ftxui::ScreenInteractive* screen,
                 std::atomic_bool& cancel_requested,
                 std::atomic<int>& current_pid,
                 std::thread& transfer_thread) {
    std::filesystem::path source(state.import_path);
    if (!source.is_absolute() || !std::filesystem::exists(source) ||
        !std::filesystem::is_regular_file(source)) {
        state.status = Tr(state.chinese, "本地文件不存在或不是普通文件", "Local file does not exist or is not a regular file");
        state.show_input = false;
        return;
    }

    state.copy = {};
    state.copy.active = true;
    state.copy.total = 1;
    state.copy.title = Tr(state.chinese, "导入", "Import");
    state.copy.message = source.string();
    state.show_input = false;
    cancel_requested.store(false);

    if (transfer_thread.joinable()) {
        transfer_thread.join();
    }

    transfer_thread = std::thread([source, remote_dir = state.current_path, &state, &adb, screen, &cancel_requested, &current_pid]() {
        int code = adb.Push(source.string(), remote_dir, cancel_requested, current_pid);
        const bool cancelled = cancel_requested.load();
        screen->Post([&state, code, cancelled] {
            if (code == 0) {
                state.copy.completed = 1;
                state.copy.message = "Done";
            } else if (cancelled) {
                state.copy.message = "Cancelled";
                state.copy.cancelled = true;
            } else {
                state.copy.message = "Failed";
            }
            state.copy.finished = true;
        });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

}  // namespace

int RunAdbFilesTui(const std::filesystem::path& output_dir, const std::string& serial) {
    using namespace ftxui;

    AdbClient adb(serial);
    TuiState state;
    std::atomic_bool cancel_requested{false};
    std::atomic<int> current_pid{-1};
    std::thread transfer_thread;

    auto screen = ScreenInteractive::Fullscreen();
    LoadDirectory(state, adb, "/");

    auto input = Input(&state.import_path, "local absolute path");

    auto renderer = Renderer(input, [&] {
        Element body = vbox({
            Header(state),
            FileList(state),
            Footer(state, screen.dimx()),
        });

        if (state.show_input) {
            body = dbox({
                body,
                InputModal(state, input->Render()) | clear_under | center,
            });
        }
        if (state.copy.active) {
            body = dbox({
                body,
                CopyModal(state) | clear_under | center,
            });
        }
        return body;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Custom) {
            ++state.path_scroll;
            return false;
        }

        if (state.copy.active) {
            if (event == Event::Escape) {
                if (state.copy.finished) {
                    state.copy.active = false;
                    state.status = state.copy.message;
                } else {
                    cancel_requested.store(true);
                    int pid = current_pid.load();
                    if (pid > 0) {
                        kill(-pid, SIGTERM);
                    }
                    state.copy.message = Tr(state.chinese, "正在取消...", "Cancelling...");
                }
                return true;
            }
            return true;
        }

        if (state.show_input) {
            if (event == Event::Escape) {
                state.show_input = false;
                state.import_path.clear();
                return true;
            }
            if (event == Event::Return) {
                StartImport(state, adb, &screen, cancel_requested, current_pid, transfer_thread);
                return true;
            }
            return false;
        }

        if (event == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (event == Event::L || event == Event::l) {
            state.chinese = !state.chinese;
            return true;
        }
        if (event == Event::ArrowUp) {
            if (state.cursor > 0) {
                --state.cursor;
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            if (state.cursor + 1 < static_cast<int>(state.entries.size())) {
                ++state.cursor;
            }
            return true;
        }
        if (event == Event::ArrowRight) {
            if (!state.entries.empty() && state.entries[state.cursor].type == EntryType::Directory) {
                LoadDirectory(state, adb, JoinRemotePath(state.current_path, state.entries[state.cursor].name));
            }
            return true;
        }
        if (event == Event::ArrowLeft) {
            if (state.current_path != "/") {
                LoadDirectory(state, adb, ParentRemotePath(state.current_path));
            }
            return true;
        }
        if (event == Event::Character(' ')) {
            if (!state.entries.empty() && state.cursor < static_cast<int>(state.selected.size())) {
                state.selected[state.cursor] = !state.selected[state.cursor];
            }
            return true;
        }
        if (event == Event::E || event == Event::e) {
            StartExport(state, adb, output_dir, &screen, cancel_requested, current_pid, transfer_thread);
            return true;
        }
        if (event == Event::I || event == Event::i) {
            state.import_path.clear();
            state.show_input = true;
            return true;
        }
        return false;
    });

    std::atomic_bool ticker_running{true};
    std::thread ticker([&] {
        while (ticker_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(component);
    ticker_running.store(false);
    if (ticker.joinable()) {
        ticker.join();
    }
    cancel_requested.store(true);
    int pid = current_pid.load();
    if (pid > 0) {
        kill(-pid, SIGTERM);
    }
    if (transfer_thread.joinable()) {
        transfer_thread.join();
    }
    return 0;
}
