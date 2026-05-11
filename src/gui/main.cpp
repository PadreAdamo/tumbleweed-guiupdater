#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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

#include <QDir>
#include <QFile>

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
    QString    vendorPolicy;
    bool       vendorChangeDetected = false;
    int        vendorChangeCount    = 0;
    QJsonArray vendorChanges;
    bool       flatpakUpdatesAvailable = false;
    int        flatpakUpdateCount      = 0;
    QString    flatpakList;
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
    s.vendorPolicy             = o["vendorPolicy"].toString(QStringLiteral("priority"));
    s.vendorChangeDetected     = o["vendorChangeDetected"].toBool();
    s.vendorChangeCount        = o["vendorChangeCount"].toInt();
    s.vendorChanges            = o["vendorChanges"].toArray();
    s.flatpakUpdatesAvailable  = o["flatpakUpdatesAvailable"].toBool();
    s.flatpakUpdateCount       = o["flatpakUpdateCount"].toInt();
    s.flatpakList              = o["flatpakList"].toString();

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

        const int fpCount  = s.flatpakUpdateCount;
        const int sysCount = updateCount - fpCount;

        if (s.flatpakUpdatesAvailable && sysCount > 0) {
            s.text = QString("⚠️ %1 system + %2 Flatpak update%3 available%4")
                         .arg(sysCount).arg(fpCount)
                         .arg(fpCount == 1 ? "" : "s")
                         .arg(suffix);
        } else if (s.flatpakUpdatesAvailable && sysCount <= 0) {
            s.text = QString("⚠️ %1 Flatpak update%2 available%3")
                         .arg(fpCount)
                         .arg(fpCount == 1 ? "" : "s")
                         .arg(suffix);
        } else if (updateCount > 0) {
            s.text = QString("⚠️ %1 updates available%2").arg(updateCount).arg(suffix);
        } else {
            s.text = QString("⚠️ Updates available%1").arg(suffix);
        }

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
        tray->setIconByName(QStringLiteral("tumbleweed-updater"));
        tray->setStatus(KStatusNotifierItem::NeedsAttention);
        tray->setToolTip(QStringLiteral("tumbleweed-updater"),
                         QStringLiteral("Tumbleweed Updater"),
                         QStringLiteral("Updates are available"));
    } else if (st.kind == "error" || st.kind == "lock") {
        tray->setIconByName(QStringLiteral("tumbleweed-updater"));
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setToolTip(QStringLiteral("tumbleweed-updater"),
                         QStringLiteral("Tumbleweed Updater"),
                         QStringLiteral("Could not check for updates"));
    } else {
        tray->setIconByName(QStringLiteral("tumbleweed-updater"));
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setToolTip(QStringLiteral("tumbleweed-updater"),
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
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("tumbleweed-updater")));

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

    KConfigGroup vendorPolicyGrp = cfg->group(QStringLiteral("VendorPolicy"));
    QString vendorPolicyMode = vendorPolicyGrp.readEntry("Mode", QStringLiteral("priority"));

    const bool snapperAvailable    = QFile::exists(QStringLiteral("/usr/bin/snapper"));
    const bool snapperGuiAvailable = QFile::exists(QStringLiteral("/usr/bin/snapper-gui"));

    setProp(root, "settingsAutoCheckEnabled", autoCheckEnabled);
    setProp(root, "settingsIntervalHours",    intervalHours);
    setProp(root, "settingsSnapperEnabled",   snapperEnabled);
    setProp(root, "settingsFlatpakEnabled",   flatpakEnabled);
    setProp(root, "settingsVendorPolicy",     vendorPolicyMode);
    setProp(root, "snapperAvailable",         snapperAvailable);
    setProp(root, "snapperGuiAvailable",      snapperGuiAvailable);
    setProp(root, "appVersion",               QStringLiteral(APP_VERSION));

    // Offer to enable the systemd background-check timer on first launch.
    // Only shown once — tracked by [Timer] OfferShown in KConfig.
    {
        KConfigGroup timerGrp = cfg->group(QStringLiteral("Timer"));
        const bool offerShown = timerGrp.readEntry("OfferShown", false);
        if (!offerShown) {
            timerGrp.writeEntry("OfferShown", true);
            cfg->sync();

            QProcess checkTimer;
            checkTimer.start(QStringLiteral("systemctl"),
                {QStringLiteral("--user"), QStringLiteral("is-enabled"),
                 QStringLiteral("tumbleweed-updater-check.timer")});
            checkTimer.waitForFinished(2000);
            if (checkTimer.exitCode() != 0)
                setProp(root, "timerOfferReady", true);
        }
    }

    // Post-reboot recovery check: if the last successful apply with snapshots
    // happened before the current boot and hasn't been confirmed yet, show the
    // "how did it go?" dialog once after startup.
    if (snapperAvailable) {
        const int lastConfirmed =
            cfg->group(QStringLiteral("Snapper")).readEntry("LastConfirmedSnapshot", -1);

        const QString histPath =
            QDir::homePath() + QStringLiteral("/.local/share/TumbleweedUpdater/history.log");
        QFile histFile(histPath);
        if (histFile.open(QIODevice::ReadOnly)) {
            QJsonObject lastApply;
            const QList<QByteArray> lines = histFile.readAll().split('\n');
            for (int i = lines.size() - 1; i >= 0; --i) {
                const QByteArray trimmed = lines[i].trimmed();
                if (trimmed.isEmpty()) continue;
                QJsonParseError jerr{};
                const QJsonDocument jdoc = QJsonDocument::fromJson(trimmed, &jerr);
                if (jerr.error != QJsonParseError::NoError || !jdoc.isObject()) continue;
                const QJsonObject obj = jdoc.object();
                if (obj["operation"].toString() == "apply" &&
                    obj["ok"].toBool() &&
                    obj["rebootRequired"].toBool() &&
                    obj["snapperUsed"].toBool() &&
                    obj["snapshotPre"].toInt(-1) > 0) {
                    lastApply = obj;
                    break;
                }
            }

            if (!lastApply.isEmpty()) {
                const int snapPre = lastApply["snapshotPre"].toInt(-1);
                if (snapPre > 0 && snapPre != lastConfirmed) {
                    QFile uptimeFile(QStringLiteral("/proc/uptime"));
                    if (uptimeFile.open(QIODevice::ReadOnly)) {
                        const double uptimeSecs =
                            uptimeFile.readAll().split(' ').first().toDouble();
                        const QDateTime bootTime =
                            QDateTime::currentDateTimeUtc()
                            .addMSecs(-static_cast<qint64>(uptimeSecs * 1000.0));
                        const QDateTime applyTime = QDateTime::fromString(
                            lastApply["timestamp"].toString(), Qt::ISODate);
                        if (applyTime.isValid() && applyTime < bootTime) {
                            setProp(root, "postRebootSnapshotPre",
                                    snapPre);
                            setProp(root, "postRebootSnapshotPost",
                                    lastApply["snapshotPost"].toInt(-1));
                            setProp(root, "postRebootTimestamp",
                                    lastApply["timestamp"].toString());
                            // QML Timer fires 800ms after the event loop starts
                            // so the window is fully shown before the dialog opens.
                            setProp(root, "postRebootCheckReady", true);
                        }
                    }
                }
            }
        }
    }

    QTimer autoCheckTimer;
    autoCheckTimer.setSingleShot(true);

    // ---- Tray icon ----

    // Declare before lambdas so [&] captures the pointer variable correctly;
    // the pointer is assigned below, before the event loop starts.
    KStatusNotifierItem *tray = nullptr;
    bool lastUpdatesAvailable = false;

    QProcess proc;
    bool applyInProgress    = false;
    bool refreshInProgress  = false;
    bool rollbackInProgress = false;
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

            // Phase 1 (refresh) complete — start the actual status check.
            // Refresh failure is non-fatal: stale cache beats no check at all.
            if (refreshInProgress) {
                refreshInProgress = false;
                stdoutAccum.clear();
                proc.start(findTwuCtl(), {"status"});
                if (!proc.waitForStarted(1000)) {
                    setProp(root, "statusKind", "error");
                    setProp(root, "statusText", "❌ Error: could not start status check");
                    setProp(root, "busy", false);
                }
                return;
            }

            // Rollback complete — surface the result in a dialog.
            if (rollbackInProgress) {
                rollbackInProgress = false;
                const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
                const QString out = stdoutAccum.trimmed();
                setProp(root, "rollbackSucceeded",
                        ok);
                setProp(root, "rollbackOutput",
                        out.isEmpty() ? err : out + (err.isEmpty() ? "" : "\n" + err));
                setProp(root, "showRollbackResultDialog", true);
                setProp(root, "busy",                     false);
                return;
            }

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

            if (applyInProgress && st.vendorChangeDetected && st.vendorChangeCount > 0)
                st.text += QString("\n⚠️ %1 package(s) changed vendor during this update")
                               .arg(st.vendorChangeCount);

            // Convert vendorChanges QJsonArray to QVariantList so QML can iterate it
            QVariantList vcList;
            for (const QJsonValue &item : st.vendorChanges) {
                const QJsonObject vco = item.toObject();
                QVariantMap m;
                m[QStringLiteral("package")]    = vco[QStringLiteral("package")].toString();
                m[QStringLiteral("fromVendor")] = vco[QStringLiteral("fromVendor")].toString();
                m[QStringLiteral("toVendor")]   = vco[QStringLiteral("toVendor")].toString();
                vcList.append(m);
            }

            setProp(root, "statusKind",               st.kind);
            setProp(root, "statusText",               st.text);
            setProp(root, "updatesAvailable",         st.updatesAvailable);
            setProp(root, "packageList",              st.packageList);
            setProp(root, "vendorChangeDetected",     st.vendorChangeDetected);
            setProp(root, "vendorChangeCount",        st.vendorChangeCount);
            setProp(root, "vendorChanges",            vcList);
            setProp(root, "flatpakUpdatesAvailable",  st.flatpakUpdatesAvailable);
            setProp(root, "flatpakUpdateCount",       st.flatpakUpdateCount);
            setProp(root, "flatpakList",              st.flatpakList);
            setProp(root, "busy", false);

            // Expose snapshot numbers to QML and show the rollback banner.
            if (applyInProgress) {
                setProp(root, "snapperUsed",  st.snapperUsed);
                setProp(root, "snapshotPre",  st.snapshotPre);
                setProp(root, "snapshotPost", st.snapshotPost);
                if (st.snapperUsed && st.snapshotPre > 0)
                    setProp(root, "showSnapperBanner", true);
            }

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

            refreshInProgress = true;
            applyInProgress   = false;
            stdoutAccum.clear();
            setProp(root, "applyLog",         QString());
            setProp(root, "showSnapperBanner", false);

            if (tray) {
                tray->setIconByName(QStringLiteral("tumbleweed-updater"));
                tray->setToolTipTitle(QStringLiteral("Checking for updates…"));
                tray->setStatus(KStatusNotifierItem::Active);
            }

            // Phase 1: refresh repo metadata under pkexec so zypper lu sees
            // current package versions, not stale cache.
            proc.start("pkexec", {findTwuCtl(), "refresh"});
            if (!proc.waitForStarted(1000)) {
                refreshInProgress = false;
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
                tray->setIconByName(QStringLiteral("tumbleweed-updater"));
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
            vendorPolicyMode = root->property("settingsVendorPolicy").toString();

            auto wcfg = KSharedConfig::openConfig();
            wcfg->group(QStringLiteral("AutoCheck")).writeEntry("Enabled",       autoCheckEnabled);
            wcfg->group(QStringLiteral("AutoCheck")).writeEntry("IntervalHours", intervalHours);
            wcfg->group(QStringLiteral("Snapper")).writeEntry("Enabled",         snapperEnabled);
            wcfg->group(QStringLiteral("Flatpak")).writeEntry("Enabled",         flatpakEnabled);
            wcfg->group(QStringLiteral("VendorPolicy")).writeEntry("Mode",       vendorPolicyMode);
            wcfg->sync();

            if (autoCheckEnabled)
                autoCheckTimer.start(intervalMs);
            else
                autoCheckTimer.stop();

            // Keep the systemd timer interval in sync with the in-app setting
            QProcess::startDetached(QStringLiteral("systemctl"), {
                QStringLiteral("--user"), QStringLiteral("set-property"),
                QStringLiteral("tumbleweed-updater-check.timer"),
                QStringLiteral("OnUnitActiveSec=") + QString::number(intervalHours) + QStringLiteral("h")
            });
        }

        if (root->property("loadHistoryRequested").toBool()) {
            root->setProperty("loadHistoryRequested", false);
            const QString path =
                QDir::homePath() + QStringLiteral("/.local/share/TumbleweedUpdater/history.log");
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
                QDir::homePath() + QStringLiteral("/.local/share/TumbleweedUpdater/history.log");
            QFile::remove(path);
            root->setProperty("historyLog", QString());
        }

        if (root->property("enableTimerRequested").toBool()) {
            root->setProperty("enableTimerRequested", false);
            QProcess::startDetached(QStringLiteral("systemctl"),
                {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"),
                 QStringLiteral("tumbleweed-updater-check.timer")});
        }

        if (root->property("runSnapperGuiRequested").toBool()) {
            root->setProperty("runSnapperGuiRequested", false);
            if (QFile::exists(QStringLiteral("/usr/bin/snapper-gui")))
                QProcess::startDetached(QStringLiteral("snapper-gui"), {});
            else
                QProcess::startDetached(QStringLiteral("xdg-open"),
                    {QStringLiteral("https://software.opensuse.org/search?q=snapper")});
        }

        if (root->property("runRollbackRequested").toBool()) {
            root->setProperty("runRollbackRequested", false);

            if (proc.state() != QProcess::NotRunning) return;

            const int snapNum = root->property("rollbackSnapshotNum").toInt();
            if (snapNum <= 0) return;

            rollbackInProgress = true;
            applyInProgress    = false;
            refreshInProgress  = false;
            stdoutAccum.clear();
            setProp(root, "busy", true);

            proc.start("pkexec", {"snapper", "rollback", QString::number(snapNum)});
            if (!proc.waitForStarted(1000)) {
                rollbackInProgress = false;
                setProp(root, "rollbackSucceeded",        false);
                setProp(root, "rollbackOutput",
                        QStringLiteral("Failed to launch snapper rollback."));
                setProp(root, "showRollbackResultDialog", true);
                setProp(root, "busy",                     false);
            }
        }

        if (root->property("rebootConfirmedRequested").toBool()) {
            root->setProperty("rebootConfirmedRequested", false);
            const int snapPre  = root->property("postRebootSnapshotPre").toInt();
            const int snapPost = root->property("postRebootSnapshotPost").toInt();

            const QString histPath =
                QDir::homePath() + QStringLiteral("/.local/share/TumbleweedUpdater/history.log");
            QFile hf(histPath);
            if (hf.open(QIODevice::Append | QIODevice::Text)) {
                const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                hf.write(
                    QString("{\"timestamp\":\"%1\",\"operation\":\"reboot-confirmed\","
                            "\"ok\":true,\"updateCount\":0,\"rebootRequired\":false,"
                            "\"snapperUsed\":true,\"snapshotPre\":%2,\"snapshotPost\":%3,"
                            "\"details\":\"\"}\n")
                    .arg(ts).arg(snapPre).arg(snapPost).toUtf8());
            }

            auto wcfg = KSharedConfig::openConfig();
            wcfg->group(QStringLiteral("Snapper"))
                .writeEntry("LastConfirmedSnapshot", snapPre);
            wcfg->sync();
        }
    });

    // ---- Build the tray icon ----

    tray = new KStatusNotifierItem(QStringLiteral("tumbleweed-updater"), &app);
    tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    tray->setStatus(KStatusNotifierItem::Active);
    tray->setTitle(QStringLiteral("Tumbleweed Updater"));
    tray->setIconByName(QStringLiteral("tumbleweed-updater"));
    tray->setToolTip(QStringLiteral("tumbleweed-updater"),
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
        tray->setIconByName(QStringLiteral("tumbleweed-updater"));
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
            tray->setIconByName(QStringLiteral("tumbleweed-updater"));
            tray->setToolTipTitle(QStringLiteral("Checking for updates…"));
            tray->setStatus(KStatusNotifierItem::Active);
        }
    });

    if (autoCheckEnabled)
        autoCheckTimer.start(intervalMs);

    poll.start();
    return app.exec();
}
