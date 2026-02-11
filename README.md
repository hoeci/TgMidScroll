# TgMidScroll - Telegram Desktop Middle-Click Scroller

**TgMidScroll** is a lightweight Windows utility that adds middle-click auto-scrolling to [Telegram Desktop](https://github.com/telegramdesktop/tdesktop). It mimics the behavior found in web browsers like Chrome or Firefox.

<img src="https://github.com/user-attachments/assets/7a6e7807-dcc7-44aa-99e6-37248d806302" width="55%">

## Features
*   **Native Feel:** Smooth scrolling with acceleration logic.
*   **Visual Feedback:** Changes cursor to scroll anchors (North/South/Middle) just like a browser.
*   **Auto-Start:** Automatically runs on Windows startup after the first launch.
*   **Safe:** Only active when the Telegram window is under the mouse.
*   **Lightweight:** Uses minimal resources (written in C++ WinAPI).

## Usage
1.  Download `TgMidScroll.exe` from the [Releases](../../releases) page.
2.  Move the file to a permanent folder before running.
3.  Run the executable.
4.  Open Telegram and click the Middle Mouse Button (Wheel Click) to scroll.

### Hotkeys
(These work only when a Telegram window is in the foreground)
*   `Ctrl` + `F12`: **Toggle** the script On/Off.
*   `Ctrl` + `F11`: **Exit** the application completely.

### Uninstall
1.  Press `Ctrl` + `F11` while Telegram is focused to close the app.
2.  Delete `TgMidScroll.exe`.

## Building from Source

To compile it yourself, you will need a C++ compiler for Windows (such as **w64devkit** or **MinGW-w64**).

**1. Compile Resources:**
```bash
windres resources.rc -O coff -o resources.res
```

**2. Compile Application:**
```bash
g++ -o TgMidScroll.exe main.cpp resources.res -lpsapi -lgdi32 -lwinmm -static -mwindow
```
