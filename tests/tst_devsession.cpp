#include "devsession.h"

#include <QTemporaryDir>
#include <QtTest>

class DevSessionTests final : public QObject {
    Q_OBJECT

private slots:
    // FailedToStart emits no finished() signal. Before errorOccurred was
    // connected, a session whose application could not start sat in exec()
    // forever with nothing running and no nonzero exit. A hang here fails the
    // test by timing out, which is the point.
    void sessionReportsAnApplicationThatCannotStart()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());

        DevSession session(
            DevSession::Configuration{
                .projectRoot = project.path(),
                .application =
                    ApplicationDefinition{
                        .name = QStringLiteral("Missing"),
                        .target = QStringLiteral("Missing"),
                        .id = QStringLiteral("com.example.missing"),
                        .uri = QStringLiteral("com.example.Missing"),
                        .entry = QStringLiteral("Main"),
                        .qmlRoots = {QStringLiteral("qml")},
                        .assetRoots = {},
                        .platforms = {QStringLiteral("desktop")},
                    },
                .buildDirectory = project.filePath(QStringLiteral("build")),
                .buildConfiguration = QStringLiteral("Debug"),
                .cmakeArguments = {},
                .generator = QStringLiteral("Ninja"),
                .executable = project.filePath(QStringLiteral("does-not-exist")),
                .applicationArguments = {},
            });

        QString error;
        QElapsedTimer timer;
        timer.start();
        const auto status = session.run(&error);

        QCOMPARE(status, 127);
        QVERIFY2(!error.isEmpty(), "a failed start produced no explanation");
        QVERIFY2(
            timer.elapsed() < 10000,
            "the session did not return promptly from a failed start");
    }
};

QTEST_GUILESS_MAIN(DevSessionTests)
#include "devsession_tests.moc"
