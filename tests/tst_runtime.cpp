#include <respin/protocol.h>
#include <respin/reloadcontroller.h>

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

namespace {

respin::Bundle
bundleAt(const QString &id, const QString &path, const QByteArray &contents)
{
    return respin::Bundle{
        .id = id,
        .files =
            {
                respin::BundleFile{
                    .path = path,
                    .contents = contents,
                    .sha256 =
                        QCryptographicHash::hash(contents, QCryptographicHash::Sha256),
                },
            },
    };
}

respin::Bundle bundleWithMain(const QString &id, const QByteArray &contents)
{
    return bundleAt(id, QStringLiteral("qt/qml/com/example/Test/Main.qml"), contents);
}

respin::Bundle bundleWithWindow(const QString &id, const QByteArray &contents)
{
    return bundleAt(
        id, QStringLiteral("qt/qml/com/example/WindowTest/WindowMain.qml"), contents);
}

QString bundleCacheBase()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/respin/bundles");
}

// Each ReloadController owns a "<pid>-XXXXXX" directory under the cache base.
QStringList bundleCacheRoots()
{
    return QDir(bundleCacheBase())
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
}

// Gives one test function a cache base nobody else writes to. Without this the
// directory bookkeeping below counts roots created by the other tests in this
// process, and by any concurrent run sharing the build tree's XDG_CACHE_HOME --
// which is precisely the multi-process situation these tests are about, so it
// happens readily.
class ScopedCacheHome {
public:
    ScopedCacheHome()
        : m_previous(qgetenv("XDG_CACHE_HOME"))
    {
        qputenv("XDG_CACHE_HOME", QFile::encodeName(m_directory.path()));
    }
    ~ScopedCacheHome()
    {
        qputenv("XDG_CACHE_HOME", m_previous);
    }

    ScopedCacheHome(const ScopedCacheHome &) = delete;
    ScopedCacheHome &operator=(const ScopedCacheHome &) = delete;

    bool isValid() const
    {
        return m_directory.isValid();
    }

private:
    QTemporaryDir m_directory;
    QByteArray m_previous;
};

bool isMarkedComplete(const QString &bundleDirectory)
{
    return QFileInfo::exists(bundleDirectory + QStringLiteral("/.respin-complete"));
}

bool writeFileAt(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

const QByteArray BrokenQml = "import QtQuick\nItem { objectName: ";

// A stand-in for DevServer that counts Hello frames. The real one lives in the
// CLI library and mints its own token, and this test needs to control both the
// token and the port so it can take the server away and bring it back.
class HelloCounter {
public:
    bool listen(const quint16 port = 0)
    {
        if (!m_server.listen(QHostAddress::LocalHost, port))
            return false;
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (m_server.hasPendingConnections()) {
                auto *socket = m_server.nextPendingConnection();
                m_sockets.append(socket);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray buffer = socket->readAll();
                    respin::Frame frame;
                    QString error;
                    while (respin::takeFrame(buffer, frame, &error)) {
                        if (frame.type == respin::MessageType::Hello)
                            ++m_helloCount;
                    }
                });
            }
        });
        return true;
    }

    void shutDown()
    {
        m_server.close();
        for (const auto &socket : std::as_const(m_sockets)) {
            if (socket)
                socket->abort();
        }
        m_sockets.clear();
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }
    int helloCount() const
    {
        return m_helloCount;
    }

private:
    QTcpServer m_server;
    QList<QPointer<QTcpSocket>> m_sockets;
    int m_helloCount = 0;
};

} // namespace

class RuntimeTests final : public QObject {
    Q_OBJECT

private slots:
    void reloadsAndPreservesExplicitState()
    {
        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY2(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")),
            qPrintable(controller.lastError()));
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("embedded"));
        QVERIFY(controller.rootObject()->setProperty("count", 42));
        QVariant exportedState;
        QVERIFY(
            QMetaObject::invokeMethod(
                controller.rootObject(), "respinSaveState", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, exportedState)));
        QCOMPARE(exportedState.toMap().value(QStringLiteral("count")).toInt(), 42);

        const QByteArray qml = R"(
            import QtQuick
            Item {
                objectName: "reloaded"
                property int count: 0
                function respinSaveState() { return { "count": count } }
                function respinRestoreState(state) { count = state.count }
            }
        )";
        QString error;
        const auto payload =
            respin::encodeBundle(bundleWithMain(QStringLiteral("valid"), qml));
        QVERIFY2(controller.applyBundle(payload, &error), qPrintable(error));
        QCoreApplication::processEvents();
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("reloaded"));
        QCOMPARE(controller.rootObject()->property("count").toInt(), 42);
    }

    void invalidQmlRollsBackToWorkingScene()
    {
        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray valid = R"(
            import QtQuick
            Item { objectName: "working"; property int count: 7 }
        )";
        QString error;
        QVERIFY(controller.applyBundle(
            respin::encodeBundle(bundleWithMain(QStringLiteral("working"), valid)),
            &error));
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("working"));

        const QByteArray invalid = "import QtQuick\nItem { objectName: ";
        QVERIFY(!controller.applyBundle(
            respin::encodeBundle(bundleWithMain(QStringLiteral("broken"), invalid)),
            &error));
        QVERIFY(
            error.contains(QStringLiteral("Expected token"))
            || error.contains(QStringLiteral("Syntax error"))
            || error.contains(QStringLiteral("Unexpected token")));
        QVERIFY(controller.rootObject());
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("working"));
    }

    void reloadingAWindowSceneKeepsTheApplicationRunning()
    {
        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY2(
            controller.load(
                QStringLiteral("com.example.WindowTest"), QStringLiteral("WindowMain")),
            qPrintable(controller.lastError()));
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("embeddedWindow"));
        QVERIFY(controller.rootObject()->setProperty("count", 5));

        // Destroying the outgoing root destroys the only window in the
        // process. If that reaches QGuiApplication's quit-on-last-window-closed
        // path, the event loop is told to quit and the application dies on
        // every hot reload.
        QSignalSpy quitSpy(qApp, &QGuiApplication::lastWindowClosed);

        const QByteArray qml = R"(
            import QtQuick
            import QtQuick.Window
            Window {
                objectName: "reloadedWindow"
                visible: true
                width: 200
                height: 100
                property int count: 0
                function respinSaveState() { return { "count": count } }
                function respinRestoreState(state) { count = state.count ?? 0 }
            }
        )";
        // The reload must happen inside a running event loop, exactly as it does
        // in a real app: QCoreApplication::quit() only tears down a loop that is
        // actually running, so reloading outside one cannot observe the failure.
        QString error;
        bool applied = false;
        bool testAskedToQuit = false;
        QEventLoop loop;
        QTimer::singleShot(0, &loop, [&] {
            applied = controller.applyBundle(
                respin::encodeBundle(bundleWithWindow(QStringLiteral("win"), qml)),
                &error);
        });
        QTimer::singleShot(250, &loop, [&] {
            testAskedToQuit = true;
            loop.quit();
        });
        loop.exec();

        QVERIFY2(applied, qPrintable(error));
        QVERIFY2(
            testAskedToQuit,
            "the event loop exited on its own after a reload: something quit the "
            "application");
        QCOMPARE(quitSpy.size(), 0);
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("reloadedWindow"));
        QCOMPARE(controller.rootObject()->property("count").toInt(), 5);
    }

    // A refused or dropped connection used to end hot reload for the rest of the
    // session: nothing was connected to errorOccurred or disconnected, so the
    // failure was neither observed nor retried.
    void reconnectsAfterTheDevelopmentServerRestarts()
    {
        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        HelloCounter server;
        QVERIFY(server.listen());
        const auto port = server.port();

        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));
        controller.connectToDevelopmentServer(
            QStringLiteral("127.0.0.1"), port, QStringLiteral("test-token"));

        QTRY_COMPARE_WITH_TIMEOUT(server.helloCount(), 1, 5000);

        server.shutDown();
        HelloCounter restarted;
        QVERIFY2(
            restarted.listen(port),
            "could not rebind the port; the test cannot distinguish a failed "
            "reconnect from a failed rebind");

        // Backoff starts at 250ms and caps at 8s, so one reconnect lands well
        // inside this window.
        QTRY_COMPARE_WITH_TIMEOUT(restarted.helloCount(), 1, 15000);
    }

    // Stronger than "it rolled back": the incoming scene is compiled while the
    // running one is still up, so a bundle that does not compile -- what you get
    // every time a QML file is saved mid-edit -- never tears anything down.
    void aBundleThatDoesNotCompileNeverTouchesTheLiveScene()
    {
        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray good = R"(
            import QtQuick
            Item { objectName: "live" }
        )";
        QString error;
        QVERIFY2(
            controller.applyBundle(
                respin::encodeBundle(bundleWithMain(QStringLiteral("good"), good)),
                &error),
            qPrintable(error));

        const QPointer<QObject> live = controller.rootObject();
        QVERIFY(live);
        live->setObjectName(QStringLiteral("untouched"));

        QVERIFY(!controller.applyBundle(
            respin::encodeBundle(bundleWithMain(QStringLiteral("nope"), BrokenQml)),
            &error));

        QVERIFY2(
            live, "the running scene was destroyed for a bundle that never compiled");
        QCOMPARE(controller.rootObject(), live.data());
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("untouched"));
    }

    // The dev server resends the current bundle whenever an app reconnects.
    // Re-applying it used to delete the live bundle directory that
    // m_activeBundleDirectory still pointed at, permanently breaking rollback.
    void reapplyingTheActiveBundleIsANoOp()
    {
        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "stable"; property int count: 3 }
        )";
        const auto payload =
            respin::encodeBundle(bundleWithMain(QStringLiteral("same"), qml));

        QString error;
        QVERIFY2(controller.applyBundle(payload, &error), qPrintable(error));
        // A QPointer, not a raw one: the allocator readily hands the same
        // address back for a rebuilt scene, so comparing pointers passes even
        // when the old root was destroyed. The mark on the live object is the
        // second, independent witness.
        const QPointer<QObject> firstRoot = controller.rootObject();
        QVERIFY(firstRoot);
        firstRoot->setObjectName(QStringLiteral("touched-after-load"));

        QVERIFY2(controller.applyBundle(payload, &error), qPrintable(error));
        QVERIFY2(firstRoot, "the running scene was destroyed and rebuilt");
        QCOMPARE(
            controller.rootObject()->objectName(), QStringLiteral("touched-after-load"));

        // A later bad bundle must still find the session intact. It is rejected
        // by the compile probe before anything is torn down, so the same object
        // is still running and still carries the mark -- if the no-op had
        // deleted the live bundle directory, this is where the damage would show.
        QVERIFY(!controller.applyBundle(
            respin::encodeBundle(bundleWithMain(QStringLiteral("bad"), BrokenQml)),
            &error));
        QVERIFY2(firstRoot, "a bundle that never compiled destroyed the live scene");
        QCOMPARE(
            controller.rootObject()->objectName(), QStringLiteral("touched-after-load"));
    }

    // Two instances of one application previously shared
    // "<cache>/respin/bundles/<id>" and called removeRecursively() on each other's
    // live directories.
    void controllersDoNotShareBundleDirectories()
    {
        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "shared" }
        )";
        // Same id from both controllers: identical content is exactly when the
        // old deterministic path collided.
        const auto payload =
            respin::encodeBundle(bundleWithMain(QStringLiteral("collide"), qml));
        QString error;

        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        QQmlApplicationEngine firstEngine;
        respin::ReloadController first(firstEngine);
        QVERIFY(first.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));
        QVERIFY2(first.applyBundle(payload, &error), qPrintable(error));
        QCOMPARE(bundleCacheRoots().size(), 1);

        QQmlApplicationEngine secondEngine;
        respin::ReloadController second(secondEngine);
        QVERIFY(second.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));
        QVERIFY2(second.applyBundle(payload, &error), qPrintable(error));

        const auto roots = bundleCacheRoots();
        QCOMPARE(roots.size(), 2);
        QVERIFY2(
            roots.at(0) != roots.at(1),
            "two controllers were handed the same bundle cache directory");

        // The real regression: the second controller staging the same id must
        // not have destroyed what the first one is running from.
        QVERIFY(!first.applyBundle(
            respin::encodeBundle(bundleWithMain(QStringLiteral("bad"), BrokenQml)),
            &error));
        QVERIFY2(
            first.rootObject(),
            "rollback found nothing to load: the first controller's bundle "
            "directory was deleted by the second");
        QCOMPARE(first.rootObject()->objectName(), QStringLiteral("shared"));
    }

    void completedBundlesAreMarkedAndAbandonedRootsAreSwept()
    {
        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        // A pid above the kernel maximum can never be running, so this stands in
        // for a crashed instance. The flat directory is the 0.1.0 cache layout.
        // The third belongs to a process that is very much alive -- this one.
        const auto stale = bundleCacheBase() + QStringLiteral("/999999999-abcdef");
        const auto legacy = bundleCacheBase() + QStringLiteral("/deadbeefdeadbeef");
        const auto live = bundleCacheBase() + QLatin1Char('/')
            + QString::number(QCoreApplication::applicationPid())
            + QStringLiteral("-inuse");
        QVERIFY(writeFileAt(stale + QStringLiteral("/marker"), "x"));
        QVERIFY(writeFileAt(legacy + QStringLiteral("/marker"), "x"));
        QVERIFY(writeFileAt(live + QStringLiteral("/marker"), "x"));

        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "swept" }
        )";
        QString error;
        QVERIFY2(
            controller.applyBundle(
                respin::encodeBundle(bundleWithMain(QStringLiteral("sweeper"), qml)),
                &error),
            qPrintable(error));

        QVERIFY2(!QFileInfo::exists(stale), "a dead instance's cache root survived");
        QVERIFY2(
            !QFileInfo::exists(legacy), "a pre-0.2.0 flat bundle directory survived");
        // Telling "abandoned" from "busy" is the whole point: deleting a running
        // instance's root is the bug the sweep must not reintroduce.
        QVERIFY2(
            QFileInfo::exists(live),
            "the sweep deleted a cache root belonging to a live process");

        auto created = bundleCacheRoots();
        created.removeAll(QFileInfo(live).fileName());
        QCOMPARE(created.size(), 1);
        const auto bundleDirectory = bundleCacheBase() + QLatin1Char('/')
            + created.constFirst() + QStringLiteral("/sweeper");
        QVERIFY2(
            isMarkedComplete(bundleDirectory),
            "an activated bundle directory has no completion marker");
    }

    // A directory left behind by a run that died mid-stage has no marker. Trust
    // it and the app loads half a scene.
    void unmarkedBundleDirectoryIsRestagedRatherThanTrusted()
    {
        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        QQmlApplicationEngine engine;
        respin::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        QString error;
        QVERIFY(controller.applyBundle(
            respin::encodeBundle(bundleWithMain(
                QStringLiteral("first"), "import QtQuick\nItem { objectName: \"one\" }")),
            &error));

        const auto created = bundleCacheRoots();
        QCOMPARE(created.size(), 1);
        const auto cacheRoot =
            bundleCacheBase() + QLatin1Char('/') + created.constFirst();

        // Plant a partial write for a bundle the controller is about to receive:
        // right path, broken contents, no marker.
        QVERIFY(writeFileAt(
            cacheRoot + QStringLiteral("/partial/qt/qml/com/example/Test/Main.qml"),
            BrokenQml));
        QVERIFY(!isMarkedComplete(cacheRoot + QStringLiteral("/partial")));

        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "restaged" }
        )";
        QVERIFY2(
            controller.applyBundle(
                respin::encodeBundle(bundleWithMain(QStringLiteral("partial"), qml)),
                &error),
            qPrintable(error));
        QVERIFY2(
            controller.rootObject()
                && controller.rootObject()->objectName() == QStringLiteral("restaged"),
            "the unmarked directory was loaded instead of being restaged");
        QVERIFY(isMarkedComplete(cacheRoot + QStringLiteral("/partial")));
    }
};

QTEST_MAIN(RuntimeTests)
#include "runtime_tests.moc"
