#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

int main() {
    auto document = ftxui::window(
        ftxui::text("adb-files-tui"),
        ftxui::text("Hello from FTXUI!")
    );

    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(document),
        ftxui::Dimension::Fit(document)
    );

    ftxui::Render(screen, document);
    screen.Print();

    return 0;
}
