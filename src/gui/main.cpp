#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <QFileInfo>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QDateTime>
#include <QTimeZone>

static QString findTwuCtl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo devPath(QDir(appDir).filePath("../controller/twu-ctl"));
    if (devPath.exists() && devPath.isExecutable()) {
        return devPath.canonicalFilePath();
    }
    return QStringLiteral("twu-ctl");
}

static QString prettyTimestampLocal(const QString &isoUtc)
{
    if (isoUtc.isEmpty())
        return QString();

    QDateTime dt = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!dt.isValid())
        return QString();

    dt = dt.toTimeZone(QTimeZone::UTC);
    const QDateTime local = dt.toLocalTime();
    return local.toString("MMM d, h:mm AP");
}

struct UiStatus {
    QString kind; // ok | warn | lock | error
    QString text;
};

static UiStatus statusFromJson(const QJsonObject &o)
{
    const bool ok = o.value("ok").toBool(false);
    const QString details = o.value("details").toString();
    const bool updatesAvailable = o.value("updatesAvailable").toBool(false);
    const bool needsAuth = o.value("needsAuth").toBool(false);
    const QString ts = o.value("timestamp").toString();

    const QString when = prettyTimestampLocal(ts);
    const QString suffix = when.isEmpty() ? QString() : QString(" • Last checked: %1").arg(when);

    UiStatus s;

    if (!ok) {
        if (needsAuth) {
            s.kind = "lock";
            s.text = QString("🔒 Admin required to check updates%1").arg(suffix);
            if (!details.isEmpty()) s.text += "\n" + details;
            return s;
        }
        s.kind = "error";
        s.text = QString("❌ Error checking updates%1").arg(suffix);
        if (!details.isEmpty()) s.text += "\n" + details;
        return s;
    }

    if (updatesAvailable) {
        s.kind = "warn";
        s.text = QString("⚠️ Updates available • Admin required to apply%1").arg(suffix);
        if (!details.isEmpty()) s.text += "\n" + details;
        return s;
    }

    s.kind = "ok";
    s.text = QString("✅ Up to date%1").arg(suffix);
    if (!details.isEmpty()) s.text += "\n" + details;
    return s;
}

static UiStatus statusFromOutput(const QString &out)
{
    const QByteArray raw = out.toUtf8();
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &jerr);

    if (jerr.error == QJsonParseError::NoError && doc.isObject()) {
        return statusFromJson(doc.object());
    }

    // Fallback if controller doesn't output JSON
    UiStatus s;
    s.kind = "error";
    s.text = out.isEmpty() ? QStringLiteral("❌ No output from controller") : out;
    return s;
}

static void setRootProp(QObject *root, const char *name, const QVariant &v)
{
    if (!root) return;
    root->setProperty(name, v);
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

    const QString twuCtl = findTwuCtl();
    qInfo() << "Using twu-ctl:" << twuCtl;

    QProcess proc;

    QObject::connect(&proc, &QProcess::finished, &app,
                     [&](int exitCode, QProcess::ExitStatus exitStatus) {

        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

        UiStatus st;

        if (exitStatus != QProcess::NormalExit) {
            st.kind = "error";
            st.text = "❌ Error: controller crashed";
        } else if (exitCode != 0) {
            // Controller may still output JSON on failure; prefer stdout if present.
            if (!out.isEmpty()) st = statusFromOutput(out);
            else {
                st.kind = "error";
                st.text = err.isEmpty() ? QString("❌ Error: exit %1").arg(exitCode)
                                        : QString("❌ Error: %1").arg(err);
            }
        } else {
            st = statusFromOutput(out);
        }

        setRootProp(root, "statusKind", st.kind);
        setRootProp(root, "statusText", st.text);
        setRootProp(root, "busy", false);
    });

    QTimer poll;
    poll.setInterval(150);

    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (!root->property("runStatusRequested").toBool())
            return;

        root->setProperty("runStatusRequested", false);

        if (proc.state() != QProcess::NotRunning) {
            setRootProp(root, "statusKind", "warn");
            setRootProp(root, "statusText", "Busy…");
            return;
        }

        proc.start(twuCtl, {"status"});
        if (!proc.waitForStarted(1000)) {
            setRootProp(root, "statusKind", "error");
            setRootProp(root, "statusText", "❌ Error: could not start controller");
            setRootProp(root, "busy", false);
        }
    });

    poll.start();
    return app.exec();
}
