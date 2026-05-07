#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QTimeZone>
#include <QMenu>
#include <QAction>
#include <QIcon>

#include <KStatusNotifierItem>
#include <KNotification>
#include <KSharedConfig>
#include <KConfigGroup>

#include <QFile>
#include <QStandardPaths>

#include <cstdio>

static bool isOnBattery()
{
    FILE *f = fopen("/sys/class/power_supply/AC/online", "r");
    if (!f) return false;
    char buf[4] = {};
    const bool read = fgets(buf, sizeof(buf), f) != nullptr;
    fclose(f);
    return read && buf[0] == '0';
}

static QString findTwuCtl()
{
    return QStringLiteral("/usr/bin/twu-ctl");
}

static QString prettyTimestamp(const QString &isoUtc)
{
    QDateTime dt = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!dt.isValid())
        return {};

    dt = dt.toTimeZone(QTimeZone::UTC);
    return dt.toLocalTime().toString("MMM d, h:mm AP");
}

struct UiStatus {
    QString kind;
    QString text;
    bool updatesAvailable = false;
    bool rebootRequired = false;
    QString packageList;
    bool snapperUsed = false;
    int snapshotPre  = -1;
    int snapshotPost = -1;
    int updateCount  = 0;
};

static UiStatus parseStatusJson(const QString &out)
{
    UiStatus s;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        s.kind = "error";
        s.text = out.isEmpty() ? "❌ Invalid controller output" : out;
        return s;
    }

    const QJsonObject o = doc.object();

    const bool ok = o["ok"].toBool();
    const bool updates = o["updatesAvailable"].toBool();
    const int updateCount = o["updateCount"].toInt();
    const bool rebootRequired = o["rebootRequired"].toBool();
    const bool needsAuth = o["needsAuth"].toBool();
    const QString details = o["details"].toString();
    const QString preview = o["packagePreview"].toString();
    const QString packageList = o["packageList"].toString();
    const QString ts = prettyTimestamp(o["timestamp"].toString());

    const QString suffix = ts.isEmpty() ? QString() : QString(" • %1").arg(ts);

    s.updatesAvailable = updates;
    s.rebootRequired = rebootRequired;
    s.packageList = packageList;
    s.snapperUsed = o["snapperUsed"].toBool();
    s.snapshotPre  = o["snapshotPre"].toInt(-1);
    s.snapshotPost = o["snapshotPost"].toInt(-1);
    s.updateCount  = o["updateCount"].toInt();

    if (!ok) {
        if (needsAuth) {
            s.kind = "lock";
            s.text = QString("🔒 Admin required%1").arg(suffix);
            if (!details.isEmpty())
                s.text += "\n" + details;
            return s;
        }

        s.kind = "error";
        s.text = QString("❌ Error checking updates%1").arg(suffix);
        if (!details.isEmpty())
            s.text += "\n" + details;
        return s;
    }

    if (updates) {
        s.kind = "warn";

        if (updateCount > 0)
            s.text = QString("⚠️ %1 updates available%2").arg(updateCount).arg(suffix);
        else
            s.text = QString("⚠️ Updates available%1").arg(suffix);

        if (!preview.isEmpty())
            s.text += "\n" + preview;

        if (rebootRequired)
            s.text += "\n\n🔁 Reboot will likely be required";

        if (!details.isEmpty())
            s.text += "\n\n" + details;

        return s;
    }

    s.kind = "ok";
    s.text = QString("✅ Up to date%1").arg(suffix);
    if (!details.isEmpty())
        s.text += "\n" + details;

    return s;
}

static void setProp(QObject *root, const char *name, const QVariant &v)
{
    if (root)
        root->setProperty(name, v);
}

// Update the tray icon and tooltip to reflect the latest status.
static void updateTray(KStatusNotifierItem *tray, const UiStatus &st)
{
    if (st.updatesAvailable) {
        tray->setIconByName(QStringLiteral("update-low"));
        tray->setStatus(KStatusNotifierItem::NeedsAttention);
        tray->setToolTip(QStringLiteral("update-low"),
                         QStringLiteral("Tumbleweed Updater"),
                         QStringLiteral("Updates are available"));
    } else if (st.kind == "error" || st.kind == "lock") {
        tray->setIconByName(QStringLiteral("dialog-error"));
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setToolTip(QStringLiteral("dialog-error"),
                         QStringLiteral("Tumbleweed Updater"),
                         QStringLiteral("Could not check for updates"));
    } else {
        tray->setIconByName(QStringLiteral("update-none"));
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setToolTip(QStringLiteral("update-none"),
                         QStringLiteral("Tumbleweed Updater"),
                         QStringLiteral("System is up to date"));
    }
}

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication) because KStatusNotifierItem's context
    // menu is a QMenu which requires the widget subsystem.
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QGuiApplication::setDesktopFileName(QStringLiteral("tumbleweed-updater"));

    QCoreApplication::setOrganizationName("TumbleweedUpdater");
    QCoreApplication::setOrganizationDomain("tumbleweedupdater.local");
    QCoreApplication::setApplicationName("TumbleweedUpdater");

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().first();
    QQuickWindow *window = qobject_cast<QQuickWindow *>(root);

    // ---- Read all settings from KConfig ----

    auto cfg = KSharedConfig::openConfig();

    KConfigGroup autoCheckGrp = cfg->group(QStringLiteral("AutoCheck"));
    bool autoCheckEnabled = autoCheckGrp.readEntry("Enabled", true);
    int  intervalHours    = autoCheckGrp.readEntry("IntervalHours", 4);
    int  intervalMs       = intervalHours * 60 * 60 * 1000;

    KConfigGroup snapperGrp  = cfg->group(QStringLiteral("Snapper"));
    bool snapperEnabled = snapperGrp.readEntry("Enabled", true);

    KConfigGroup flatpakGrp  = cfg->group(QStringLiteral("Flatpak"));
    bool flatpakEnabled = flatpakGrp.readEntry("Enabled", false);

    const bool snapperAvailable = QFile::exists(QStringLiteral("/usr/bin/snapper"));

    setProp(root, "settingsAutoCheckEnabled", autoCheckEnabled);
    setProp(root, "settingsIntervalHours",    intervalHours);
    setProp(root, "settingsSnapperEnabled",   snapperEnabled);
    setProp(root, "settingsFlatpakEnabled",   flatpakEnabled);
    setProp(root, "snapperAvailable",         snapperAvailable);
    setProp(root, "appVersion",               QStringLiteral(APP_VERSION));

    QTimer autoCheckTimer;
    autoCheckTimer.setSingleShot(true);

    // ---- Tray icon ----

    // Declare before lambdas so [&] captures the pointer variable correctly;
    // the pointer is assigned below, before the event loop starts.
    KStatusNotifierItem *tray = nullptr;
    bool lastUpdatesAvailable = false;

    QProcess proc;
    bool applyInProgress = false;
    QString stdoutAccum;

    // Helper: show/raise the main window
    auto showWindow = [window]() {
        window->show();
        window->raise();
        window->requestActivate();
    };

    // During apply, stream each chunk into the QML log as it arrives.
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, &app,
        [&]() {
            const auto data = QString::fromUtf8(proc.readAllStandardOutput());
            stdoutAccum += data;
            if (applyInProgress)
                setProp(root, "applyLog", stdoutAccum);
        });

    QObject::connect(&proc, &QProcess::finished, &app,
        [&](int exitCode, QProcess::ExitStatus exitStatus) {
            // Drain any data that arrived after the last readyReadStandardOutput
            stdoutAccum += QString::fromUtf8(proc.readAllStandardOutput());
            const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

            UiStatus st;

            if (exitStatus != QProcess::NormalExit) {
                st.kind = "error";
                st.text = "❌ Controller crashed";
            } else if (applyInProgress) {
                // The controller writes plain-text zypper lines then a JSON object
                // as the final line. Find that JSON line working backwards.
                const QStringList lines = stdoutAccum.split('\n', Qt::SkipEmptyParts);
                QString jsonLine;
                int jsonIdx = -1;
                for (int i = lines.size() - 1; i >= 0; --i) {
                    if (lines[i].trimmed().startsWith('{')) {
                        jsonLine = lines[i].trimmed();
                        jsonIdx  = i;
                        break;
                    }
                }

                if (jsonLine.isEmpty()) {
                    // pkexec auth cancellation or other failure before zypper ran
                    st.kind = "error";
                    st.text = err.isEmpty()
                        ? QString("❌ Apply failed (exit %1)").arg(exitCode)
                        : QString("❌ %1").arg(err);
                } else {
                    // Show only the zypper output (pre-JSON lines) in the log
                    const QStringList logLines = lines.mid(0, jsonIdx);
                    setProp(root, "applyLog", logLines.join('\n'));
                    st = parseStatusJson(jsonLine);

                    if (st.rebootRequired) {
                        st.kind = "warn";
                        st.text = "⚠️ Updates applied — reboot recommended";
                    }
                }
            } else {
                // Status command: entire stdout is one JSON object
                const QString out = stdoutAccum.trimmed();
                if (exitCode != 0 && out.isEmpty()) {
                    st.kind = "error";
                    st.text = err.isEmpty()
                        ? QString("❌ Error: exit %1").arg(exitCode)
                        : QString("❌ Error: %1").arg(err);
                } else {
                    st = parseStatusJson(out);
                }
            }

            if (applyInProgress && st.snapperUsed)
                st.text += QString("\n📸 Snapshots #%1 (pre) and #%2 (post) created")
                               .arg(st.snapshotPre).arg(st.snapshotPost);

            setProp(root, "statusKind", st.kind);
            setProp(root, "statusText", st.text);
            setProp(root, "updatesAvailable", st.updatesAvailable);
            setProp(root, "packageList", st.packageList);
            setProp(root, "busy", false);

            // Fire reboot dialog only after a successful apply, once busy is clear.
            if (applyInProgress && st.rebootRequired) {
                setProp(root, "rebootRequired", true);
                setProp(root, "showRebootDialog", true);
            }

            applyInProgress = false;

            // Update tray to reflect the new status
            if (tray)
                updateTray(tray, st);

            // Show a desktop notification the first time updates are detected.
            if (st.updatesAvailable && !lastUpdatesAvailable) {
                auto *notif = new KNotification(
                    QStringLiteral("updateAvailable"),
                    KNotification::CloseOnTimeout,
                    &app
                );
                notif->setTitle(QStringLiteral("Updates Available"));
                notif->setText(QStringLiteral("Your system has updates ready to install."));
                notif->setIconName(QStringLiteral("update-low"));
                notif->sendEvent();
            }
            lastUpdatesAvailable = st.updatesAvailable;
        });

    QTimer poll;
    poll.setInterval(150);

    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (root->property("runStatusRequested").toBool()) {
            root->setProperty("runStatusRequested", false);

            if (proc.state() != QProcess::NotRunning) {
                setProp(root, "statusKind", "warn");
                setProp(root, "statusText", "Busy…");
                return;
            }

            applyInProgress = false;
            stdoutAccum.clear();
            setProp(root, "applyLog", QString());

            if (tray) {
                tray->setIconByName(QStringLiteral("view-refresh"));
                tray->setToolTipTitle(QStringLiteral("Checking for updates…"));
                tray->setStatus(KStatusNotifierItem::Active);
            }

            proc.start(findTwuCtl(), {"status"});
            if (!proc.waitForStarted(1000)) {
                setProp(root, "statusKind", "error");
                setProp(root, "statusText", "❌ Error: could not start controller");
                setProp(root, "busy", false);
            } else {
                autoCheckTimer.start(intervalMs);
            }
        }

        if (root->property("runApplyRequested").toBool()) {
            root->setProperty("runApplyRequested", false);

            if (proc.state() != QProcess::NotRunning) {
                setProp(root, "statusKind", "warn");
                setProp(root, "statusText", "Busy…");
                return;
            }

            applyInProgress = true;
            stdoutAccum.clear();
            setProp(root, "applyLog", QString());
            setProp(root, "rebootRequired", false);

            if (tray) {
                tray->setIconByName(QStringLiteral("view-refresh"));
                tray->setToolTipTitle(QStringLiteral("Applying updates…"));
                tray->setStatus(KStatusNotifierItem::Active);
            }

            proc.start("pkexec", {findTwuCtl(), "apply"});
            if (!proc.waitForStarted(1000)) {
                setProp(root, "statusKind", "error");
                setProp(root, "statusText", "❌ Error: could not start apply");
                setProp(root, "busy", false);
                applyInProgress = false;
            } else {
                autoCheckTimer.start(intervalMs);
            }
        }

        if (root->property("runRebootRequested").toBool()) {
            root->setProperty("runRebootRequested", false);
            QProcess::startDetached("pkexec", {"systemctl", "reboot"});
        }

        if (root->property("saveSettingsRequested").toBool()) {
            root->setProperty("saveSettingsRequested", false);

            autoCheckEnabled = root->property("settingsAutoCheckEnabled").toBool();
            intervalHours    = root->property("settingsIntervalHours").toInt();
            intervalMs       = intervalHours * 60 * 60 * 1000;
            snapperEnabled   = root->property("settingsSnapperEnabled").toBool();
            flatpakEnabled   = root->property("settingsFlatpakEnabled").toBool();

            auto wcfg = KSharedConfig::openConfig();
            wcfg->group(QStringLiteral("AutoCheck")).writeEntry("Enabled",       autoCheckEnabled);
            wcfg->group(QStringLiteral("AutoCheck")).writeEntry("IntervalHours", intervalHours);
            wcfg->group(QStringLiteral("Snapper")).writeEntry("Enabled",         snapperEnabled);
            wcfg->group(QStringLiteral("Flatpak")).writeEntry("Enabled",         flatpakEnabled);
            wcfg->sync();

            if (autoCheckEnabled)
                autoCheckTimer.start(intervalMs);
            else
                autoCheckTimer.stop();
        }

        if (root->property("loadHistoryRequested").toBool()) {
            root->setProperty("loadHistoryRequested", false);
            const QString path =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + "/history.log";
            QFile file(path);
            QString content;
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QByteArray all = file.readAll();
                const QList<QByteArray> rawLines = all.split('\n');
                QStringList kept;
                for (int i = rawLines.size() - 1; i >= 0 && kept.size() < 100; --i) {
                    const QByteArray line = rawLines[i].trimmed();
                    if (!line.isEmpty())
                        kept.prepend(QString::fromUtf8(line));
                }
                content = kept.join('\n');
            }
            root->setProperty("historyLog", content);
        }

        if (root->property("clearHistoryRequested").toBool()) {
            root->setProperty("clearHistoryRequested", false);
            const QString path =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + "/history.log";
            QFile::remove(path);
            root->setProperty("historyLog", QString());
        }
    });

    // ---- Build the tray icon ----

    tray = new KStatusNotifierItem(QStringLiteral("tumbleweed-updater"), &app);
    tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    tray->setStatus(KStatusNotifierItem::Active);
    tray->setTitle(QStringLiteral("Tumbleweed Updater"));
    tray->setIconByName(QStringLiteral("view-refresh"));
    tray->setToolTip(QStringLiteral("view-refresh"),
                     QStringLiteral("Tumbleweed Updater"),
                     QStringLiteral("Checking for updates…"));
    tray->setAssociatedWindow(window);

    // Context menu
    auto *trayMenu = new QMenu();

    auto *checkAction = trayMenu->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        QStringLiteral("Check Now"));

    auto *showAction = trayMenu->addAction(
        QIcon::fromTheme(QStringLiteral("window-restore")),
        QStringLiteral("Show Window"));

    trayMenu->addSeparator();

    auto *quitAction = trayMenu->addAction(
        QIcon::fromTheme(QStringLiteral("application-exit")),
        QStringLiteral("Quit"));

    tray->setContextMenu(trayMenu);

    // Left-click: toggle window visibility
    QObject::connect(tray, &KStatusNotifierItem::activateRequested, &app,
        [window, showWindow](bool /*active*/, const QPoint &) {
            if (window->isVisible())
                window->hide();
            else
                showWindow();
        });

    QObject::connect(checkAction, &QAction::triggered, &app, [root, tray]() {
        if (root->property("busy").toBool())
            return;
        root->setProperty("busy", true);
        root->setProperty("statusText", "Checking for updates…");
        root->setProperty("statusKind", "ok");
        root->setProperty("runStatusRequested", true);
        tray->setIconByName(QStringLiteral("view-refresh"));
        tray->setToolTipTitle(QStringLiteral("Checking for updates…"));
        tray->setStatus(KStatusNotifierItem::Active);
    });

    QObject::connect(showAction, &QAction::triggered, &app, showWindow);

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    QObject::connect(&autoCheckTimer, &QTimer::timeout, &app, [&]() {
        if (!root->property("settingsAutoCheckEnabled").toBool()) return;
        if (isOnBattery()) {
            fprintf(stderr, "[auto-check] on battery — rescheduling in 30 minutes\n");
            autoCheckTimer.start(30 * 60 * 1000);
            return;
        }
        autoCheckTimer.start(intervalMs);
        if (root->property("busy").toBool()) return;
        fprintf(stderr, "[auto-check] triggering background status check\n");
        root->setProperty("busy", true);
        root->setProperty("statusText", "Checking for updates…");
        root->setProperty("statusKind", "ok");
        root->setProperty("runStatusRequested", true);
        if (tray) {
            tray->setIconByName(QStringLiteral("view-refresh"));
            tray->setToolTipTitle(QStringLiteral("Checking for updates…"));
            tray->setStatus(KStatusNotifierItem::Active);
        }
    });

    if (autoCheckEnabled)
        autoCheckTimer.start(intervalMs);

    poll.start();
    return app.exec();
}
