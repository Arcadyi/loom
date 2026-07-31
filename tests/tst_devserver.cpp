#include "devserver.h"

#include <loom/protocol.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {

ApplicationDefinition testApplication()
{
    return ApplicationDefinition{
        .name = QStringLiteral("Test"),
        .target = QStringLiteral("Test"),
        .id = QStringLiteral("com.example.test"),
        .uri = QStringLiteral("com.example.Test"),
        .entry = QStringLiteral("Main"),
        .qmlRoots = {QStringLiteral("qml")},
        .assetRoots = {},
        .platforms = {QStringLiteral("desktop")},
    };
}

bool writeProjectQml(const QTemporaryDir &project)
{
    if (!QDir().mkpath(project.filePath(QStringLiteral("qml"))))
        return false;
    QFile main(project.filePath(QStringLiteral("qml/Main.qml")));
    if (!main.open(QIODevice::WriteOnly))
        return false;
    return main.write("import QtQuick\nItem {}\n") > 0;
}

// Stands in for the qmldir qt_add_qml_module writes into the build tree. The
// "prefer" line is the point: it redirects the engine to the compiled-in copy.
bool writeGeneratedQmldir(const QTemporaryDir &build, const QString &uriPath)
{
    if (!QDir().mkpath(build.filePath(uriPath)))
        return false;
    QFile qmldir(build.filePath(uriPath + QStringLiteral("/qmldir")));
    if (!qmldir.open(QIODevice::WriteOnly))
        return false;
    return qmldir.write(
               "module com.example.Test\n"
               "typeinfo Test.qmltypes\n"
               "prefer :/qt/qml/com/example/Test/\n"
               "Main 254.0 Main.qml\n"
               "singleton Theme 254.0 Theme.qml\n"
               "depends QtQuick\n")
        > 0;
}

// Deliberately not a QObject: the readyRead handler uses the socket itself as
// its context object, so this needs no moc and can live in an anonymous
// namespace next to the tests that use it.
class ClientProbe {
public:
    explicit ClientProbe(const quint16 port)
    {
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [this] {
            buffer.append(socket.readAll());
            while (true) {
                loom::Frame frame;
                QString error;
                if (!loom::takeFrame(buffer, frame, &error)) {
                    if (!error.isEmpty())
                        framingError = error;
                    break;
                }
                frames.append(frame);
            }
        });
        socket.connectToHost(QHostAddress::LocalHost, port);
    }

    bool waitForConnected()
    {
        return socket.waitForConnected(3000);
    }

    void sendHello(const QString &token, int version, const QString &currentBundle = {})
    {
        const QJsonObject hello{
            {QStringLiteral("version"), version},
            {QStringLiteral("token"), token},
            {QStringLiteral("applicationId"), QStringLiteral("probe")},
            {QStringLiteral("currentBundle"), currentBundle},
        };
        socket.write(
            loom::encodeFrame(
                loom::MessageType::Hello,
                QJsonDocument(hello).toJson(QJsonDocument::Compact)));
    }

    // Bypasses encodeFrame, which refuses to build an oversized frame at all.
    void sendOversizedHeader(const qsizetype declaredSize)
    {
        QByteArray header;
        header.resize(4);
        qToBigEndian(
            static_cast<quint32>(declaredSize), reinterpret_cast<uchar *>(header.data()));
        header.append(static_cast<char>(loom::MessageType::Hello));
        socket.write(header);
    }

    bool has(const loom::MessageType type) const
    {
        return std::any_of(frames.cbegin(), frames.cend(), [type](const auto &frame) {
            return frame.type == type;
        });
    }

    bool isDisconnected() const
    {
        return socket.state() != QAbstractSocket::ConnectedState;
    }

    QTcpSocket socket;
    QByteArray buffer;
    QList<loom::Frame> frames;
    QString framingError;
};

// Short enough that the heartbeat tests do not dominate the fast suite; the
// production defaults are 5s/20s.
constexpr int HeartbeatIntervalMs = 150;
constexpr int HeartbeatTimeoutMs = 600;

} // namespace

class DevServerTests final : public QObject {
    Q_OBJECT

private slots:
    void nativeSourceChangeEmitsRebuildSignal()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(QDir().mkpath(project.filePath(QStringLiteral("src"))));

        QFile source(project.filePath(QStringLiteral("src/main.cpp")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("int main() { return 0; }\n");
        source.close();

        DevServer server(project.path(), testApplication());
        QSignalSpy rebuildSpy(&server, &DevServer::nativeFilesChanged);

        QVERIFY(source.open(QIODevice::Append));
        source.write("// changed\n");
        source.close();

        QTRY_COMPARE_WITH_TIMEOUT(rebuildSpy.size(), 1, 3000);
    }

    void reloadTokenIsFullWidthAndUnpredictable()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid() && second.isValid());
        DevServer serverA(first.path(), testApplication());
        DevServer serverB(second.path(), testApplication());

        // 32 bytes of hex. A narrower token means entropy was thrown away.
        QCOMPARE(serverA.token().size(), 64);
        QCOMPARE(
            serverA.token(),
            serverA.token().toLower()); // hex, so no stray characters
        QVERIFY(QByteArray::fromHex(serverA.token().toLatin1()).size() == 32);

        // Every byte position must be capable of varying: the previous
        // implementation kept only the low byte of each generated word.
        QVERIFY2(
            serverA.token() != serverB.token(),
            "two dev servers produced the same reload token");
    }

    void authenticatedClientReceivesTheBundle()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QString error;
        QVERIFY2(server.start(&error), qPrintable(error));

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion);

        QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Bundle), 3000);
        QCOMPARE(server.clientCount(), 1);
    }

    // Without the module declaration, a development scene resolves types
    // differently from the compiled build -- a `pragma Singleton` type loads as
    // an ordinary component. With the "prefer" line left in, the engine loads
    // the compiled-in copy instead and every reload is silently a no-op
    // (measured: a live edit kept showing the colour compiled at startup).
    void bundleCarriesTheModuleQmldirWithoutPrefer()
    {
        QTemporaryDir project;
        QTemporaryDir build;
        QVERIFY(project.isValid() && build.isValid() && writeProjectQml(project));
        const auto uriPath = QStringLiteral("com/example/Test");
        QVERIFY(writeGeneratedQmldir(build, uriPath));

        DevServer server(project.path(), testApplication(), build.path());
        QString error;
        QVERIFY2(server.start(&error), qPrintable(error));

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion);
        QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Bundle), 3000);

        const auto frame =
            *std::find_if(probe.frames.cbegin(), probe.frames.cend(), [](const auto &f) {
                return f.type == loom::MessageType::Bundle;
            });
        loom::Bundle bundle;
        QVERIFY2(loom::decodeBundle(frame.payload, bundle, &error), qPrintable(error));

        const auto qmldirPath =
            QStringLiteral("qt/qml/") + uriPath + QStringLiteral("/qmldir");
        const auto entry = std::find_if(
            bundle.files.cbegin(), bundle.files.cend(),
            [&qmldirPath](const auto &file) { return file.path == qmldirPath; });
        QVERIFY2(
            entry != bundle.files.cend(),
            "the module's qmldir was not bundled, so singletons cannot resolve");

        const auto contents = QString::fromUtf8(entry->contents);
        QVERIFY2(
            !contents.contains(QStringLiteral("prefer ")),
            "the bundled qmldir still redirects the engine to the compiled copy");
        QVERIFY2(
            contents.contains(QStringLiteral("singleton Theme")),
            "the singleton declaration was lost");
        QVERIFY2(
            !contents.contains(QStringLiteral("typeinfo ")),
            "the bundled qmldir references a .qmltypes file that is not bundled");

        // 32 hex characters. The cache reuses a directory whenever the id
        // matches, so a truncated id is a correctness cliff, not a cosmetic one.
        QCOMPARE(bundle.id.size(), 32);
    }

    // A project with no build tree must still work; the qmldir is an addition,
    // not a requirement.
    void bundleWithoutABuildTreeOmitsTheQmldir()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion);
        QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Bundle), 3000);

        const auto frame =
            *std::find_if(probe.frames.cbegin(), probe.frames.cend(), [](const auto &f) {
                return f.type == loom::MessageType::Bundle;
            });
        loom::Bundle bundle;
        QString error;
        QVERIFY2(loom::decodeBundle(frame.payload, bundle, &error), qPrintable(error));
        QCOMPARE(bundle.files.size(), 1);
    }

    // Ping was answered by the runtime but never sent by anyone, and neither
    // side had a timeout: a half-open connection left loom dev reporting
    // "application connected" while every reload went nowhere.
    void authenticatedClientIsPinged()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        server.setHeartbeat(HeartbeatIntervalMs, HeartbeatTimeoutMs);
        QVERIFY(server.start());

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion);
        QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Bundle), 3000);

        QTRY_VERIFY_WITH_TIMEOUT(
            probe.has(loom::MessageType::Ping), HeartbeatIntervalMs + 3000);
        // Answering keeps it alive; the drop path is the next test.
        probe.socket.write(loom::encodeFrame(loom::MessageType::Ping, {}));
        QTest::qWait(HeartbeatIntervalMs * 2);
        QCOMPARE(server.clientCount(), 1);
        QVERIFY(!probe.isDisconnected());
    }

    void silentClientIsDroppedByTheHeartbeat()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        server.setHeartbeat(HeartbeatIntervalMs, HeartbeatTimeoutMs);
        QVERIFY(server.start());
        QSignalSpy logSpy(&server, &DevServer::logMessage);

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion);
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);

        // Authenticates, then never speaks again. It stays connected at the TCP
        // level, which is exactly what makes this invisible without a heartbeat.
        QTRY_COMPARE_WITH_TIMEOUT(
            server.clientCount(), 0, HeartbeatTimeoutMs + HeartbeatIntervalMs + 5000);
        QVERIFY2(
            std::any_of(
                logSpy.cbegin(), logSpy.cend(),
                [](const QList<QVariant> &call) {
                    return call.at(0).toString().contains(QStringLiteral("no response"));
                }),
            "the client was dropped without saying why");
    }

    void wrongTokenIsRejected()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(QString(64, QLatin1Char('0')), loom::ProtocolVersion);

        QTRY_VERIFY_WITH_TIMEOUT(probe.isDisconnected(), 3000);
        QVERIFY2(
            !probe.has(loom::MessageType::Bundle),
            "an unauthenticated client was sent the bundle");
    }

    // loom_runtime is a static library baked into the application, so an
    // upgraded CLI routinely meets an older runtime. That used to surface as
    // "rejected an unauthorized reload client" and then silence on both sides.
    void versionMismatchIsExplainedRatherThanSilent()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        probe.sendHello(server.token(), loom::ProtocolVersion + 99);

        QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Error), 3000);
        const auto frame =
            *std::find_if(probe.frames.cbegin(), probe.frames.cend(), [](const auto &f) {
                return f.type == loom::MessageType::Error;
            });
        const auto message = QJsonDocument::fromJson(frame.payload)
                                 .object()
                                 .value(QStringLiteral("message"))
                                 .toString();
        QVERIFY2(
            message.contains(QStringLiteral("version mismatch")),
            qPrintable(QStringLiteral("unhelpful error text: %1").arg(message)));
        QTRY_VERIFY_WITH_TIMEOUT(probe.isDisconnected(), 3000);
    }

    void silentClientIsDroppedAfterTheAuthenticationDeadline()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 2000);

        // Says nothing at all: without a deadline this connection sits in the
        // client table forever, and a loop of them is free memory pressure.
        QTRY_VERIFY_WITH_TIMEOUT(
            probe.isDisconnected(), DevServer::AuthenticationTimeoutMs + 3000);
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
    }

    void oversizePreAuthFrameIsRefused()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());
        QSignalSpy logSpy(&server, &DevServer::logMessage);

        ClientProbe probe(server.port());
        QVERIFY(probe.waitForConnected());
        // Declares 32 MiB and then sends nothing. Without the pre-auth limit the
        // server buffers whatever arrives until the frame is complete.
        probe.sendOversizedHeader(32 * 1024 * 1024);

        // Asserted on the logged reason, not merely on the disconnect: the
        // authentication deadline would also close this socket a second later,
        // so a bare "was it dropped?" check passes with the limit removed.
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(
                logSpy.cbegin(), logSpy.cend(),
                [](const QList<QVariant> &call) {
                    return call.at(0).toString().contains(QStringLiteral("frame size"));
                }),
            DevServer::AuthenticationTimeoutMs - 500);
        QTRY_VERIFY_WITH_TIMEOUT(probe.isDisconnected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
    }

    void connectionsBeyondTheClientCapAreRefused()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        std::vector<std::unique_ptr<ClientProbe>> probes;
        for (int index = 0; index < DevServer::MaximumClients + 4; ++index) {
            auto probe = std::make_unique<ClientProbe>(server.port());
            probe->waitForConnected();
            probe->sendHello(server.token(), loom::ProtocolVersion);
            probes.push_back(std::move(probe));
        }
        QTest::qWait(500);
        QVERIFY2(
            server.clientCount() <= DevServer::MaximumClients,
            qPrintable(QStringLiteral("client table grew to %1, past the cap of %2")
                           .arg(server.clientCount())
                           .arg(DevServer::MaximumClients)));
    }

    void disconnectingClientIsPrunedFromTheTable()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        {
            ClientProbe probe(server.port());
            QVERIFY(probe.waitForConnected());
            probe.sendHello(server.token(), loom::ProtocolVersion);
            QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);
        }
        // The table is keyed by a raw socket pointer, so a missed prune leaves a
        // dangling key that the next rebuildBundle() would write to.
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
    }

    void clientThatAlreadyHasTheBundleIsNotResent()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid() && writeProjectQml(project));
        DevServer server(project.path(), testApplication());
        QVERIFY(server.start());

        // Learn the current bundle id the way a restarted application would:
        // connect once, receive it, then reconnect claiming to have it.
        QString bundleId;
        {
            ClientProbe probe(server.port());
            QVERIFY(probe.waitForConnected());
            probe.sendHello(server.token(), loom::ProtocolVersion);
            QTRY_VERIFY_WITH_TIMEOUT(probe.has(loom::MessageType::Bundle), 3000);
            const auto frame = *std::find_if(
                probe.frames.cbegin(), probe.frames.cend(),
                [](const auto &f) { return f.type == loom::MessageType::Bundle; });
            loom::Bundle bundle;
            QString error;
            QVERIFY2(
                loom::decodeBundle(frame.payload, bundle, &error), qPrintable(error));
            bundleId = bundle.id;
        }
        QVERIFY(!bundleId.isEmpty());

        ClientProbe rejoin(server.port());
        QVERIFY(rejoin.waitForConnected());
        rejoin.sendHello(server.token(), loom::ProtocolVersion, bundleId);
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);
        QTest::qWait(300);

        QVERIFY2(
            !rejoin.has(loom::MessageType::Bundle),
            "the server resent a bundle the application said it already had");
        QVERIFY(!rejoin.isDisconnected());
    }
};

QTEST_GUILESS_MAIN(DevServerTests)
#include "tst_devserver.moc"
