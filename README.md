# adb-files-tui
a tui file manager by adb tools

## Build

FTXUI is required and can be installed with Homebrew:

```sh
brew install ftxui
```

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/adb-files-tui
```

The executable accepts two optional arguments:

```sh
./build/adb-files-tui [output-directory] [adb-device-serial]
```

- `output-directory`: directory used for exported files. If omitted, the current working directory is used.
- `adb-device-serial`: target adb device serial. If omitted, the first `adb devices` entry with state `device` is used.

## Controls

- `Up` / `Down`: move the cursor.
- `Right`: enter a directory.
- `Left`: return to the parent directory. The remote root `/` cannot go higher.
- `Space`: select or unselect the current file or directory.
- `E`: export selected files or directories.
- `I`: import one local file into the current remote directory.
- `L`: switch language between Chinese and English.
- `Esc`: exit on the home screen, cancel or close dialogs.
