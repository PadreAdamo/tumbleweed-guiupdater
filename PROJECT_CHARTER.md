# Project Charter — Tumbleweed GUI Updater

## Project Name
**Tumbleweed GUI Updater**

## Purpose
Tumbleweed GUI Updater is a **KDE-native system update orchestrator** for **openSUSE Tumbleweed**.

Its sole purpose is to keep a Tumbleweed system up to date **safely, predictably, and with minimal user interaction**, using **persistent policy** rather than per-update decision making.

This project is **not a package manager**.

## Target User
- openSUSE Tumbleweed users
- KDE Plasma users
- Users who want a simple, reliable way to keep their system current
- Users who do *not* want to manage packages manually

## In Scope
- Execute `zypper dup` as the authoritative system update mechanism
- Support dry-run previews and human-readable summaries
- Enforce persistent update policies (set once, reused every run)
- Optional Snapper pre/post snapshots
- Optional Flatpak updates
- Background update checks
- User notifications (updates available, attention required, reboot recommended)
- Optional scheduled execution using systemd user timers
- Logging and history for troubleshooting and rollback reference

## Explicitly Out of Scope
- Package search, install, or removal
- Manual dependency resolution
- Repository management
- Pattern selection
- Interactive solver dialogs
- Ad-hoc per-run option toggles
- Replacement of zypper, libzypp, YaST, or Myrlyn

If a feature resembles a package manager, it is out of scope.

## Design Principles
- **One primary action:** Update
- **Persistent behavior:** policy over prompts
- **Safe by default:** no silent destructive changes
- **Transparent:** clear summaries and logs
- **KDE-native:** Kirigami UI, KConfig, KAuth, KNotifications
- **Rolling-release aware:** designed specifically for Tumbleweed semantics

## Non-Goals
- Cross-distribution support
- GNOME support
- Windows/macOS parity
- App store functionality
