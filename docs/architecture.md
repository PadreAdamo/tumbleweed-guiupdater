# Architecture Overview

Tumbleweed Updater is composed of three layers:

1. GUI (unprivileged)
   - Kirigami UI
   - Displays status, history, and logs
   - Stores persistent policy via KConfig

2. Controller (unprivileged)
   - Performs update checks and dry-runs
   - Applies policy decisions
   - Triggers privileged actions when required
   - Sends notifications

3. Privileged Helper (KAuth / polkit)
   - Executes `zypper dup`
   - Creates Snapper snapshots
   - Writes logs and returns structured results

The GUI never runs as root.
