#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <QFileInfo>

static QString findTwuCtl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo devPath(QDir(appDir).filePath("../controller/twu-ctl"));
    if (devPath.exists() && devPath.isExecutable()) {
        return devPath.canonicalFilePath();
    }
    return QStringLiteral("twu-ctl"); // fallback if installed later
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [](){
                         qCritical() << "Failed to load QML";
                         QCoreApplication::exit(1);
                     },
                     Qt::QueuedConnection);

    engine.load(QUrl::fromLocalFile(QStringLiteral(TWU_SOURCE_DIR "/src/gui/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().first();
    QObject *label = root->findChild<QObject*>("statusLabel");

    const QString twuCtl = findTwuCtl();
    qInfo() << "Using twu-ctl:" << twuCtl;

    QProcess proc;

    QObject::connect(&proc, &QProcess::finished, &app,
                     [&](int exitCode, QProcess::ExitStatus exitStatus){
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();

        QString msg;
        if (exitStatus != QProcess::NormalExit) msg = "Error: twu-ctl crashed";
        else if (exitCode != 0) msg = err.isEmpty() ? QString("Error: exit %1").arg(exitCode) : ("Error: " + err);
        else msg = out.isEmpty() ? "OK (no output)" : out;

        if (label) label->setProperty("text", msg);
    });

    QTimer poll;
    poll.setInterval(150);
    QObject::connect(&poll, &QTimer::timeout, &app, [&](){
        if (!root->property("runStatusRequested").toBool())
            return;

        root->setProperty("runStatusRequested", false);

        if (proc.state() != QProcess::NotRunning) {
            if (label) label->setProperty("text", "Busy…");
            return;
        }

        proc.start(twuCtl, {"status"});
        if (!proc.waitForStarted(1000)) {
            if (label) label->setProperty("text", "Error: could not start twu-ctl");
        }
    });
    poll.start();

    return app.exec();
}
