# Tumbleweed GUI Updater

**A KDE-native system update orchestrator for openSUSE Tumbleweed**

![Status](https://img.shields.io/badge/status-alpha-orange)
![License](https://img.shields.io/badge/license-GPL--2.0-blue)

Tumbleweed GUI Updater keeps your rolling-release system current without requiring a terminal. It runs `zypper dup` on your behalf, streams the output live, takes Snapper snapshots before and after so you have a rollback point, and sits quietly in the system tray until it has something to tell you. It is not a package manager — it does one thing and tries to do it well.

---

## Features

- **One-click system updates** via `zypper dup` with live streaming output
- **System tray integration** using KStatusNotifierItem — Wayland-compatible, reflects update/ok/error state
- **Desktop notifications** via KNotification when updates become available
- **Automatic background checks** via a systemd user timer that runs even when the app is closed — configured through the app on first launch or toggled any time in Settings
- **Reboot detection** after kernel, glibc, or systemd updates, using a three-method cascade: `/run/reboot-required`, zypper output keywords, and a live `uname -r` vs installed kernel comparison
- **Snapper pre/post snapshots** bracketing every `zypper dup` run — snapshot numbers are shown in the status view so you always know your rollback point
- **Optional Flatpak updates** run after `zypper dup` with output streamed to the same live log
- **Persistent update history** — every check and apply is appended to a JSONL log at `~/.local/share/TumbleweedUpdater/history.log`, browsable in the History tab
- **Vendor change policy** — four modes control how `zypper dup` handles packages that would switch repositories or vendors; a pre-apply warning dialog lists affected packages so you always know what is changing before it happens
- **KDE-native settings page** built with Kirigami FormLayout — auto-check interval, Snapper toggle, Flatpak toggle, vendor policy, all persisted via KConfig
- **Privilege separation** — the GUI never runs as root; `zypper dup` and `systemctl reboot` are invoked via `pkexec`

---

## What this is

- A one-button system updater for KDE Plasma on Tumbleweed
- A safe wrapper around `zypper dup` with sane defaults
- A tray app that stays out of your way until it matters

## What this is not

- A package manager
- A replacement for zypper, YaST, libzypp, or Myrlyn
- A tool for installing, removing, or searching packages
- Cross-distribution or GNOME software

If a requested feature resembles a package manager, it is out of scope. See [PROJECT_CHARTER.md](PROJECT_CHARTER.md).

---

## Screenshots

<!-- Screenshots coming soon — add before submission to OBS/KDE review -->

---

## Building from source

### Dependencies

| Component | Packages |
|---|---|
| Build tools | `cmake >= 3.22`, `extra-cmake-modules` |
| Qt 6 | `qt6-base-devel`, `qt6-declarative-devel`, `qt6-quickcontrols2-devel` |
| KDE Frameworks 6 | `kf6-kirigami-devel`, `kf6-knotifications-devel`, `kf6-kstatusnotifieritem-devel`, `kf6-kconfig-devel` |
| Optional | `snapper` (for snapshot integration), `flatpak` (for Flatpak update support) |

Install build dependencies on openSUSE Tumbleweed:

```bash
sudo zypper install cmake extra-cmake-modules \
    qt6-base-devel qt6-declarative-devel qt6-quickcontrols2-devel \
    kf6-kirigami-devel kf6-knotifications-devel \
    kf6-kstatusnotifieritem-devel kf6-kconfig-devel
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

This installs two binaries and a KNotification config file:

```
/usr/bin/tumbleweed-updater           # GUI (run as your normal user)
/usr/bin/twu-ctl                      # Controller (invoked by the GUI)
/usr/share/knotifications6/TumbleweedUpdater.notifyrc
```

---

## Running

```bash
tumbleweed-updater
```

The app starts minimised to the system tray. Left-click the tray icon to show or hide the window. Right-click for Check Now, Show Window, and Quit.

**Requirements at runtime:**

- `twu-ctl` must be installed to `/usr/bin/twu-ctl` — the GUI looks for it there.
- Snapper integration requires Snapper to be installed and a `root` configuration to exist (`snapper list` should work without errors). If Snapper is absent the update proceeds normally; snapshots are silently skipped.
- Flatpak support requires `/usr/bin/flatpak` to be present and is off by default. Enable it in Settings.

---

## Configuration

All settings are persisted in `~/.config/TumbleweedUpdaterrc` and editable through the in-app Settings page (gear icon in the toolbar).

```ini
[AutoCheck]
Enabled=true
IntervalHours=24

[Snapper]
Enabled=true

[Flatpak]
Enabled=false

[VendorPolicy]
Mode=priority
```

The controller reads this file directly (it has no Qt dependency), so there is no separate config file to keep in sync.

---

## Background Update Checks

Background checks are configured automatically on first launch. If the systemd timer is not already enabled, the app will offer to enable it via a one-time dialog. You can enable or disable background checks in the Settings page at any time — no terminal required.

When enabled, `tumbleweed-updater-check.timer` runs `twu-ctl-notify` on a schedule (24 hours by default). The notifier sends a desktop notification when updates are available, and does nothing if the tray app is already running.

Changing the check interval in Settings writes a drop-in override to `~/.config/systemd/user/tumbleweed-updater-check.timer.d/interval.conf` and reloads the timer automatically.

---

### Vendor policy modes

| Mode | zypper flag | When to use |
|---|---|---|
| `priority` | *(none — zypper default)* | Standard Tumbleweed systems. Respects repository priorities as configured. |
| `opensuse` | `--from openSUSE` | Forces packages to come from the official openSUSE repository only. |
| `allow` | `--allow-vendor-change` | Systems with third-party repositories such as Packman where vendor switches are expected. |
| `deny` | `--no-allow-vendor-change` | Refuses any update that would switch a package's vendor; some packages may be left unupdated. |

Regardless of policy, if a status check detects that any packages would change vendor, the app shows a warning dialog listing the affected packages before the update proceeds. The dialog is informational — it never blocks the update, it only ensures you see what is changing.

---

## Architecture

The project is intentionally split into two binaries to enforce privilege separation.

**`tumbleweed-updater`** is the Qt6/Kirigami GUI. It runs as your normal user account for its entire lifetime. It communicates with the controller by launching it as a child process and reading its stdout. It never calls zypper directly and never elevates its own privileges.

**`twu-ctl`** is a standalone C++20 controller with no Qt dependency. For read-only operations (`status`) it runs as the current user. For mutating operations (`apply`) the GUI launches it under `pkexec`, which prompts for authentication via Polkit before granting root. The controller reads the vendor policy from `~/.config/TumbleweedUpdaterrc`, constructs the `zypper dup` command with the appropriate flags, streams zypper output line-by-line to stdout, then emits a single JSON object as the final line with the structured result (exit status, package count, reboot flag, snapshot numbers, vendor changes). The `status` command also runs `zypper dup --dry-run` with the same flags so the vendor change preview is consistent with what apply will actually do. The GUI parses this JSON to update the UI and append a history record.

This separation means a bug in the GUI cannot silently acquire root, and the controller can be audited, replaced, or tested without touching the UI layer.

---

## Contributing

Contributions are welcome. Before proposing a feature, read [PROJECT_CHARTER.md](PROJECT_CHARTER.md) — it defines scope clearly. The short version: if it resembles a package manager, it is out of scope.

The project follows [KDE coding style](https://community.kde.org/Policies/Coding_Style). C++ uses the KDE/Qt naming conventions; QML follows the Kirigami component patterns used in other KDE applications.

---

## License

[GPL 2.0](LICENSE)
