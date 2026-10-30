# ClipboardHistory

Remembers everything you copy. Press **Ctrl+Shift+V** to search and paste older copies.

## Features
- Runs quietly in the **system tray**
- **Ctrl+Shift+V** opens a search popup next to your mouse
- Type to filter, arrow keys to choose, **Enter pastes** into the app you were using
- **Ctrl+P** pins an item (pinned items stay forever and show at the top)
- **Shift+Del** removes an item
- History is saved between restarts (`%APPDATA%\ClipboardHistory\history.dat`)
- Tray menu: Clear history, **Start with Windows**, Exit

> Only text is stored (not images/files). Keeps the last 300 items plus pinned ones.

## Download
Grab `ClipboardHistory.exe` from the [Releases](../../releases) page and run it. No install needed. Windows 10/11.

## Build from source
Double-click **`build.bat`**. It works with either:
- **MinGW-w64** (`winget install BrechtSanders.WinLibs.POSIX.UCRT`), or
- **Visual Studio** (run `build.bat` from the *Developer Command Prompt*).

Pure Win32 + C++17, no external dependencies.

## License
[MIT](LICENSE) © 2026 _.luarx
