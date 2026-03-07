#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QTimeZone>

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
    QString packageList;
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
    const bool needsAuth = o["needsAuth"].toBool();
    const QString details = o["details"].toString();
    const QString preview = o["packagePreview"].toString();
    const QString packageList = o["packageList"].toString();
    const QString ts = prettyTimestamp(o["timestamp"].toString());

    const QString suffix = ts.isEmpty() ? QString() : QString(" • %1").arg(ts);

    s.updatesAvailable = updates;
    s.packageList = packageList;

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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

    engine.load(QUrl::fromLocalFile(
        QStringLiteral(TWU_SOURCE_DIR "/src/gui/qml/Main.qml")
    ));

    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().first();

    QProcess proc;

    QObject::connect(&proc, &QProcess::finished, &app,
                     [&](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

        UiStatus st;

        if (exitStatus != QProcess::NormalExit) {
            st.kind = "error";
            st.text = "❌ Controller crashed";
        } else if (exitCode != 0 && out.isEmpty()) {
            st.kind = "error";
            st.text = err.isEmpty() ? QString("❌ Error: exit %1").arg(exitCode)
                                    : QString("❌ Error: %1").arg(err);
        } else {
            st = parseStatusJson(out);
        }

        setProp(root, "statusKind", st.kind);
        setProp(root, "statusText", st.text);
        setProp(root, "updatesAvailable", st.updatesAvailable);
        setProp(root, "packageList", st.packageList);
        setProp(root, "busy", false);
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

            proc.start(findTwuCtl(), {"status"});
            if (!proc.waitForStarted(1000)) {
                setProp(root, "statusKind", "error");
                setProp(root, "statusText", "❌ Error: could not start controller");
                setProp(root, "busy", false);
            }
        }

        if (root->property("runApplyRequested").toBool()) {
            root->setProperty("runApplyRequested", false);

            if (proc.state() != QProcess::NotRunning) {
                setProp(root, "statusKind", "warn");
                setProp(root, "statusText", "Busy…");
                return;
            }

            proc.start("pkexec", {findTwuCtl(), "apply"});
            if (!proc.waitForStarted(1000)) {
                setProp(root, "statusKind", "error");
                setProp(root, "statusText", "❌ Error: could not start apply");
                setProp(root, "busy", false);
            }
        }
    });

    poll.start();
    return app.exec();
}
