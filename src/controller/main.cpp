// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Adam Girardo <adamjohngirardo@gmail.com>

#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cctype>

static std::string iso8601_now_utc()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static int run_command_capture(const std::string &cmd, std::string &output)
{
    std::array<char, 4096> buffer{};
    output.clear();

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int rc = pclose(pipe);
    return rc;
}

// Runs a program directly via fork/execvp -- no shell -- so argv elements
// (e.g. a URL parsed out of another program's output) can never be
// reinterpreted as shell syntax, unlike run_command_capture()'s popen.
static int run_argv_capture(const std::vector<std::string> &argv, std::string &output)
{
    output.clear();

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &a : argv)
            cargv.push_back(const_cast<char *>(a.c_str()));
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::array<char, 4096> buffer{};
    ssize_t n;
    while ((n = read(pipefd[0], buffer.data(), buffer.size())) > 0)
        output.append(buffer.data(), static_cast<size_t>(n));
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool contains_any(const std::string &hay, const std::initializer_list<const char*> needles)
{
    for (const char* n : needles) {
        if (hay.find(n) != std::string::npos) return true;
    }
    return false;
}

static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string trim_copy(const std::string &s)
{
    const size_t left = s.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) return "";
    const size_t right = s.find_last_not_of(" \t\r\n");
    return s.substr(left, right - left + 1);
}

static std::string build_package_preview(const std::string &output, int maxNames = 5)
{
    std::vector<std::string> names;

    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();

        std::string line = trim_copy(output.substr(start, end - start));

        if (line.rfind("v", 0) == 0) {
            std::vector<std::string> cols;
            size_t colStart = 0;
            while (colStart <= line.size()) {
                size_t pipePos = line.find('|', colStart);
                if (pipePos == std::string::npos) {
                    cols.push_back(line.substr(colStart));
                    break;
                }
                cols.push_back(line.substr(colStart, pipePos - colStart));
                colStart = pipePos + 1;
            }

            if (cols.size() >= 3) {
                std::string name = trim_copy(cols[2]);
                if (!name.empty())
                    names.push_back(name);
            }
        }

        start = end + 1;
    }

    if (names.empty())
        return "";

    std::string preview;
    const int count = static_cast<int>(names.size());
    const int shown = std::min(count, maxNames);

    for (int i = 0; i < shown; ++i) {
        if (i > 0)
            preview += ", ";
        preview += names[i];
    }

    if (count > shown)
        preview += "...";

    return preview;
}

static std::string build_package_list(const std::string &output)
{
    std::string result;

    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();

        std::string line = trim_copy(output.substr(start, end - start));

        if (line.rfind("v", 0) == 0) {
            std::vector<std::string> cols;
            size_t colStart = 0;
            while (colStart <= line.size()) {
                size_t pipePos = line.find('|', colStart);
                if (pipePos == std::string::npos) {
                    cols.push_back(line.substr(colStart));
                    break;
                }
                cols.push_back(line.substr(colStart, pipePos - colStart));
                colStart = pipePos + 1;
            }

            if (cols.size() >= 3) {
                std::string name = trim_copy(cols[2]);
                if (!name.empty()) {
                    if (!result.empty())
                        result += "\n";
                    result += name;
                }
            }
        }

        start = end + 1;
    }

    return result;
}

static bool compute_reboot_required(const std::string &packageList)
{
    return contains_any(packageList, {
        "kernel-default",
        "kernel-",
        "glibc",
        "systemd",
        "udev",
        "dracut",
        "microcode_ctl",
        "shim",
        "grub2"
    });
}

// ---- reboot detection after apply ----

static bool reboot_required_file()
{
    FILE *f = fopen("/run/reboot-required", "r");
    if (f) { fclose(f); return true; }
    return false;
}

static bool running_kernel_differs_from_installed()
{
    // uname -r gives e.g. "6.8.9-1-default"
    std::string running;
    {
        FILE *p = popen("uname -r 2>/dev/null", "r");
        if (!p) return false;
        std::array<char, 128> buf{};
        if (fgets(buf.data(), buf.size(), p))
            running = buf.data();
        pclose(p);
        while (!running.empty() &&
               (running.back() == '\n' || running.back() == '\r'))
            running.pop_back();
    }
    if (running.empty()) return false;

    // Strip the flavor suffix to get "version-release", e.g. "6.8.9-1"
    const size_t lastDash = running.rfind('-');
    if (lastDash == std::string::npos) return false;
    const std::string verRel = running.substr(0, lastDash);

    // rpm -q kernel-default returns "kernel-default-6.8.9-1.1.x86_64".
    // Search for "verRel." so "6.8.9-1." matches "6.8.9-1.1.x86_64" but not
    // a newer "6.9.0-2.1.x86_64".
    std::string installed;
    {
        FILE *p = popen("rpm -q kernel-default 2>/dev/null", "r");
        if (!p) return false;
        std::array<char, 512> buf{};
        while (fgets(buf.data(), buf.size(), p))
            installed += buf.data();
        pclose(p);
    }
    if (installed.empty() || installed.find("not installed") != std::string::npos)
        return false;

    return installed.find(verRel + ".") == std::string::npos;
}

static bool compute_apply_reboot_required(const std::string &zypperOutput)
{
    // Primary: distro marker file written by some zypper plugins
    if (reboot_required_file())
        return true;

    // Secondary: critical packages appear in the zypper dup output
    if (contains_any(zypperOutput, {
            "kernel-default", "kernel-",
            "glibc", "systemd", "dbus",
            "udev", "dracut",
        }))
        return true;

    // Tertiary: running kernel differs from the now-installed kernel package
    if (running_kernel_differs_from_installed())
        return true;

    return false;
}

// ---- Unified config reader ----
// Reads from ~/.config/TumbleweedUpdaterrc (KConfig INI format).
// When running as root via pkexec, get_user_home() resolves the invoking
// user's home so the correct config is read.

static std::string get_user_home()
{
    const char *pkexec_uid = std::getenv("PKEXEC_UID");
    if (pkexec_uid) {
        char *end = nullptr;
        const unsigned long uid = std::strtoul(pkexec_uid, &end, 10);
        if (end != pkexec_uid) {
            const struct passwd *pw = getpwuid(static_cast<uid_t>(uid));
            if (pw && pw->pw_dir) return std::string(pw->pw_dir);
        }
    }
    const char *home = std::getenv("HOME");
    return home ? std::string(home) : std::string{};
}

// Same PKEXEC_UID resolution as get_user_home(), but returns the invoking
// user's login name instead of their home directory.
static std::string get_invoking_username()
{
    const char *pkexec_uid = std::getenv("PKEXEC_UID");
    if (pkexec_uid) {
        char *end = nullptr;
        const unsigned long uid = std::strtoul(pkexec_uid, &end, 10);
        if (end != pkexec_uid) {
            const struct passwd *pw = getpwuid(static_cast<uid_t>(uid));
            if (pw && pw->pw_name) return std::string(pw->pw_name);
        }
    }
    const struct passwd *pw = getpwuid(getuid());
    return (pw && pw->pw_name) ? std::string(pw->pw_name) : std::string{};
}

static std::string get_kconfig_path()
{
    const std::string home = get_user_home();
    return home.empty() ? std::string{} : home + "/.config/TumbleweedUpdaterrc";
}

// Read a single key from a group in a KConfig-format INI file.
static std::string read_kconfig_entry(const std::string &path,
                                       const std::string &group,
                                       const std::string &key,
                                       const std::string &def = "")
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return def;

    char buf[512];
    bool in_group = false;
    std::string result = def;

    while (fgets(buf, sizeof(buf), f)) {
        const std::string line = trim_copy(std::string(buf));
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            const size_t close = line.find(']');
            in_group = (close != std::string::npos &&
                        line.substr(1, close - 1) == group);
            continue;
        }

        if (in_group) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos && trim_copy(line.substr(0, eq)) == key) {
                result = trim_copy(line.substr(eq + 1));
                break;
            }
        }
    }

    fclose(f);
    return result;
}

// ---- Unattended mode (opt-in passwordless polkit rule) ----
//
// "Unattended Updates" is an explicit, consent-gated setting: enabling it
// writes a polkit rules.d file that grants *only* the refresh/apply actions,
// for *this one local user*, no password prompt. It never touches the
// rollback action, which always requires interactive authentication.
// Disabling it removes that file, restoring the normal auth_admin prompt.

static bool valid_username(const std::string &u)
{
    if (u.empty() || u.size() > 32) return false;
    if (!(std::isalpha(static_cast<unsigned char>(u[0])) || u[0] == '_')) return false;
    for (char c : u) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    }
    return true;
}

// Escapes a string for embedding in a single-quoted JavaScript string literal.
static std::string js_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'";  break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string unattended_rule_path(const std::string &username)
{
    return "/etc/polkit-1/rules.d/90-tumbleweedupdater-unattended-" + username + ".rules";
}

// /etc/polkit-1/rules.d is mode 0750 root:polkitd on openSUSE (regular users
// can't even stat inside it), so the GUI can't tell if the rule file exists
// by looking at it directly. This marker lives in a world-traversable
// directory purely so the unprivileged GUI can reflect real on/off state;
// the rule file above is what actually grants the polkit exception.
static std::string unattended_marker_path(const std::string &username)
{
    return "/var/lib/tumbleweed-updater/unattended-" + username + ".enabled";
}

static int cmd_enable_unattended()
{
    const std::string username = get_invoking_username();

    if (!valid_username(username)) {
        std::cout << "{\"ok\":false,\"details\":\"Could not resolve a valid invoking username\"}\n";
        return 1;
    }

    const std::string path = unattended_rule_path(username);
    const std::string escapedUser = js_escape(username);

    FILE *f = fopen(path.c_str(), "w");
    if (!f) {
        std::cout << "{\"ok\":false,\"details\":\"Failed to write " << json_escape(path) << "\"}\n";
        return 1;
    }

    std::fprintf(f,
        "// SPDX-License-Identifier: GPL-2.0-only\n"
        "// Generated by Tumbleweed Updater's \"Unattended Updates\" setting.\n"
        "// Grants passwordless authorization for scheduled update checks to\n"
        "// run zypper without a prompt, scoped to this one local, active user.\n"
        "// Disable the setting in Tumbleweed Updater (or delete this file) to\n"
        "// require authentication again.\n"
        "polkit.addRule(function(action, subject) {\n"
        "    if ((action.id == \"org.padreadamo.tumbleweedupdater.refresh\" ||\n"
        "         action.id == \"org.padreadamo.tumbleweedupdater.apply\") &&\n"
        "        subject.user == '%s' &&\n"
        "        subject.local && subject.active) {\n"
        "        return polkit.Result.YES;\n"
        "    }\n"
        "});\n",
        escapedUser.c_str());
    fclose(f);

    ::chmod(path.c_str(), 0644);

    const std::string markerPath = unattended_marker_path(username);
    const std::string markerDir  = markerPath.substr(0, markerPath.rfind('/'));
    std::error_code ec;
    std::filesystem::create_directories(markerDir, ec);
    ::chmod(markerDir.c_str(), 0755);
    FILE *mf = fopen(markerPath.c_str(), "w");
    if (mf) {
        fclose(mf);
        ::chmod(markerPath.c_str(), 0644);
    }

    std::cout << "{\"ok\":true,\"path\":\"" << json_escape(path) << "\"}\n";
    return 0;
}

static int cmd_disable_unattended()
{
    const std::string username = get_invoking_username();

    if (!valid_username(username)) {
        std::cout << "{\"ok\":false,\"details\":\"Could not resolve a valid invoking username\"}\n";
        return 1;
    }

    const std::string path = unattended_rule_path(username);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        std::cout << "{\"ok\":false,\"details\":\"Failed to remove " << json_escape(path) << "\"}\n";
        return 1;
    }

    std::filesystem::remove(unattended_marker_path(username), ec);

    std::cout << "{\"ok\":true}\n";
    return 0;
}

// ---- Snapper integration ----

static bool read_snapper_enabled()
{
    // Primary: unified config [Snapper] Enabled
    const std::string kconf = get_kconfig_path();
    if (!kconf.empty()) {
        const std::string val = read_kconfig_entry(kconf, "Snapper", "Enabled", "");
        if (!val.empty()) return (val != "false");
    }

    // Fallback: old per-controller config (read-only, never written)
    const std::string home = get_user_home();
    if (!home.empty()) {
        const std::string old = home + "/.config/TumbleweedUpdater/controller.conf";
        FILE *f = fopen(old.c_str(), "r");
        if (f) {
            char buf[256];
            bool result = true;
            while (fgets(buf, sizeof(buf), f)) {
                const std::string line = trim_copy(std::string(buf));
                if (line.rfind("SnapperEnabled=", 0) == 0) {
                    result = (line.substr(15) != "false");
                    break;
                }
            }
            fclose(f);
            return result;
        }
    }

    return true;
}

static bool read_flatpak_enabled()
{
    const std::string kconf = get_kconfig_path();
    if (kconf.empty()) return false;
    return read_kconfig_entry(kconf, "Flatpak", "Enabled", "false") == "true";
}

// ---- Zypper GPG key handling ----
//
// When a repository rotates its signing key (a known recurrence with
// third-party repos like google-chrome), zypper refuses to refresh that
// repo's metadata and `zypper dup` aborts entirely rather than risk
// installing unverified packages. --gpg-auto-import-keys tells zypper to
// accept a newly-seen signing key automatically instead of failing; it does
// not weaken verification of packages against whichever key is current.
// Opt-in and off by default since it removes a manual trust checkpoint.

static bool read_gpg_auto_import_keys()
{
    const std::string kconf = get_kconfig_path();
    if (kconf.empty()) return false;
    return read_kconfig_entry(kconf, "Zypper", "AutoImportKeys", "false") == "true";
}

static std::string zypper_global_flags()
{
    return read_gpg_auto_import_keys() ? " --gpg-auto-import-keys" : "";
}

// ---- fwupd integration ----

static bool read_fwupd_enabled()
{
    const std::string kconf = get_kconfig_path();
    if (kconf.empty()) {
        // Default: enabled if fwupdmgr exists
        return std::filesystem::exists("/usr/bin/fwupdmgr");
    }
    const std::string val = read_kconfig_entry(kconf, "Fwupd", "Enabled", "");
    if (val.empty()) {
        // Key not set yet — default to true if installed
        return std::filesystem::exists("/usr/bin/fwupdmgr");
    }
    return val == "true";
}

// ---- Vendor change policy ----

struct VendorChange {
    std::string package;
    std::string fromVendor;
    std::string toVendor;
};

static std::string read_vendor_policy()
{
    const std::string kconf = get_kconfig_path();
    if (!kconf.empty()) {
        const std::string val = read_kconfig_entry(kconf, "VendorPolicy", "Mode", "priority");
        if (val == "opensuse" || val == "allow" || val == "deny" || val == "priority")
            return val;
    }
    return "priority";
}

// Build the extra flags to append to `zypper dup` for the given policy.
// NOTE: "opensuse" mode uses --from openSUSE, which assumes the repository alias
// is literally "openSUSE" — the default on fresh Tumbleweed installs. Systems
// that have renamed repositories will need this alias to be configurable; that
// is deferred to a future version.
static std::string dup_policy_flags(const std::string &policy)
{
    if (policy == "opensuse") return " --from openSUSE";
    if (policy == "allow")    return " --allow-vendor-change";
    if (policy == "deny")     return " --no-allow-vendor-change";
    return "";  // "priority" = zypper default, no extra flags
}

// Parse the "The following packages are going to change vendor:" section from
// zypper dup (--dry-run or real) output. Returns empty on any parse ambiguity
// so we never produce a false positive.
static std::vector<VendorChange> parse_vendor_changes(const std::string &output)
{
    std::vector<VendorChange> changes;
    bool in_vendor_section = false;

    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();

        const std::string line    = output.substr(start, end - start);
        const std::string trimmed = trim_copy(line);
        start = end + 1;

        if (trimmed.empty()) {
            in_vendor_section = false;
            continue;
        }

        if (contains_any(trimmed, {"change vendor", "vendor will change", "different vendor"})) {
            in_vendor_section = true;
            continue;
        }

        if (!in_vendor_section) continue;

        // Expected format: "  packagename  oldvendor -> newvendor"
        const size_t arrow = trimmed.find(" -> ");
        if (arrow == std::string::npos) continue;

        const std::string before   = trimmed.substr(0, arrow);
        const std::string toVendor = trim_copy(trimmed.substr(arrow + 4));

        // Package name ends at the first run of two-or-more spaces
        const size_t sep = before.find("  ");
        if (sep == std::string::npos) continue;

        VendorChange vc;
        vc.package    = trim_copy(before.substr(0, sep));
        vc.fromVendor = trim_copy(before.substr(sep));
        vc.toVendor   = toVendor;

        if (!vc.package.empty() && !vc.fromVendor.empty() && !vc.toVendor.empty())
            changes.push_back(vc);
    }

    return changes;
}

static std::string vendor_changes_json(const std::vector<VendorChange> &changes)
{
    std::string s = "[";
    for (size_t i = 0; i < changes.size(); ++i) {
        if (i > 0) s += ",";
        s += "{\"package\":\""    + json_escape(changes[i].package)    + "\","
              "\"fromVendor\":\"" + json_escape(changes[i].fromVendor) + "\","
              "\"toVendor\":\""   + json_escape(changes[i].toVendor)   + "\"}";
    }
    s += "]";
    return s;
}

static int create_pre_snapshot()
{
    if (!std::filesystem::exists("/usr/bin/snapper"))
        return -1;

    std::string output;
    const int rc = run_command_capture(
        "snapper --csvout create --type pre --print-number"
        " --description \"Tumbleweed Updater: pre-update\""
        " --cleanup-algorithm number 2>/dev/null",
        output);

    if (rc != 0) return -1;

    const std::string trimmed = trim_copy(output);
    if (trimmed.empty()) return -1;

    char *end = nullptr;
    const long n = std::strtol(trimmed.c_str(), &end, 10);
    return (end != trimmed.c_str() && n > 0) ? static_cast<int>(n) : -1;
}

static int create_post_snapshot(int pre_num)
{
    if (!std::filesystem::exists("/usr/bin/snapper"))
        return -1;

    const std::string cmd =
        "snapper --csvout create --type post --pre-number " +
        std::to_string(pre_num) +
        " --print-number"
        " --description \"Tumbleweed Updater: post-update\""
        " --cleanup-algorithm number 2>/dev/null";

    std::string output;
    const int rc = run_command_capture(cmd, output);

    if (rc != 0) return -1;

    const std::string trimmed = trim_copy(output);
    if (trimmed.empty()) return -1;

    char *end = nullptr;
    const long n = std::strtol(trimmed.c_str(), &end, 10);
    return (end != trimmed.c_str() && n > 0) ? static_cast<int>(n) : -1;
}

// ---- History log ----

// When running as root via pkexec, PKEXEC_UID holds the invoking user's UID.
// Use it to write history to the correct user's XDG data directory instead of
// /root/.local/share/, then chown the file and dir so the user can append later.
static std::string get_history_log_path()
{
    const std::string home = get_user_home();
    return home.empty() ? std::string{}
                        : home + "/.local/share/TumbleweedUpdater/history.log";
}

static void chown_to_invoking_user(const std::string &path)
{
    const char *s = std::getenv("PKEXEC_UID");
    if (!s) return;
    char *end = nullptr;
    const unsigned long uid = std::strtoul(s, &end, 10);
    if (end == s) return;
    const struct passwd *pw = getpwuid(static_cast<uid_t>(uid));
    if (pw) ::chown(path.c_str(), pw->pw_uid, pw->pw_gid);
}

static void append_history(const std::string &timestamp,
                            const std::string &operation,
                            bool ok,
                            int updateCount,
                            bool rebootRequired,
                            bool snapperUsed,
                            int snapshotPre,
                            int snapshotPost,
                            const std::string &details)
{
    const std::string path = get_history_log_path();
    if (path.empty()) return;

    const std::string dir = path.substr(0, path.rfind('/'));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    chown_to_invoking_user(dir);

    FILE *f = fopen(path.c_str(), "a");
    if (!f) return;

    std::fprintf(f,
        "{\"timestamp\":\"%s\",\"operation\":\"%s\","
        "\"ok\":%s,\"updateCount\":%d,\"rebootRequired\":%s,"
        "\"snapperUsed\":%s,\"snapshotPre\":%d,\"snapshotPost\":%d,"
        "\"details\":\"%s\"}\n",
        json_escape(timestamp).c_str(), operation.c_str(),
        ok ? "true" : "false", updateCount,
        rebootRequired ? "true" : "false",
        snapperUsed ? "true" : "false",
        snapshotPre, snapshotPost,
        json_escape(details).c_str());

    fclose(f);
    chown_to_invoking_user(path);
}

// Shared with twu-ctl-notify, which writes the same file with
// source="systemd-timer" after a background check. Whichever of the two
// ran most recently wins, so the GUI can show an accurate "last checked"
// time regardless of who performed the check.
static void write_last_check_state(const std::string &timestamp,
                                    bool updatesAvailable,
                                    int updateCount,
                                    const std::string &source)
{
    const std::string home = get_user_home();
    if (home.empty()) return;

    const std::string dir = home + "/.local/share/TumbleweedUpdater";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::ofstream f(dir + "/last-check-state.json", std::ios::trunc);
    if (!f.is_open()) return;

    f << "{"
      << "\"timestamp\":\"" << json_escape(timestamp) << "\","
      << "\"updatesAvailable\":" << (updatesAvailable ? "true" : "false") << ","
      << "\"updateCount\":" << updateCount << ","
      << "\"source\":\"" << json_escape(source) << "\""
      << "}\n";
}

// popen strips the TTY so flatpak outputs spaces instead of tabs.
// Format: " N.   APP_ID   BRANCH   OP   REMOTE   SIZE\n"
static int checkFlatpakUpdates(std::string &flatpakList)
{
    flatpakList.clear();

    if (!std::filesystem::exists("/usr/bin/flatpak"))
        return 0;

    // List updates — pipe 'n' to answer the confirmation prompt
    std::string output;
    run_command_capture("/bin/sh -c 'echo n | flatpak update 2>&1'", output);

    std::vector<std::string> apps;

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Strip leading whitespace
        size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos)
            continue;

        // Must start with a digit
        if (!std::isdigit(static_cast<unsigned char>(line[firstNonSpace])))
            continue;

        // Find the dot after the number: "1." "2." etc.
        size_t dotPos = line.find('.', firstNonSpace);
        if (dotPos == std::string::npos || dotPos > firstNonSpace + 3)
            continue;

        // Skip all whitespace (spaces or tabs) after the dot to reach the app ID
        size_t appStart = dotPos + 1;
        while (appStart < line.size() &&
               (line[appStart] == ' ' || line[appStart] == '\t'))
            appStart++;

        if (appStart >= line.size())
            continue;

        // App ID ends at the next whitespace
        size_t appEnd = line.find_first_of(" \t", appStart);
        std::string appId = appEnd != std::string::npos
            ? line.substr(appStart, appEnd - appStart)
            : line.substr(appStart);

        // Validate reverse-domain format — must contain a dot
        if (appId.empty() || appId.find('.') == std::string::npos)
            continue;

        // Exclude known non-app tokens
        if (appId == "ID" || appId == "Proceed")
            continue;

        apps.push_back(appId);
    }

    for (size_t i = 0; i < apps.size(); i++) {
        if (i > 0) flatpakList += "\n";
        flatpakList += apps[i];
    }

    return static_cast<int>(apps.size());
}

struct FwupdDevice {
    std::string name;
    std::string currentVersion;
    std::string newVersion;
};

static int checkFwupdUpdates(std::vector<FwupdDevice> &devices)
{
    devices.clear();

    if (!std::filesystem::exists("/usr/bin/fwupdmgr"))
        return 0;

    std::string output;
    // --no-unreported-check suppresses the reporting nag
    // --no-metadata-check suppresses metadata age warnings
    const int rc = run_command_capture(
        "fwupdmgr get-updates --no-unreported-check "
        "--no-metadata-check 2>&1", output);

    // rc=2 means "no updates available" on some fwupd versions
    // rc=0 means updates found
    // anything else is an error
    if (rc != 0 && rc != 2 &&
        output.find("No updatable devices") == std::string::npos &&
        output.find("No updates available") == std::string::npos) {
        // Genuine error — return empty, don't fail the whole status check
        std::cerr << "[twu-ctl] fwupd check failed (rc=" << rc << ")\n";
        return 0;
    }

    // Definitive "nothing to do" markers — short-circuit before parsing.
    // Without this, popen's non-TTY output can include a leading progress
    // line (e.g. "Idle…") that would otherwise be misread as a device name.
    if (output.find("No updatable devices") != std::string::npos ||
        output.find("No updates available") != std::string::npos) {
        return 0;
    }

    // Parse output for device update entries. fwupdmgr get-updates groups
    // devices under section headers:
    //   Devices with the following firmware updates available:
    //     DeviceName (guid)
    //       Current version: X.Y.Z
    //       New version:     A.B.C
    //       Summary:         ...
    //   Devices with no available firmware updates:
    //     • Some Device
    //   Devices with the latest available firmware version:
    //     • Some Device
    // We only collect devices from the "available" section. This also
    // ignores any preamble progress text (e.g. a plain "Idle…" line
    // fwupdmgr prints when stdout isn't a TTY) that would otherwise be
    // misread as a device name.
    FwupdDevice current;
    bool inAvailableSection = false;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.find("Devices with") != std::string::npos) {
            if (!current.name.empty()) {
                devices.push_back(current);
                current = FwupdDevice{};
            }
            inAvailableSection =
                line.find("no available") == std::string::npos &&
                line.find("latest available") == std::string::npos;
            continue;
        }

        if (!inAvailableSection)
            continue;

        // Lines starting without leading whitespace begin a new device entry.
        if (!line.empty() && line[0] != ' ' && line[0] != '\t') {
            if (!current.name.empty()) {
                devices.push_back(current);
                current = FwupdDevice{};
            }
            // Strip trailing " (guid)" if present
            size_t parenPos = line.rfind(" (");
            current.name = parenPos != std::string::npos
                ? line.substr(0, parenPos)
                : line;
            continue;
        }

        // Parse version lines
        const std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Current version:", 0) == 0) {
            current.currentVersion = trim_copy(trimmed.substr(16));
        }
        if (trimmed.rfind("New version:", 0) == 0) {
            current.newVersion = trim_copy(trimmed.substr(12));
        }
    }

    // Save last device
    if (!current.name.empty())
        devices.push_back(current);

    return static_cast<int>(devices.size());
}

static int cmd_status()
{
    const std::string timestamp    = iso8601_now_utc();
    const std::string vendorPolicy = read_vendor_policy();
    const std::string zypperCmd    = "zypper -n" + zypper_global_flags() + " lu 2>&1";

    std::string output;
    const int rc = run_command_capture(zypperCmd, output);

    bool updatesAvailable = false;
    bool ok        = true;
    bool needsAuth = false;
    bool rebootRequired = false;
    int  updateCount = 0;
    std::string summary;
    std::string details;
    std::string packagePreview;
    std::string packageList;

    const bool authish = contains_any(output, {
        "Root privileges are required",
        "root privileges",
        "requires root",
        "permission denied",
        "Permission denied",
        "System management is locked",
        "is locked by the application",
        "Another instance of zypper is running",
        "You must be root"
    });

    if (authish) {
        ok = false;
        needsAuth = true;
        summary = "Permission needed";
        details =
            "Unable to check for updates without administrator rights.\n"
            "This system requires elevated privileges for zypper operations.";
    } else if (rc != 0) {
        ok = false;
        summary = "Status check failed";
        details = output;
    } else if (contains_any(output, { "No updates found", "No updates needed", "No updates." })) {
        updatesAvailable = false;
        updateCount = 0;
        rebootRequired = false;
        summary = "System up to date";
        details = "No updates listed by zypper.";
    } else {
        size_t start = 0;
        while (start < output.size()) {
            size_t end = output.find('\n', start);
            if (end == std::string::npos)
                end = output.size();

            std::string line = trim_copy(output.substr(start, end - start));
            if (line.rfind("v", 0) == 0)
                updateCount++;

            start = end + 1;
        }

        packagePreview = build_package_preview(output, 5);
        packageList    = build_package_list(output);
        rebootRequired = compute_reboot_required(packageList);
        updatesAvailable = (updateCount > 0);
        summary = "Updates available";

        if (rebootRequired) {
            details =
                "zypper lists updates.\n"
                "A reboot will likely be required after applying them.";
        } else {
            details =
                "zypper lists updates.\n"
                "Administrator privileges will be required to apply them.";
        }
    }

    // Vendor change detection: run zypper dup --dry-run with the configured policy
    // flags so the preview matches exactly what apply will do.
    // Only executed when updates are available; skipped on auth/error to avoid
    // false positives.
    std::vector<VendorChange> vendorChanges;
    bool vendorChangeDetected = false;

    if (ok && updatesAvailable) {
        std::string dupOut;
        const std::string dupCmd =
            "zypper -n" + zypper_global_flags() + " dup --dry-run" +
            dup_policy_flags(vendorPolicy) + " 2>&1";
        run_command_capture(dupCmd, dupOut);
        vendorChanges = parse_vendor_changes(dupOut);
        vendorChangeDetected = !vendorChanges.empty();
    }

    // Flatpak check — always runs regardless of zypper result
    const bool flatpakEnabled = read_flatpak_enabled();
    std::string flatpakList;
    int flatpakCount = 0;
    if (flatpakEnabled) {
        flatpakCount = checkFlatpakUpdates(flatpakList);
    }
    const bool flatpakUpdatesAvailable = flatpakCount > 0;

    // === Phase 3: fwupd firmware check ===
    std::vector<FwupdDevice> fwupdDevices;
    int fwupdCount = 0;
    bool fwupdEnabled = read_fwupd_enabled();

    if (fwupdEnabled && std::filesystem::exists("/usr/bin/fwupdmgr")) {
        fwupdCount = checkFwupdUpdates(fwupdDevices);
    }

    const bool fwupdUpdatesAvailable = fwupdCount > 0;

    std::string fwupdList;
    for (size_t i = 0; i < fwupdDevices.size(); i++) {
        if (i > 0) fwupdList += "\n";
        fwupdList += fwupdDevices[i].name;
        if (!fwupdDevices[i].currentVersion.empty() &&
            !fwupdDevices[i].newVersion.empty()) {
            fwupdList += " (" + fwupdDevices[i].currentVersion +
                         " \xE2\x86\x92 " + fwupdDevices[i].newVersion + ")";
        }
    }

    const bool anyUpdatesAvailable = updatesAvailable || flatpakUpdatesAvailable || fwupdUpdatesAvailable;
    const int  totalUpdateCount    = updateCount + flatpakCount + fwupdCount;

    // Build the summary from whichever categories have updates. Handles all
    // combinations of system/Flatpak/firmware updates cleanly.
    if (flatpakUpdatesAvailable || fwupdUpdatesAvailable) {
        std::vector<std::string> parts;
        if (updatesAvailable)
            parts.push_back(std::to_string(updateCount) + " system");
        if (flatpakUpdatesAvailable)
            parts.push_back(std::to_string(flatpakCount) + " Flatpak");
        if (fwupdUpdatesAvailable)
            parts.push_back(std::to_string(fwupdCount) + " firmware");

        summary.clear();
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) summary += " + ";
            summary += parts[i];
        }
        summary += " update" + std::string(totalUpdateCount == 1 ? "" : "s") + " available";

        if (!updatesAvailable)
            details = "Updates are available outside of zypper.";
    }

    std::cout
        << "{"
        << "\"ok\":"                   << (ok ? "true" : "false") << ","
        << "\"summary\":\""            << json_escape(summary) << "\","
        << "\"details\":\""            << json_escape(details) << "\","
        << "\"packagePreview\":\""     << json_escape(packagePreview) << "\","
        << "\"packageList\":\""        << json_escape(packageList) << "\","
        << "\"updateCount\":"          << totalUpdateCount << ","
        << "\"updatesAvailable\":"     << (anyUpdatesAvailable ? "true" : "false") << ","
        << "\"rebootRequired\":"       << (rebootRequired ? "true" : "false") << ","
        << "\"needsAuth\":"            << (needsAuth ? "true" : "false") << ","
        << "\"vendorPolicy\":\""       << json_escape(vendorPolicy) << "\","
        << "\"vendorChangeDetected\":" << (vendorChangeDetected ? "true" : "false") << ","
        << "\"vendorChangeCount\":"    << static_cast<int>(vendorChanges.size()) << ","
        << "\"vendorChanges\":"        << vendor_changes_json(vendorChanges) << ","
        << "\"flatpakUpdatesAvailable\":" << (flatpakUpdatesAvailable ? "true" : "false") << ","
        << "\"flatpakUpdateCount\":"   << flatpakCount << ","
        << "\"flatpakList\":\""        << json_escape(flatpakList) << "\","
        << "\"fwupdUpdatesAvailable\":" << (fwupdUpdatesAvailable ? "true" : "false") << ","
        << "\"fwupdUpdateCount\":"    << fwupdCount << ","
        << "\"fwupdList\":\""         << json_escape(fwupdList) << "\","
        << "\"timestamp\":\""          << json_escape(timestamp) << "\""
        << "}\n";

    // Only log status checks that found something noteworthy.
    // Routine "up to date" checks are not recorded to avoid log spam.
    const bool worthLogging = anyUpdatesAvailable || !ok || needsAuth;
    if (worthLogging)
        append_history(timestamp, "status", ok, totalUpdateCount, rebootRequired,
                       false, -1, -1, summary);

    if (ok)
        write_last_check_state(timestamp, anyUpdatesAvailable, totalUpdateCount, "gui");

    return ok ? 0 : 1;
}

static int cmd_refresh()
{
    std::string output;
    const int rc = run_command_capture(
        "zypper -n" + zypper_global_flags() + " ref 2>&1", output);

    const bool ok = (rc == 0);
    std::cout
        << "{"
        << "\"ok\":"       << (ok ? "true" : "false") << ","
        << "\"details\":\"" << json_escape(output) << "\""
        << "}\n";
    std::cout.flush();

    return ok ? 0 : 1;
}

// ---- Repository refresh/signature failure detection ----
//
// `zypper dup` aborts outright if any enabled repository fails to refresh
// (most commonly a rotated signing key causing repomd.xml signature
// verification to fail). Surfacing which repo broke -- instead of just
// dumping the raw zypper transcript -- lets the GUI show something
// actionable instead of an opaque wall of text.

static bool is_repo_error(const std::string &output)
{
    return contains_any(output, {
        "Signature verification failed",
        "GPG check FAILED",
        "have not been refreshed because of an error",
        "Failed to retrieve new repository metadata"
    });
}

static std::vector<std::string> extract_broken_repos(const std::string &output)
{
    std::vector<std::string> repos;
    for (const char *marker : {"Repository '", "repository '"}) {
        const size_t markerLen = std::strlen(marker);
        size_t pos = 0;
        while ((pos = output.find(marker, pos)) != std::string::npos) {
            const size_t start = pos + markerLen;
            const size_t end = output.find('\'', start);
            if (end == std::string::npos) break;
            const std::string name = output.substr(start, end - start);
            if (!name.empty() &&
                std::find(repos.begin(), repos.end(), name) == repos.end())
                repos.push_back(name);
            pos = end + 1;
        }
    }
    return repos;
}

static std::string repo_error_details(const std::vector<std::string> &repos)
{
    std::string details;
    if (repos.empty()) {
        details = "A repository failed to refresh (signature verification failed).";
    } else {
        details = "Signature verification failed for: ";
        for (size_t i = 0; i < repos.size(); ++i) {
            if (i > 0) details += ", ";
            details += "'" + repos[i] + "'";
        }
        details += ".";
    }
    details += " The repository's signing key has likely changed. Import its "
               "current key and retry, or fix/disable the repository yourself.";
    return details;
}

static std::string string_array_json(const std::vector<std::string> &items)
{
    std::string s = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) s += ",";
        s += "\"" + json_escape(items[i]) + "\"";
    }
    s += "]";
    return s;
}

// zypper prints the repo's own configured gpgkey= URL(s) while looking for a
// key to verify against, e.g.:
//   Looking for gpg keys in repository google-chrome.
//     gpgkey=https://dl.google.com/linux/linux_signing_key.pub
// That URL comes from the repo's local .repo file (writable only by root),
// not from the network, so re-surfacing it to offer a one-click re-import is
// safe -- it's not attacker-controlled the way the repomd.xml content is.
struct RepoKeyInfo {
    std::string repo;
    std::string keyUrl;
};

static std::vector<RepoKeyInfo> extract_repo_gpg_keys(const std::string &output)
{
    std::vector<RepoKeyInfo> result;
    std::string currentRepo;

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = trim_copy(line);

        static const std::string repoMarker = "Looking for gpg keys in repository ";
        const size_t repoPos = trimmed.find(repoMarker);
        if (repoPos != std::string::npos) {
            currentRepo = trimmed.substr(repoPos + repoMarker.size());
            if (!currentRepo.empty() && currentRepo.back() == '.')
                currentRepo.pop_back();
            continue;
        }

        static const std::string keyMarker = "gpgkey=";
        const size_t keyPos = trimmed.find(keyMarker);
        if (keyPos != std::string::npos && !currentRepo.empty()) {
            const std::string url = trim_copy(trimmed.substr(keyPos + keyMarker.size()));
            if (!url.empty())
                result.push_back({currentRepo, url});
        }
    }
    return result;
}

// Only https URLs are eligible for the one-click "Import Key & Retry" path;
// anything else falls back to manual instructions.
static bool valid_https_url(const std::string &url)
{
    if (url.rfind("https://", 0) != 0) return false;
    if (url.size() > 2048) return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

// Disables a repository for the duration of an apply, then always
// re-enables it on scope exit -- including the early-return "failed to
// start zypper" path in cmd_apply() below. Used by the "Skip Repo & Update"
// action: when a repo's metadata can't be verified (e.g. an upstream
// signing-key mismatch with no correct key published anywhere to import),
// disabling it is the only way to let `zypper dup` proceed at all, since dup
// refuses to run if any enabled repository fails to refresh. Best-effort:
// failures to disable/re-enable are not treated as fatal, since worst case
// the apply just fails the same way it would have without this.
class RepoDisableGuard {
public:
    explicit RepoDisableGuard(const std::string &alias) : alias_(alias)
    {
        if (!alias_.empty()) {
            std::string out;
            run_argv_capture({"zypper", "-n", "mr", "-d", alias_}, out);
        }
    }

    ~RepoDisableGuard()
    {
        if (!alias_.empty()) {
            std::string out;
            run_argv_capture({"zypper", "-n", "mr", "-e", alias_}, out);
        }
    }

    RepoDisableGuard(const RepoDisableGuard &) = delete;
    RepoDisableGuard &operator=(const RepoDisableGuard &) = delete;

private:
    std::string alias_;
};

static int cmd_apply(const std::string &skipRepoAlias = "")
{
    // No-op when skipRepoAlias is empty. Disables now, re-enables on every
    // return path below via the destructor.
    RepoDisableGuard repoGuard(skipRepoAlias);

    const std::string timestamp    = iso8601_now_utc();
    const std::string vendorPolicy = read_vendor_policy();
    const bool snapperEnabled      = read_snapper_enabled();

    int snapshotPre  = -1;
    int snapshotPost = -1;
    bool snapperUsed = false;

    if (snapperEnabled) {
        snapshotPre = create_pre_snapshot();
        if (snapshotPre < 0)
            fprintf(stderr, "[snapper] pre-snapshot failed or snapper unavailable — continuing without snapshot\n");
    }

    const std::string zypperCmd =
        "zypper -n" + zypper_global_flags() + " dup" +
        dup_policy_flags(vendorPolicy) + " 2>&1";

    std::string output;
    bool ok = false;

    FILE *pipe = popen(zypperCmd.c_str(), "r");
    if (!pipe) {
        // Still attempt post-snapshot so the pair is balanced if pre succeeded
        if (snapperEnabled && snapshotPre >= 0) {
            snapshotPost = create_post_snapshot(snapshotPre);
            snapperUsed  = (snapshotPost >= 0);
        }
        std::cout
            << "{"
            << "\"ok\":false,"
            << "\"summary\":\"Failed to start zypper\","
            << "\"details\":\"popen failed\","
            << "\"packagePreview\":\"\","
            << "\"packageList\":\"\","
            << "\"updateCount\":0,"
            << "\"updatesAvailable\":false,"
            << "\"rebootRequired\":false,"
            << "\"needsAuth\":false,"
            << "\"snapshotPre\":"          << snapshotPre << ","
            << "\"snapshotPost\":"         << snapshotPost << ","
            << "\"snapperUsed\":"          << (snapperUsed ? "true" : "false") << ","
            << "\"vendorPolicy\":\""       << json_escape(vendorPolicy) << "\","
            << "\"vendorChangeDetected\":false,"
            << "\"vendorChangeCount\":0,"
            << "\"vendorChanges\":[],"
            << "\"timestamp\":\""          << json_escape(timestamp) << "\""
            << "}\n";
        std::cout.flush();
        append_history(timestamp, "apply", false, 0, false,
                       snapperUsed, snapshotPre, snapshotPost, "Failed to start zypper");
        return 1;
    }

    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        const std::string chunk = buf.data();
        output += chunk;
        std::cout << chunk;
        std::cout.flush();
    }

    const int rc = pclose(pipe);
    ok = (rc == 0);

    if (snapperEnabled && snapshotPre >= 0) {
        snapshotPost = create_post_snapshot(snapshotPre);
        if (snapshotPost < 0)
            fprintf(stderr, "[snapper] post-snapshot failed\n");
        else
            snapperUsed = true;
    }

    const bool needsAuth = !ok && contains_any(output, {
        "Root privileges are required",
        "root privileges",
        "requires root",
        "Permission denied",
        "permission denied",
        "You must be root"
    });

    const bool repoError = !ok && !needsAuth && is_repo_error(output);
    const std::vector<std::string> brokenRepos =
        repoError ? extract_broken_repos(output) : std::vector<std::string>{};

    // Best-effort: the https key URL for whichever broken repo zypper
    // printed one for, so the GUI can offer a one-click re-import.
    std::string repoKeyUrl;
    if (repoError) {
        const std::vector<RepoKeyInfo> keys = extract_repo_gpg_keys(output);
        for (const auto &k : keys) {
            const bool matchesBrokenRepo =
                std::find(brokenRepos.begin(), brokenRepos.end(), k.repo) != brokenRepos.end();
            if ((matchesBrokenRepo || brokenRepos.empty()) && valid_https_url(k.keyUrl)) {
                repoKeyUrl = k.keyUrl;
                break;
            }
        }
    }

    bool rebootRequired = ok && compute_apply_reboot_required(output);

    // Parse any vendor changes that actually occurred during this update
    const std::vector<VendorChange> vendorChanges = parse_vendor_changes(output);
    const bool vendorChangeDetected = !vendorChanges.empty();
    const int  vendorChangeCount    = static_cast<int>(vendorChanges.size());

    // ---- Optional Flatpak update ----
    bool flatpakUpdated = false;
    int  flatpakUpdateCount = 0;
    if (ok && read_flatpak_enabled() && std::filesystem::exists("/usr/bin/flatpak")) {
        std::string flatpakListApply;
        flatpakUpdateCount = checkFlatpakUpdates(flatpakListApply);

        std::cout << "\n--- Updating Flatpak packages ---\n";
        std::cout.flush();

        FILE *fpipe = popen("flatpak update -y 2>&1", "r");
        if (fpipe) {
            std::array<char, 4096> fbuf{};
            while (fgets(fbuf.data(), static_cast<int>(fbuf.size()), fpipe) != nullptr) {
                const std::string chunk = fbuf.data();
                output += chunk;
                std::cout << chunk;
                std::cout.flush();
            }
            flatpakUpdated = (pclose(fpipe) == 0);
        }
    }

    // ---- Optional fwupd firmware update ----
    // Bundled into the main apply flow (never a separate confirmation step)
    // because firmware devices are enumerated and gated by the same
    // [Fwupd] Enabled setting used for status detection.
    bool fwupdApplied = false;
    int  fwupdUpdateCount = 0;
    if (ok && read_fwupd_enabled() && std::filesystem::exists("/usr/bin/fwupdmgr")) {
        std::vector<FwupdDevice> fwupdDevicesApply;
        fwupdUpdateCount = checkFwupdUpdates(fwupdDevicesApply);

        if (fwupdUpdateCount > 0) {
            std::cout << "\n--- Updating firmware ---\n";
            std::cout.flush();

            FILE *fwpipe = popen("fwupdmgr update --no-unreported-check 2>&1", "r");
            if (fwpipe) {
                std::array<char, 4096> fwbuf{};
                while (fgets(fwbuf.data(), static_cast<int>(fwbuf.size()), fwpipe) != nullptr) {
                    const std::string chunk = fwbuf.data();
                    output += chunk;
                    std::cout << chunk;
                    std::cout.flush();
                }
                fwupdApplied = (pclose(fwpipe) == 0);
            }

            // Firmware updates always require a reboot to take effect.
            if (fwupdApplied)
                rebootRequired = true;
        }
    }

    std::string summary = ok         ? "Update complete"
                        : needsAuth  ? "Permission denied"
                        : repoError  ? "Repository problem"
                                     : "Update failed";
    if (ok && vendorChangeDetected)
        summary += " (" + std::to_string(vendorChangeCount) + " vendor change(s))";
    if (!skipRepoAlias.empty())
        summary += " (skipped '" + skipRepoAlias + "', re-enabled)";

    const std::string applyDetails =
        repoError ? repo_error_details(brokenRepos) : std::string();

    // Ensure the JSON result starts on its own line
    if (!output.empty() && output.back() != '\n')
        std::cout << '\n';

    std::cout
        << "{"
        << "\"ok\":"                   << (ok ? "true" : "false") << ","
        << "\"summary\":\""            << json_escape(summary) << "\","
        << "\"details\":\""            << json_escape(applyDetails) << "\","
        << "\"repoError\":"            << (repoError ? "true" : "false") << ","
        << "\"brokenRepos\":"          << string_array_json(brokenRepos) << ","
        << "\"repoKeyUrl\":\""         << json_escape(repoKeyUrl) << "\","
        << "\"packagePreview\":\"\","
        << "\"packageList\":\"\","
        << "\"updateCount\":0,"
        << "\"updatesAvailable\":false,"
        << "\"rebootRequired\":"       << (rebootRequired ? "true" : "false") << ","
        << "\"needsAuth\":"            << (needsAuth ? "true" : "false") << ","
        << "\"snapshotPre\":"          << snapshotPre << ","
        << "\"snapshotPost\":"         << snapshotPost << ","
        << "\"snapperUsed\":"          << (snapperUsed ? "true" : "false") << ","
        << "\"flatpakApplied\":"       << (flatpakUpdated ? "true" : "false") << ","
        << "\"flatpakUpdateCount\":"   << flatpakUpdateCount << ","
        << "\"fwupdApplied\":"        << (fwupdApplied ? "true" : "false") << ","
        << "\"fwupdUpdateCount\":"    << fwupdUpdateCount << ","
        << "\"vendorPolicy\":\""       << json_escape(vendorPolicy) << "\","
        << "\"vendorChangeDetected\":" << (vendorChangeDetected ? "true" : "false") << ","
        << "\"vendorChangeCount\":"    << vendorChangeCount << ","
        << "\"vendorChanges\":"        << vendor_changes_json(vendorChanges) << ","
        << "\"timestamp\":\""          << json_escape(timestamp) << "\""
        << "}\n";
    std::cout.flush();

    append_history(timestamp, "apply", ok, 0, rebootRequired,
                   snapperUsed, snapshotPre, snapshotPost, summary);
    return ok ? 0 : 1;
}

// Imports a repository's current GPG signing key into the RPM keyring, so a
// subsequent zypper run verifies against it. Used by the GUI's "Import Key &
// Retry" action after a repository signature-verification failure -- e.g. a
// vendor (Google's google-chrome repo has done this) rotating their key.
// Deliberately run via fork/execvp (run_argv_capture), not a shell, since
// the URL comes from parsed program output rather than a fixed string.
static int cmd_import_repo_key(const std::string &url)
{
    if (!valid_https_url(url)) {
        std::cout << "{\"ok\":false,\"details\":\"Refusing to import: "
                      "key URL must be a plain https:// URL\"}\n";
        return 1;
    }

    std::string output;
    const int rc = run_argv_capture({"rpm", "--import", url}, output);
    const bool ok = (rc == 0);

    std::cout
        << "{"
        << "\"ok\":"      << (ok ? "true" : "false") << ","
        << "\"details\":\"" << json_escape(trim_copy(output)) << "\""
        << "}\n";
    std::cout.flush();

    return ok ? 0 : 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cout <<
            "Usage: twu-ctl <command>\n"
            "\n"
            "Commands:\n"
            "  status               Check for available updates\n"
            "  apply                Apply pending updates (requires root via pkexec)\n"
            "  refresh              Refresh repository metadata (requires root via pkexec)\n"
            "  enable-unattended    Install a per-user passwordless polkit rule for\n"
            "                       refresh/apply (requires root via pkexec)\n"
            "  disable-unattended   Remove that rule (requires root via pkexec)\n"
            "  import-repo-key URL  Import a repository's https:// GPG signing key\n"
            "                       into the RPM keyring (requires root via pkexec)\n"
            "  apply-skip-repo ALIAS\n"
            "                       Apply updates with the named repository temporarily\n"
            "                       disabled, then re-enable it (requires root via pkexec)\n"
            "\n"
            "Options:\n"
            "  --help     Show this help message\n"
            "  --version  Show version information\n";
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        std::cout <<
            "Usage: twu-ctl <command>\n"
            "\n"
            "Commands:\n"
            "  status               Check for available updates\n"
            "  apply                Apply pending updates (requires root via pkexec)\n"
            "  refresh              Refresh repository metadata (requires root via pkexec)\n"
            "  enable-unattended    Install a per-user passwordless polkit rule for\n"
            "                       refresh/apply (requires root via pkexec)\n"
            "  disable-unattended   Remove that rule (requires root via pkexec)\n"
            "  import-repo-key URL  Import a repository's https:// GPG signing key\n"
            "                       into the RPM keyring (requires root via pkexec)\n"
            "  apply-skip-repo ALIAS\n"
            "                       Apply updates with the named repository temporarily\n"
            "                       disabled, then re-enable it (requires root via pkexec)\n"
            "\n"
            "Options:\n"
            "  --help     Show this help message\n"
            "  --version  Show version information\n";
        return 0;
    }

    if (cmd == "--version" || cmd == "-v") {
        std::cout << "twu-ctl 0.1.3\n";
        return 0;
    }

    if (cmd == "status")             return cmd_status();
    if (cmd == "apply")              return cmd_apply();
    if (cmd == "refresh")            return cmd_refresh();
    if (cmd == "enable-unattended")  return cmd_enable_unattended();
    if (cmd == "disable-unattended") return cmd_disable_unattended();
    if (cmd == "import-repo-key") {
        if (argc < 3) {
            std::cerr << "twu-ctl: import-repo-key requires a URL argument\n";
            return 1;
        }
        return cmd_import_repo_key(argv[2]);
    }
    if (cmd == "apply-skip-repo") {
        if (argc < 3) {
            std::cerr << "twu-ctl: apply-skip-repo requires a repository alias argument\n";
            return 1;
        }
        return cmd_apply(argv[2]);
    }

    std::cerr << "twu-ctl: unknown command: " << cmd << "\n";
    std::cerr << "Try 'twu-ctl --help' for more information.\n";
    return 1;
}
