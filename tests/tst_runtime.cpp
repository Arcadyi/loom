#include <loom/loom.h>
#include <loom/protocol.h>
#include <loom/reloadcontroller.h>

// Design token reload lands in the process-wide registry, which has no public
// membership query; the styling tests reach it the same way.
#include "tokens/loomtokenregistry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

loom::Bundle bundleAt(const QString &id, const QString &path, const QByteArray &contents)
{
    return loom::Bundle{
        .id = id,
        .files =
            {
                loom::BundleFile{
                    .path = path,
                    .contents = contents,
                    .sha256 =
                        QCryptographicHash::hash(contents, QCryptographicHash::Sha256),
                },
            },
    };
}

// applyDesign takes an encoded loom::Design -- the document plus the path it
// has in the project, which a relative `iconRoot` resolves against.
QByteArray encodedDesign(const QByteArray &tokens, const QString &path = QString())
{
    return loom::encodeDesign(loom::Design{.path = path, .tokens = tokens});
}

loom::Bundle
bundleWithFiles(const QString &id, const QList<QPair<QString, QByteArray>> &files)
{
    loom::Bundle bundle{.id = id, .files = {}};
    for (const auto &[path, contents] : files) {
        bundle.files.append(
            loom::BundleFile{
                .path = QStringLiteral("qt/qml/com/example/Test/") + path,
                .contents = contents,
                .sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256),
            });
    }
    return bundle;
}

loom::Bundle bundleWithMain(const QString &id, const QByteArray &contents)
{
    return bundleAt(id, QStringLiteral("qt/qml/com/example/Test/Main.qml"), contents);
}

loom::Bundle bundleWithWindow(const QString &id, const QByteArray &contents)
{
    return bundleAt(
        id, QStringLiteral("qt/qml/com/example/WindowTest/WindowMain.qml"), contents);
}

QString bundleCacheBase()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/loom/bundles");
}

// Each ReloadController owns a "<pid>-XXXXXX" directory under the cache base.
QStringList bundleCacheRoots()
{
    return QDir(bundleCacheBase())
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
}

// Gives one test function a cache base nobody else writes to. Without this the
// directory bookkeeping below counts roots created by the other tests in this
// process, and by any concurrent run sharing the cache -- which is precisely
// the multi-process situation these tests are about, so it happens readily.
//
// The isolation runs through the application name rather than XDG_CACHE_HOME
// because QStandardPaths only reads that variable on Unix-but-not-Apple: on
// macOS the cache is under ~/Library/Caches and the override was silently
// ignored, so every test function shared one base and counted the roots the
// previous one deliberately left behind. CacheLocation ends in
// <organization>/<application> on all three desktop platforms, which makes a
// unique application name the one lever that isolates everywhere. Test mode
// keeps the directories under ~/.qttest instead of the real user cache.
class ScopedCacheHome {
public:
    ScopedCacheHome()
        : m_previous(QCoreApplication::applicationName())
    {
        static QAtomicInt counter;
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("loom-test-%1-%2")
                                                 .arg(QCoreApplication::applicationPid())
                                                 .arg(counter.fetchAndAddOrdered(1)));
        m_base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        m_valid = !m_base.isEmpty() && QDir().mkpath(m_base);
    }
    ~ScopedCacheHome()
    {
        // Nothing else ever names this directory again, and the tests plant
        // roots that outlive the sweep on purpose.
        if (m_valid)
            QDir(m_base).removeRecursively();
        QCoreApplication::setApplicationName(m_previous);
    }

    ScopedCacheHome(const ScopedCacheHome &) = delete;
    ScopedCacheHome &operator=(const ScopedCacheHome &) = delete;

    bool isValid() const
    {
        return m_valid;
    }

private:
    QString m_previous;
    QString m_base;
    bool m_valid = false;
};

bool isMarkedComplete(const QString &bundleDirectory)
{
    return QFileInfo::exists(bundleDirectory + QStringLiteral("/.loom-complete"));
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
                    loom::Frame frame;
                    QString error;
                    while (loom::takeFrame(buffer, frame, &error)) {
                        if (frame.type == loom::MessageType::Hello)
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
        loom::ReloadController controller(engine);
        QVERIFY2(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")),
            qPrintable(controller.lastError()));
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("embedded"));
        QVERIFY(controller.rootObject()->setProperty("count", 42));
        QVariant exportedState;
        QVERIFY(
            QMetaObject::invokeMethod(
                controller.rootObject(), "loomSaveState", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, exportedState)));
        QCOMPARE(exportedState.toMap().value(QStringLiteral("count")).toInt(), 42);

        const QByteArray qml = R"(
            import QtQuick
            Item {
                objectName: "reloaded"
                property int count: 0
                function loomSaveState() { return { "count": count } }
                function loomRestoreState(state) { count = state.count }
            }
        )";
        QString error;
        const auto payload =
            loom::encodeBundle(bundleWithMain(QStringLiteral("valid"), qml));
        QVERIFY2(controller.applyBundle(payload, &error), qPrintable(error));
        QCoreApplication::processEvents();
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("reloaded"));
        QCOMPARE(controller.rootObject()->property("count").toInt(), 42);
    }

    // The point of applying design tokens outside the bundle path: the scene
    // object is never replaced, so nothing on screen loses its state. A user
    // typing in a field while tweaking a colour keeps what they typed.
    void designTokensApplyWithoutRebuildingTheScene()
    {
        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY2(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")),
            qPrintable(controller.lastError()));
        QObject *const sceneBefore = controller.rootObject();
        QVERIFY(sceneBefore);
        QVERIFY(controller.rootObject()->setProperty("count", 7));

        QString error;
        QVERIFY2(
            controller.applyDesign(
                encodedDesign(
                    R"({"schemaVersion": 2, "tokens": {"colors": {"brand": "#7c5cff"}}})"),
                &error),
            qPrintable(error));
        QCoreApplication::processEvents();

        QCOMPARE(controller.rootObject(), sceneBefore);
        QCOMPARE(controller.rootObject()->property("count").toInt(), 7);
        QVERIFY(LoomTokenRegistry::instance()->hasColor(QStringLiteral("brand")));
    }

    // A half-typed design file arrives as readily as a finished one. It must
    // cost nothing: no scene teardown, and the previous tokens stay live.
    void malformedDesignTokensChangeNothing()
    {
        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY2(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")),
            qPrintable(controller.lastError()));
        QVERIFY(controller.applyDesign(encodedDesign(
            R"({"schemaVersion": 2, "tokens": {"colors": {"brand": "#7c5cff"}}})")));
        QObject *const sceneBefore = controller.rootObject();

        QString error;
        QVERIFY(
            !controller.applyDesign(encodedDesign(R"({"colors": {"brand": )"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(controller.rootObject(), sceneBefore);
        QVERIFY2(
            LoomTokenRegistry::instance()->hasColor(QStringLiteral("brand")),
            "a malformed design file discarded the tokens that were working");

        // And the size cap, which exists so a stray large file cannot be pushed
        // into the running process as configuration.
        QVERIFY(!controller.applyDesign(
            encodedDesign(QByteArray(loom::MaximumDesignSize + 1, 'x'))));
        QCOMPARE(controller.rootObject(), sceneBefore);

        // A payload that is not an encoded Design at all is rejected too.
        QVERIFY(!controller.applyDesign(R"({"colors": {"brand": "#7c5cff"}})"));
        QCOMPARE(controller.rootObject(), sceneBefore);
    }

    // Regression: the controller used to stage the received document into its
    // own cache directory and reload from there, so a relative `iconRoot`
    // resolved against the staging path. Every icon in a project using one
    // broke on the first design save under `loom dev`, while working in a
    // compiled build.
    void designIconRootResolvesAgainstTheProjectNotTheCache()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        const QString designPath =
            QDir(project.path()).filePath(QStringLiteral("tokens.json"));

        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY2(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")),
            qPrintable(controller.lastError()));

        QString error;
        QVERIFY2(
            controller.applyDesign(
                encodedDesign(
                    R"({"schemaVersion": 2, "iconRoot": "assets/icons"})", designPath),
                &error),
            qPrintable(error));

        const QUrl expected = QUrl::fromLocalFile(
            QDir(project.path()).filePath(QStringLiteral("assets/icons")));
        QCOMPARE(loom::iconRoot().adjusted(QUrl::StripTrailingSlash), expected);
    }

    void invalidQmlRollsBackToWorkingScene()
    {
        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray valid = R"(
            import QtQuick
            Item { objectName: "working"; property int count: 7 }
        )";
        QString error;
        QVERIFY(controller.applyBundle(
            loom::encodeBundle(bundleWithMain(QStringLiteral("working"), valid)),
            &error));
        QCOMPARE(controller.rootObject()->objectName(), QStringLiteral("working"));

        const QByteArray invalid = "import QtQuick\nItem { objectName: ";
        QVERIFY(!controller.applyBundle(
            loom::encodeBundle(bundleWithMain(QStringLiteral("broken"), invalid)),
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
        loom::ReloadController controller(engine);
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
                function loomSaveState() { return { "count": count } }
                function loomRestoreState(state) { count = state.count ?? 0 }
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
                loom::encodeBundle(bundleWithWindow(QStringLiteral("win"), qml)), &error);
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
        loom::ReloadController controller(engine);
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
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray good = R"(
            import QtQuick
            Item { objectName: "live" }
        )";
        QString error;
        QVERIFY2(
            controller.applyBundle(
                loom::encodeBundle(bundleWithMain(QStringLiteral("good"), good)), &error),
            qPrintable(error));

        const QPointer<QObject> live = controller.rootObject();
        QVERIFY(live);
        live->setObjectName(QStringLiteral("untouched"));

        QVERIFY(!controller.applyBundle(
            loom::encodeBundle(bundleWithMain(QStringLiteral("nope"), BrokenQml)),
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
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "stable"; property int count: 3 }
        )";
        const auto payload =
            loom::encodeBundle(bundleWithMain(QStringLiteral("same"), qml));

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
            loom::encodeBundle(bundleWithMain(QStringLiteral("bad"), BrokenQml)),
            &error));
        QVERIFY2(firstRoot, "a bundle that never compiled destroyed the live scene");
        QCOMPARE(
            controller.rootObject()->objectName(), QStringLiteral("touched-after-load"));
    }

    // Two instances of one application previously shared
    // "<cache>/loom/bundles/<id>" and called removeRecursively() on each other's
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
            loom::encodeBundle(bundleWithMain(QStringLiteral("collide"), qml));
        QString error;

        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        QQmlApplicationEngine firstEngine;
        loom::ReloadController first(firstEngine);
        QVERIFY(first.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));
        QVERIFY2(first.applyBundle(payload, &error), qPrintable(error));
        QCOMPARE(bundleCacheRoots().size(), 1);

        QQmlApplicationEngine secondEngine;
        loom::ReloadController second(secondEngine);
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
            loom::encodeBundle(bundleWithMain(QStringLiteral("bad"), BrokenQml)),
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
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        const QByteArray qml = R"(
            import QtQuick
            Item { objectName: "swept" }
        )";
        QString error;
        QVERIFY2(
            controller.applyBundle(
                loom::encodeBundle(bundleWithMain(QStringLiteral("sweeper"), qml)),
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
    // Editing a file behind a Loader used to cost the whole scene: the root was
    // deleted and rebuilt, which on a real application means the window goes
    // away and comes back on every keystroke that lands. Only the Loader's
    // contents have changed, so only the Loader is rebuilt.
    void aChangeBehindALoaderRebuildsOnlyTheLoader()
    {
        const QByteArray shell = R"(
            import QtQuick
            Item {
                objectName: "shell"
                property int visits: 0
                Loader { objectName: "boundary"; source: "Panel.qml" }
            }
        )";
        const auto panel = [](const char *name) {
            return QByteArray("import QtQuick\nItem { objectName: \"") + name + "\" }\n";
        };

        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        QString error;
        QVERIFY2(
            controller.applyBundle(
                loom::encodeBundle(bundleWithFiles(
                    QStringLiteral("one"),
                    {{QStringLiteral("Main.qml"), shell},
                     {QStringLiteral("Panel.qml"), panel("first")}})),
                &error),
            qPrintable(error));

        // Guarded, because a rebuilt scene can land on the address the old one
        // was freed from and a raw comparison would call that "not rebuilt".
        // A QPointer answers the question actually being asked.
        QPointer<QObject> root = controller.rootObject();
        QVERIFY(root);
        QVERIFY(root->setProperty("visits", 7));
        QPointer<QObject> boundary =
            root->findChild<QObject *>(QStringLiteral("boundary"));
        QVERIFY(boundary);
        QVERIFY2(
            boundary->property("item").value<QObject *>(),
            qPrintable(QStringLiteral("loader status %1")
                           .arg(boundary->property("status").toInt())));
        QCOMPARE(
            boundary->property("item").value<QObject *>()->objectName(),
            QStringLiteral("first"));

        // Same shell, different panel.
        QVERIFY2(
            controller.applyBundle(
                loom::encodeBundle(bundleWithFiles(
                    QStringLiteral("two"),
                    {{QStringLiteral("Main.qml"), shell},
                     {QStringLiteral("Panel.qml"), panel("second")}})),
                &error),
            qPrintable(error));

        QVERIFY2(root, "the scene was rebuilt for a change behind the Loader");
        QCOMPARE(controller.rootObject(), root.data());
        QCOMPARE(root->property("visits").toInt(), 7);
        QVERIFY(boundary);
        QCOMPARE(root->findChild<QObject *>(QStringLiteral("boundary")), boundary.data());
        QCOMPARE(
            boundary->property("item").value<QObject *>()->objectName(),
            QStringLiteral("second"));

        // The shell itself is not behind anything, so changing it still costs
        // the scene -- there is nothing smaller left to rebuild.
        const QByteArray editedShell =
            QByteArray(shell).replace("visits: 0", "visits: 1");
        QVERIFY2(
            controller.applyBundle(
                loom::encodeBundle(bundleWithFiles(
                    QStringLiteral("three"),
                    {{QStringLiteral("Main.qml"), editedShell},
                     {QStringLiteral("Panel.qml"), panel("second")}})),
                &error),
            qPrintable(error));
        QVERIFY2(!root, "the scene survived a change to the document holding it");
        QVERIFY(controller.rootObject());
        QCOMPARE(controller.rootObject()->property("visits").toInt(), 1);
    }

    void unmarkedBundleDirectoryIsRestagedRatherThanTrusted()
    {
        ScopedCacheHome cacheHome;
        QVERIFY(cacheHome.isValid());

        QQmlApplicationEngine engine;
        loom::ReloadController controller(engine);
        QVERIFY(
            controller.load(QStringLiteral("com.example.Test"), QStringLiteral("Main")));

        QString error;
        QVERIFY(controller.applyBundle(
            loom::encodeBundle(bundleWithMain(
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
                loom::encodeBundle(bundleWithMain(QStringLiteral("partial"), qml)),
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
#include "tst_runtime.moc"
