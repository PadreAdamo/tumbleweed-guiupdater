#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QTimeZone>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QDateTime>

static QString findTwuCtl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo devPath(QDir(appDir).filePath("../controller/twu-ctl"));
    if (devPath.exists() && devPath.isExecutable()) {
        return devPath.canonicalFilePath();
    }
    return QStringLiteral("twu-ctl"); // fallback if installed later
}

static QString prettyTimestampLocal(const QString &isoUtc)
{
    if (isoUtc.isEmpty())
        return QString();

    // Controller emits ISO-8601 UTC like: 2026-02-28T23:35:05Z
    QDateTime dt = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!dt.isValid())
        return QString();

    dt = dt.toTimeZone(QTimeZone::UTC);
    const QDateTime local = dt.toLocalTime();
    return local.toString("MMM d, h:mm AP"); // e.g. "Feb 28, 6:35 PM"
}

static QString formatHumanStatus(const QJsonObject &o)
{
    const bool ok = o.value("ok").toBool(false);
    const QString summary = o.value("summary").toString();
    const QString details = o.value("details").toString();
    const bool updatesAvailable = o.value("updatesAvailable").toBool(false);
    const bool needsAuth = o.value("needsAuth").toBool(false);
    const QString ts = o.value("timestamp").toString();

    const QString when = prettyTimestampLocal(ts);
    const QString suffix = when.isEmpty() ? QString() : QString(" • Last checked: %1").arg(when);

    if (!ok) {
        if (needsAuth) {
            return QString("🔒 Admin required to check updates%1").arg(suffix);
        }
        // Generic failure
        QString msg = QString("❌ Error checking updates%1").arg(suffix);
        if (!details.isEmpty()) {
            msg += "\n" + details;
        }
        return msg;
    }

    if (updatesAvailable) {
        QString msg = QString("⚠️ Updates available • Admin required to apply%1").arg(suffix);
        if (!details.isEmpty()) {
            msg += "\n" + details;
        }
        return msg;
    }

    // OK and no updates
    {
        QString msg = QString("✅ Up to date%1").arg(suffix);
        if (!details.isEmpty()) {
            msg += "\n" + details;
        }
        return msg;
    }
}

static QString formatStatusFromJsonOrRaw(const QString &out)
{
    const QByteArray raw = out.toUtf8();

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &jerr);

    if (jerr.error == QJsonParseError::NoError && doc.isObject()) {
        return formatHumanStatus(doc.object());
    }

    // Not JSON; fall back
    return out.isEmpty() ? QStringLiteral("OK (no output)") : out;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qCritical() << "Failed to load QML";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );

    engine.load(QUrl::fromLocalFile(
        QStringLiteral(TWU_SOURCE_DIR "/src/gui/qml/Main.qml")
    ));

    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().first();
    QObject *label = root->findChild<QObject*>("statusLabel");

    const QString twuCtl = findTwuCtl();
    qInfo() << "Using twu-ctl:" << twuCtl;

    QProcess proc;

    QObject::connect(&proc, &QProcess::finished, &app,
                     [&](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

        QString msg;

        if (exitStatus != QProcess::NormalExit) {
            msg = QStringLiteral("❌ Error: controller crashed");
        } else if (exitCode != 0) {
            // Sometimes controller writes JSON even on non-zero, but keep it simple:
            // try parsing stdout; fall back to stderr.
            msg = !out.isEmpty() ? formatStatusFromJsonOrRaw(out)
                                 : (err.isEmpty() ? QString("❌ Error: exit %1").arg(exitCode)
                                                  : QString("❌ Error: %1").arg(err));
        } else {
            msg = formatStatusFromJsonOrRaw(out);
        }

        if (label) {
            label->setProperty("text", msg);
        }
    });

    QTimer poll;
    poll.setInterval(150);

    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (!root->property("runStatusRequested").toBool())
            return;

        // Reset the flag so it runs once per click
        root->setProperty("runStatusRequested", false);

        if (proc.state() != QProcess::NotRunning) {
            if (label) label->setProperty("text", "Busy…");
            return;
        }

        proc.start(twuCtl, {"status"});
        if (!proc.waitForStarted(1000)) {
            if (label) label->setProperty("text", "❌ Error: could not start controller");
        }
    });

    poll.start();
    return app.exec();
}
