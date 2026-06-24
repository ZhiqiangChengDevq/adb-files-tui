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
constexpr int kHelpPanelWidth = 30;
constexpr int kFooterStatusWidth = 10;
constexpr int kEntryTimeWidth = 18;
constexpr int kPreviewMaxWidth = 88;
constexpr int kPreviewMinWidth = 52;
constexpr int kPreviewHeight = 18;

struct CopyState {
    bool active = false;
    bool finished = false;
    bool cancelled = false;
    int completed = 0;
    int total = 0;
    std::string title;
    std::string message;
};

struct PreviewState {
    bool active = false;
    bool loading = false;
    bool cancelled = false;
    int scroll = 0;
    int request_id = 0;
    std::string title;
    std::string content;
    std::string message;
};

enum class SortMode {
    Name,
    Time,
};

struct TuiState {
    bool chinese = true;
    SortMode sort_mode = SortMode::Name;
    bool loading = false;
    int load_request_id = 0;
    std::string current_path = "/";
    std::vector<RemoteEntry> entries;
    std::vector<bool> selected;
    int cursor = 0;
    std::string status;
    int path_scroll = 0;
    bool show_input = false;
    std::string import_path;
    CopyState copy;
    PreviewState preview;
};

std::string Tr(bool chinese, const std::string& zh, const std::string& en) {
    return chinese ? zh : en;
}

int TypeRank(EntryType type) {
    switch (type) {
        case EntryType::Directory:
            return 0;
        case EntryType::File:
            return 1;
        case EntryType::Other:
            return 2;
    }
    return 3;
}

bool EntryLess(const RemoteEntry& lhs, const RemoteEntry& rhs, SortMode sort_mode) {
    const int lhs_type = TypeRank(lhs.type);
    const int rhs_type = TypeRank(rhs.type);
    if (lhs_type != rhs_type) {
        return lhs_type < rhs_type;
    }

    if (sort_mode == SortMode::Time && lhs.modified_time != rhs.modified_time) {
        return lhs.modified_time > rhs.modified_time;
    }
    return lhs.name < rhs.name;
}

void SortEntries(std::vector<RemoteEntry>& entries, SortMode sort_mode) {
    std::sort(entries.begin(), entries.end(), [sort_mode](const RemoteEntry& lhs, const RemoteEntry& rhs) {
        return EntryLess(lhs, rhs, sort_mode);
    });
}

void SortStateEntries(TuiState& state) {
    std::vector<std::pair<RemoteEntry, bool>> rows;
    rows.reserve(state.entries.size());
    for (size_t i = 0; i < state.entries.size(); ++i) {
        rows.push_back({std::move(state.entries[i]), i < state.selected.size() && state.selected[i]});
    }

    std::sort(rows.begin(), rows.end(), [&state](const auto& lhs, const auto& rhs) {
        return EntryLess(lhs.first, rhs.first, state.sort_mode);
    });

    state.entries.clear();
    state.selected.clear();
    state.entries.reserve(rows.size());
    state.selected.reserve(rows.size());
    for (auto& row : rows) {
        state.entries.push_back(std::move(row.first));
        state.selected.push_back(row.second);
    }

    if (state.cursor >= static_cast<int>(state.entries.size())) {
        state.cursor = state.entries.empty() ? 0 : static_cast<int>(state.entries.size()) - 1;
    }
}

std::string SortModeLabel(const TuiState& state) {
    if (state.sort_mode == SortMode::Name) {
        return Tr(state.chinese, "排序:名称", "Sort:Name");
    }
    return Tr(state.chinese, "排序:时间", "Sort:Time");
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

std::string EntryTimeLabel(const RemoteEntry& entry) {
    if (!entry.modified_label.empty()) {
        return entry.modified_label;
    }
    if (entry.modified_time <= 0) {
        return "";
    }

    std::time_t raw = static_cast<std::time_t>(entry.modified_time);
    std::tm tm{};
    localtime_r(&raw, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return out.str();
}

ftxui::Element Header(const TuiState& state) {
    using namespace ftxui;
    return hbox({
               text(std::string("zh/en:") + (state.chinese ? "zh" : "en")) | size(WIDTH, EQUAL, 12),
               text(kAppName) | bold | hcenter | flex,
               text(SortModeLabel(state) + " v" + kVersion) | align_right | size(WIDTH, EQUAL, 24),
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
    const int path_width = std::max(10, width - kFooterStatusWidth - 1);
    return hbox({
               text(ScrolledPath(state.current_path, path_width, state.path_scroll)) | flex,
               text(state.loading ? "Loading" : "Loaded") | align_right |
                   size(WIDTH, EQUAL, kFooterStatusWidth),
           }) |
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
        auto element = hbox({
            text(row) | flex,
            text(EntryTimeLabel(state.entries[i])) | dim | align_right |
                size(WIDTH, EQUAL, kEntryTimeWidth),
        });
        if (static_cast<int>(i) == state.cursor) {
            element = element | inverted | focus;
        }
        rows.push_back(element);
    }
    return vbox(std::move(rows)) | yframe | vscroll_indicator | flex;
}

ftxui::Element HelpPanel(const TuiState& state) {
    using namespace ftxui;
    Elements rows{
        text(Tr(state.chinese, "按键说明", "Controls")) | bold,
        separator(),
        text(Tr(state.chinese, "↑ / W  上移", "↑ / W  Up")),
        text(Tr(state.chinese, "↓ / S  下移", "↓ / S  Down")),
        text(Tr(state.chinese, "→ / D  进入目录", "→ / D  Enter")),
        text(Tr(state.chinese, "← / A  返回上级", "← / A  Back")),
        text(Tr(state.chinese, "Space  选择/取消", "Space  Toggle")),
        text(Tr(state.chinese, "E      导出选中", "E      Export")),
        text(Tr(state.chinese, "I      导入文件", "I      Import")),
        text(Tr(state.chinese, "T      删除选中", "T      Delete")),
        text(Tr(state.chinese, "P      预览文件", "P      Preview")),
        text(Tr(state.chinese, "O      切换排序", "O      Sort mode")),
        text(Tr(state.chinese, "L      切换语言", "L      Language")),
        text(Tr(state.chinese, "Esc    取消加载/退出", "Esc    Cancel/Exit")),
    };
    return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, kHelpPanelWidth);
}

ftxui::Element MainArea(const TuiState& state) {
    using namespace ftxui;
    return hbox({
               FileList(state) | flex,
               HelpPanel(state),
           }) |
           flex;
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

std::string FinalCopyMessage(bool chinese, bool cancelled, int failures) {
    if (cancelled) {
        return Tr(chinese, "已取消", "Cancelled");
    }
    if (failures == 0) {
        return Tr(chinese, "已完成", "Completed");
    }
    return std::to_string(failures) + Tr(chinese, " 项失败", " failed");
}

void AutoCloseCopyModal(TuiState& state, ftxui::ScreenInteractive* screen) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    screen->Post([&state] {
        if (state.copy.active && state.copy.finished) {
            state.status = state.copy.message;
            state.copy.active = false;
        }
    });
    screen->PostEvent(ftxui::Event::Custom);
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

std::vector<std::string> WrapText(const std::string& content, int width) {
    std::vector<std::string> lines;
    if (width <= 0) {
        return lines;
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        for (char& c : line) {
            const unsigned char value = static_cast<unsigned char>(c);
            if (c == '\t') {
                c = ' ';
            } else if (value < 32 || value == 127) {
                c = '.';
            }
        }
        if (line.empty()) {
            lines.push_back("");
            continue;
        }
        for (size_t start = 0; start < line.size(); start += static_cast<size_t>(width)) {
            lines.push_back(line.substr(start, static_cast<size_t>(width)));
        }
    }
    if (content.empty()) {
        lines.push_back("");
    }
    return lines;
}

int PreviewContentWidth(int screen_width) {
    const int modal_width = std::max(kPreviewMinWidth, std::min(kPreviewMaxWidth, screen_width - 8));
    return std::max(20, modal_width - 4);
}

int PreviewMaxScroll(const TuiState& state, int screen_width) {
    const int content_width = PreviewContentWidth(screen_width);
    const std::vector<std::string> wrapped = WrapText(state.preview.content, content_width);
    return std::max(0, static_cast<int>(wrapped.size()) - kPreviewHeight);
}

ftxui::Element PreviewModal(const TuiState& state, int screen_width) {
    using namespace ftxui;
    const int content_width = PreviewContentWidth(screen_width);
    Elements rows;

    if (!state.preview.message.empty()) {
        rows.push_back(text(state.preview.message) | color(Color::Yellow));
        rows.push_back(separator());
    }

    if (state.preview.loading) {
        rows.push_back(text(Tr(state.chinese, "正在读取预览内容...", "Loading preview...")) | dim);
    } else {
        std::vector<std::string> wrapped = WrapText(state.preview.content, content_width);
        const int max_scroll = PreviewMaxScroll(state, screen_width);
        const int scroll = std::max(0, std::min(state.preview.scroll, max_scroll));
        Elements content_rows;
        for (int i = 0; i < kPreviewHeight; ++i) {
            int index = scroll + i;
            if (index >= static_cast<int>(wrapped.size())) {
                break;
            }
            content_rows.push_back(text(wrapped[static_cast<size_t>(index)]));
        }
        if (content_rows.empty()) {
            content_rows.push_back(text(Tr(state.chinese, "无可显示内容", "No preview content")) | dim);
        }
        rows.push_back(vbox(std::move(content_rows)) | size(WIDTH, EQUAL, content_width) |
                       size(HEIGHT, EQUAL, kPreviewHeight));
    }

    rows.push_back(separator());
    rows.push_back(text(Tr(state.chinese, "↑/↓ 滚动，Esc 关闭/取消", "↑/↓ scroll, Esc close/cancel")) | dim);
    return window(text(state.preview.title), vbox(std::move(rows)) | size(WIDTH, EQUAL, content_width));
}

void ResetSelection(TuiState& state) {
    state.selected.assign(state.entries.size(), false);
    if (state.cursor >= static_cast<int>(state.entries.size())) {
        state.cursor = state.entries.empty() ? 0 : static_cast<int>(state.entries.size()) - 1;
    }
}

std::vector<std::string> CurrentTargets(const TuiState& state) {
    std::vector<std::string> paths;
    for (size_t i = 0; i < state.entries.size(); ++i) {
        if (i < state.selected.size() && state.selected[i]) {
            paths.push_back(JoinRemotePath(state.current_path, state.entries[i].name));
        }
    }

    if (paths.empty() && !state.entries.empty() &&
        state.cursor >= 0 && state.cursor < static_cast<int>(state.entries.size())) {
        paths.push_back(JoinRemotePath(state.current_path, state.entries[state.cursor].name));
    }
    return paths;
}

void LoadDirectory(TuiState& state, const AdbClient& adb, const std::string& path) {
    CommandResult result = adb.ListDirectory(path);
    state.current_path = path;
    state.entries = ParseDirectoryEntries(result.output);
    SortEntries(state.entries, state.sort_mode);
    state.cursor = 0;
    ResetSelection(state);
    if (result.exit_code == 0) {
        state.status.clear();
    } else {
        state.status = Tr(state.chinese, "目录读取失败: ", "Failed to read directory: ") + result.output;
    }
}

void StartLoadDirectory(TuiState& state,
                        const AdbClient& adb,
                        const std::string& path,
                        ftxui::ScreenInteractive* screen,
                        std::atomic_bool& load_cancel_requested,
                        std::atomic<int>& load_current_pid,
                        std::thread& load_thread) {
    if (load_thread.joinable()) {
        load_thread.join();
    }

    state.loading = true;
    state.status = Tr(state.chinese, "正在读取目录: ", "Loading directory: ") + path;
    const int request_id = ++state.load_request_id;
    load_cancel_requested.store(false);
    load_current_pid.store(-1);

    load_thread = std::thread([path,
                               request_id,
                               &state,
                               &adb,
                               screen,
                               &load_cancel_requested,
                               &load_current_pid] {
        CommandResult result = adb.ListDirectory(path, load_cancel_requested, load_current_pid);
        const bool cancelled = load_cancel_requested.load() || result.exit_code == 130;
        std::vector<RemoteEntry> entries;
        if (result.exit_code == 0) {
            entries = ParseDirectoryEntries(result.output);
        }

        screen->Post([&state,
                      path,
                      request_id,
                      cancelled,
                      exit_code = result.exit_code,
                      output = std::move(result.output),
                      entries = std::move(entries)]() mutable {
            if (request_id != state.load_request_id) {
                return;
            }

            state.loading = false;
            if (cancelled) {
                state.status = Tr(state.chinese, "目录读取已取消", "Directory load cancelled");
                return;
            }

            if (exit_code == 0) {
                state.current_path = path;
                state.entries = std::move(entries);
                SortEntries(state.entries, state.sort_mode);
                state.cursor = 0;
                ResetSelection(state);
                state.status.clear();
            } else {
                state.status = Tr(state.chinese, "目录读取失败: ", "Failed to read directory: ") + output;
            }
        });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

void StartExport(TuiState& state,
                 const AdbClient& adb,
                 const std::filesystem::path& output_dir,
                 ftxui::ScreenInteractive* screen,
                 std::atomic_bool& cancel_requested,
                 std::atomic<int>& current_pid,
                 std::thread& transfer_thread) {
    std::vector<std::string> paths = CurrentTargets(state);
    if (paths.empty()) {
        state.status = Tr(state.chinese, "没有可导出的文件或文件夹", "No file or folder is available to export");
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
            state.copy.message = FinalCopyMessage(state.chinese, state.copy.cancelled, failures);
        });
        screen->PostEvent(ftxui::Event::Custom);
        AutoCloseCopyModal(state, screen);
    });
}

void StartDelete(TuiState& state,
                 const AdbClient& adb,
                 ftxui::ScreenInteractive* screen,
                 std::atomic_bool& cancel_requested,
                 std::atomic<int>& current_pid,
                 std::thread& transfer_thread) {
    std::vector<std::string> paths = CurrentTargets(state);
    if (paths.empty()) {
        state.status = Tr(state.chinese, "没有可删除的文件或文件夹", "No file or folder is available to delete");
        return;
    }

    state.copy = {};
    state.copy.active = true;
    state.copy.total = static_cast<int>(paths.size());
    state.copy.title = Tr(state.chinese, "删除", "Delete");
    state.copy.message = Tr(state.chinese, "正在删除...", "Deleting...");
    cancel_requested.store(false);

    if (transfer_thread.joinable()) {
        transfer_thread.join();
    }

    transfer_thread = std::thread([paths = std::move(paths),
                                   remote_dir = state.current_path,
                                   &state,
                                   &adb,
                                   screen,
                                   &cancel_requested,
                                   &current_pid] {
        int failures = 0;
        for (const auto& path : paths) {
            if (cancel_requested.load()) {
                break;
            }
            int code = adb.Remove(path, cancel_requested, current_pid);
            if (code != 0) {
                ++failures;
            }
            screen->Post([&state] { ++state.copy.completed; });
            screen->PostEvent(ftxui::Event::Custom);
        }

        const bool cancelled = cancel_requested.load();
        CommandResult refresh_result;
        std::vector<RemoteEntry> refreshed_entries;
        if (!cancelled) {
            refresh_result = adb.ListDirectory(remote_dir);
            refreshed_entries = ParseDirectoryEntries(refresh_result.output);
        }

        screen->Post([&state,
                      cancelled,
                      failures,
                      remote_dir,
                      refresh_exit_code = refresh_result.exit_code,
                      refresh_output = std::move(refresh_result.output),
                      refreshed_entries = std::move(refreshed_entries)]() mutable {
            if (!cancelled && refresh_exit_code == 0) {
                state.current_path = remote_dir;
                state.entries = std::move(refreshed_entries);
                SortEntries(state.entries, state.sort_mode);
                state.cursor = 0;
                ResetSelection(state);
            }
            state.copy.finished = true;
            state.copy.cancelled = cancelled;
            state.copy.message = FinalCopyMessage(state.chinese, state.copy.cancelled, failures);
            if (!cancelled && refresh_exit_code != 0) {
                state.copy.message += Tr(state.chinese, "，但刷新目录失败", ", but directory refresh failed");
                if (!refresh_output.empty()) {
                    state.copy.message += ": " + refresh_output;
                }
            }
        });
        screen->PostEvent(ftxui::Event::Custom);
        AutoCloseCopyModal(state, screen);
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
        CommandResult refresh_result;
        std::vector<RemoteEntry> refreshed_entries;
        if (code == 0) {
            refresh_result = adb.ListDirectory(remote_dir);
            refreshed_entries = ParseDirectoryEntries(refresh_result.output);
        }

        screen->Post([&state,
                      code,
                      cancelled,
                      remote_dir,
                      refresh_exit_code = refresh_result.exit_code,
                      refresh_output = std::move(refresh_result.output),
                      refreshed_entries = std::move(refreshed_entries)]() mutable {
            if (code == 0) {
                state.current_path = remote_dir;
                state.entries = std::move(refreshed_entries);
                SortEntries(state.entries, state.sort_mode);
                state.cursor = 0;
                ResetSelection(state);
                state.copy.completed = 1;
                state.copy.message = Tr(state.chinese, "已完成", "Completed");
                if (refresh_exit_code != 0) {
                    state.copy.message += Tr(state.chinese, "，但刷新目录失败", ", but directory refresh failed");
                    if (!refresh_output.empty()) {
                        state.copy.message += ": " + refresh_output;
                    }
                }
            } else if (cancelled) {
                state.copy.completed = 1;
                state.copy.message = Tr(state.chinese, "已取消", "Cancelled");
                state.copy.cancelled = true;
            } else {
                state.copy.completed = 1;
                state.copy.message = Tr(state.chinese, "导入失败", "Import failed");
            }
            state.copy.finished = true;
        });
        screen->PostEvent(ftxui::Event::Custom);
        AutoCloseCopyModal(state, screen);
    });
}

void StartPreview(TuiState& state,
                  const AdbClient& adb,
                  ftxui::ScreenInteractive* screen,
                  std::atomic_bool& preview_cancel_requested,
                  std::atomic<int>& preview_current_pid,
                  std::thread& preview_thread) {
    if (state.entries.empty() || state.cursor < 0 || state.cursor >= static_cast<int>(state.entries.size())) {
        state.status = Tr(state.chinese, "没有可预览的文件", "No file is available to preview");
        return;
    }

    if (preview_thread.joinable()) {
        preview_thread.join();
    }

    const std::string path = JoinRemotePath(state.current_path, state.entries[state.cursor].name);
    state.preview.active = true;
    state.preview.loading = true;
    state.preview.cancelled = false;
    state.preview.scroll = 0;
    state.preview.title = path;
    state.preview.content.clear();
    state.preview.message.clear();
    const int request_id = ++state.preview.request_id;
    preview_cancel_requested.store(false);
    preview_current_pid.store(-1);

    preview_thread = std::thread([path,
                                  request_id,
                                  &state,
                                  &adb,
                                  screen,
                                  &preview_cancel_requested,
                                  &preview_current_pid] {
        CommandResult result = adb.Cat(path, preview_cancel_requested, preview_current_pid);
        const bool cancelled = preview_cancel_requested.load() || result.exit_code == 130;
        screen->Post([&state,
                      request_id,
                      cancelled,
                      exit_code = result.exit_code,
                      output = std::move(result.output)]() mutable {
            if (request_id != state.preview.request_id) {
                return;
            }
            state.preview.loading = false;
            state.preview.cancelled = cancelled;
            if (cancelled) {
                state.preview.message = Tr(state.chinese, "预览已取消", "Preview cancelled");
                state.preview.content.clear();
            } else if (exit_code == 0) {
                state.preview.message.clear();
                state.preview.content = std::move(output);
            } else {
                state.preview.message = Tr(state.chinese, "预览失败: ", "Preview failed: ") + output;
                state.preview.content.clear();
            }
        });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

bool IsUpEvent(const ftxui::Event& event) {
    return event == ftxui::Event::ArrowUp || event == ftxui::Event::W || event == ftxui::Event::w;
}

bool IsDownEvent(const ftxui::Event& event) {
    return event == ftxui::Event::ArrowDown || event == ftxui::Event::S || event == ftxui::Event::s;
}

bool IsRightEvent(const ftxui::Event& event) {
    return event == ftxui::Event::ArrowRight || event == ftxui::Event::D || event == ftxui::Event::d;
}

bool IsLeftEvent(const ftxui::Event& event) {
    return event == ftxui::Event::ArrowLeft || event == ftxui::Event::A || event == ftxui::Event::a;
}

}  // namespace

int RunAdbFilesTui(const std::filesystem::path& output_dir,
                   const std::string& serial,
                   const std::string& adb_command,
                   const std::string& verity_key) {
    using namespace ftxui;

    AdbClient adb(adb_command, serial, verity_key);
    TuiState state;
    std::atomic_bool cancel_requested{false};
    std::atomic<int> current_pid{-1};
    std::atomic_bool load_cancel_requested{false};
    std::atomic<int> load_current_pid{-1};
    std::atomic_bool preview_cancel_requested{false};
    std::atomic<int> preview_current_pid{-1};
    std::thread transfer_thread;
    std::thread load_thread;
    std::thread preview_thread;

    auto screen = ScreenInteractive::Fullscreen();
    LoadDirectory(state, adb, "/");

    auto input = Input(&state.import_path, "local absolute path");

    auto renderer = Renderer(input, [&] {
        Element body = vbox({
            Header(state),
            MainArea(state),
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
        if (state.preview.active) {
            body = dbox({
                body,
                PreviewModal(state, screen.dimx()) | clear_under | center,
            });
        }
        return body;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Custom) {
            ++state.path_scroll;
            return false;
        }

        if (state.preview.active) {
            if (event == Event::Escape) {
                if (state.preview.loading) {
                    preview_cancel_requested.store(true);
                    int pid = preview_current_pid.load();
                    if (pid > 0) {
                        kill(-pid, SIGTERM);
                    }
                    state.preview.message = Tr(state.chinese, "正在取消预览...", "Cancelling preview...");
                } else {
                    state.preview.active = false;
                    state.preview.content.clear();
                    state.preview.message.clear();
                }
                return true;
            }
            if (!state.preview.loading && IsUpEvent(event)) {
                const int max_scroll = PreviewMaxScroll(state, screen.dimx());
                state.preview.scroll = std::min(state.preview.scroll, max_scroll);
                if (state.preview.scroll > 0) {
                    --state.preview.scroll;
                }
                return true;
            }
            if (!state.preview.loading && IsDownEvent(event)) {
                const int max_scroll = PreviewMaxScroll(state, screen.dimx());
                if (state.preview.scroll < max_scroll) {
                    ++state.preview.scroll;
                } else {
                    state.preview.scroll = max_scroll;
                }
                return true;
            }
            return true;
        }

        if (state.loading) {
            if (event == Event::Escape) {
                load_cancel_requested.store(true);
                int pid = load_current_pid.load();
                if (pid > 0) {
                    kill(-pid, SIGTERM);
                }
                state.status = Tr(state.chinese, "正在取消目录读取...", "Cancelling directory load...");
            }
            return true;
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
        if (event == Event::O || event == Event::o) {
            state.sort_mode = state.sort_mode == SortMode::Name ? SortMode::Time : SortMode::Name;
            SortStateEntries(state);
            state.status = SortModeLabel(state);
            return true;
        }
        if (IsUpEvent(event)) {
            if (state.cursor > 0) {
                --state.cursor;
            }
            return true;
        }
        if (IsDownEvent(event)) {
            if (state.cursor + 1 < static_cast<int>(state.entries.size())) {
                ++state.cursor;
            }
            return true;
        }
        if (IsRightEvent(event)) {
            if (!state.entries.empty() && state.entries[state.cursor].type == EntryType::Directory) {
                StartLoadDirectory(state,
                                   adb,
                                   JoinRemotePath(state.current_path, state.entries[state.cursor].name),
                                   &screen,
                                   load_cancel_requested,
                                   load_current_pid,
                                   load_thread);
            }
            return true;
        }
        if (IsLeftEvent(event)) {
            if (state.current_path != "/") {
                StartLoadDirectory(state,
                                   adb,
                                   ParentRemotePath(state.current_path),
                                   &screen,
                                   load_cancel_requested,
                                   load_current_pid,
                                   load_thread);
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
        if (event == Event::T || event == Event::t) {
            StartDelete(state, adb, &screen, cancel_requested, current_pid, transfer_thread);
            return true;
        }
        if (event == Event::P || event == Event::p) {
            StartPreview(state, adb, &screen, preview_cancel_requested, preview_current_pid, preview_thread);
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
    load_cancel_requested.store(true);
    int load_pid = load_current_pid.load();
    if (load_pid > 0) {
        kill(-load_pid, SIGTERM);
    }
    preview_cancel_requested.store(true);
    int preview_pid = preview_current_pid.load();
    if (preview_pid > 0) {
        kill(-preview_pid, SIGTERM);
    }
    if (transfer_thread.joinable()) {
        transfer_thread.join();
    }
    if (load_thread.joinable()) {
        load_thread.join();
    }
    if (preview_thread.joinable()) {
        preview_thread.join();
    }
    return 0;
}
