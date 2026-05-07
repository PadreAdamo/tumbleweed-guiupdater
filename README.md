# Tumbleweed GUI Updater

A KDE-native system update orchestrator for openSUSE Tumbleweed.

Provides a simple, one-button interface for keeping a Tumbleweed system current
using the recommended `zypper dup` workflow — with live output streaming, system
tray integration, desktop notifications, Snapper snapshot safety nets, and a
persistent history log.

## What this is

- A one-button system updater built on Kirigami / Qt6
- Safe defaults for a rolling release (zypper dup, not install)
- Optional automation, notifications, and Snapper integration
- A tray icon that stays out of your way until updates are available

## What this is not

- A package manager
- A replacement for zypper, YaST, or Myrlyn
- An application store

---

## Status

**Working alpha.** Core update, tray, and history features are functional.
Tested on openSUSE Tumbleweed with KDE Plasma 6.

---

## Features

| Feature | Status |
|---|---|
| Check for updates (`zypper lu`) | ✅ |
| Apply updates (`zypper dup`) via pkexec | ✅ |
| Live streaming output during apply | ✅ |
| System tray icon (KStatusNotifierItem, Wayland-safe) | ✅ |
| Tray icon state: ok / updates available / error | ✅ |
| Desktop notification on first update detection | ✅ |
| Close-to-tray (window hides, process stays running) | ✅ |
| Reboot detection (3-method cascade) + modal prompt | ✅ |
| Auto background checks (configurable interval) | ✅ |
| Battery-aware: skip auto-check on battery, retry in 30 min | ✅ |
| Package list preview dialog | ✅ |
| Snapper pre/post snapshot pair around zypper dup | ✅ |
| Persistent update history log (JSONL) | ✅ |
| History tab with reverse-chronological list | ✅ |
| "Check for updates on launch" toggle | ✅ |

---

## Architecture

Two binaries are built:

**`twu-ctl`** — unprivileged controller (C++20, no Qt dependency)
- Runs `zypper lu` for status checks (as the current user)
- Runs `zypper dup` for updates (via `pkexec`, as root)
- Creates Snapper pre/post snapshots around each apply
- Writes structured JSONL to `~/.local/share/TumbleweedUpdater/history.log`
- Emits a single JSON object on stdout; the GUI reads and parses it

**`tumbleweed-updater`** — Qt6/Kirigami GUI (unprivileged)
- Communicates with the controller via stdout/stdin (no D-Bus IPC)
- Never runs as root; privilege escalation is delegated to pkexec
- Persists settings via KConfig (`~/.config/TumbleweedUpdaterrc`)
- Reads history log from the XDG data directory

---

## Build

### Dependencies

| Package | Purpose |
|---|---|
| `cmake >= 3.22` | Build system |
| `extra-cmake-modules` | KDE CMake helpers |
| `qt6-base-devel` | Qt6 Core, Gui, Widgets, DBus |
| `qt6-declarative-devel` | Qt6 Qml, Quick |
| `qt6-quickcontrols2-devel` | Qt6 QuickControls2 |
| `kf6-kirigami-devel` | Kirigami UI framework |
| `kf6-knotifications-devel` | Desktop notifications |
| `kf6-kstatusnotifieritem-devel` | System tray |
| `kf6-kconfig-devel` | Settings persistence |
| `snapper` (optional) | Pre/post update snapshots |

Install on Tumbleweed:

```bash
sudo zypper install cmake extra-cmake-modules \
    qt6-base-devel qt6-declarative-devel qt6-quickcontrols2-devel \
    kf6-kirigami-devel kf6-knotifications-devel \
    kf6-kstatusnotifieritem-devel kf6-kconfig-devel
```

### Build steps

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Install

```bash
sudo cmake --install build
```

This installs:
- `/usr/bin/twu-ctl` — the controller
- `/usr/bin/tumbleweed-updater` — the GUI
- `/usr/share/knotifications6/TumbleweedUpdater.notifyrc` — notification config

---

## Running (from build directory)

```bash
# Install the controller to its expected path first, or set a custom path
# in src/gui/main.cpp:findTwuCtl()

./build/src/gui/tumbleweed-updater
```

---

## Configuration

### Auto-check interval

The background check interval is read from KConfig. Default is 4 hours.

To change to 2 hours:

```bash
kwriteconfig6 --file TumbleweedUpdaterrc --group AutoCheck --key IntervalHours 2
```

### Snapper snapshots

Snapshots are enabled by default if `snapper` is installed. To disable:

```bash
mkdir -p ~/.config/TumbleweedUpdater
echo "SnapperEnabled=false" > ~/.config/TumbleweedUpdater/controller.conf
```

### History log

The history log is written by the controller to:

```
~/.local/share/TumbleweedUpdater/history.log
```

Each line is a JSON object (JSONL format) and is human-readable with `cat` or
any text editor. The GUI History tab displays the 100 most recent entries.

---

## Files written

| Path | Written by | Contents |
|---|---|---|
| `~/.config/TumbleweedUpdaterrc` | GUI | KConfig settings (autocheck interval, launch toggle) |
| `~/.config/TumbleweedUpdater/settings.ini` | GUI | Qt Settings (autocheck on launch) |
| `~/.config/TumbleweedUpdater/controller.conf` | User | Optional controller overrides (SnapperEnabled) |
| `~/.local/share/TumbleweedUpdater/history.log` | Controller | JSONL update history |

---

## License

GPL 2.0
