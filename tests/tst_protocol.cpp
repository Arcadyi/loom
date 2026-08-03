#include <loom/protocol.h>

#include <QCryptographicHash>
#include <QtTest>

class ProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void frameWaitsForCompletePayload()
    {
        const auto encoded =
            loom::encodeFrame(loom::MessageType::Ping, QByteArrayLiteral("hello"));
        QByteArray partial = encoded.first(6);
        loom::Frame frame;
        QVERIFY(!loom::takeFrame(partial, frame));
        partial.append(encoded.sliced(6));
        QVERIFY(loom::takeFrame(partial, frame));
        QCOMPARE(frame.type, loom::MessageType::Ping);
        QCOMPARE(frame.payload, QByteArrayLiteral("hello"));
        QVERIFY(partial.isEmpty());
    }

    // Clearing the buffer on a bad length pretended to recover while throwing
    // away whatever valid frames had already been pipelined behind it. There is
    // no resync marker, so the only correct response is a fatal error.
    void framingErrorIsFatalAndPreservesTheBuffer()
    {
        QByteArray buffer;
        buffer.resize(4);
        qToBigEndian<quint32>(0, reinterpret_cast<uchar *>(buffer.data()));
        buffer.append(
            loom::encodeFrame(loom::MessageType::Ping, QByteArrayLiteral("queued")));
        const auto originalSize = buffer.size();

        loom::Frame frame;
        QString error;
        QVERIFY(!loom::takeFrame(buffer, frame, &error));
        QVERIFY2(!error.isEmpty(), "a zero-length frame was accepted");
        QCOMPARE(buffer.size(), originalSize); // nothing consumed, nothing discarded
    }

    void rejectsOversizeDeclaredFrame()
    {
        QByteArray buffer;
        buffer.resize(4);
        qToBigEndian<quint32>(
            static_cast<quint32>(loom::MaximumFrameSize + 1),
            reinterpret_cast<uchar *>(buffer.data()));

        loom::Frame frame;
        QString error;
        QVERIFY(!loom::takeFrame(buffer, frame, &error));
        QVERIFY(error.contains(QStringLiteral("frame size")));
    }

    // The pre-auth limit is what stops an unauthenticated peer from making the
    // receiver buffer a declared 64 MiB before its type is even known.
    void preAuthLimitRejectsFramesTheFullLimitAccepts()
    {
        const QByteArray payload(loom::MaximumPreAuthFrameSize + 1, 'x');
        auto encoded = loom::encodeFrame(loom::MessageType::Hello, payload);

        loom::Frame frame;
        QString error;
        auto restricted = encoded;
        QVERIFY(
            !loom::takeFrame(restricted, frame, &error, loom::MaximumPreAuthFrameSize));
        QVERIFY(error.contains(QStringLiteral("frame size")));

        error.clear();
        QVERIFY2(loom::takeFrame(encoded, frame, &error), qPrintable(error));
        QCOMPARE(frame.payload.size(), payload.size());
    }

    void rejectsUnknownMessageType()
    {
        auto encoded = loom::encodeFrame(loom::MessageType::Ping, QByteArrayLiteral("x"));
        encoded[4] = static_cast<char>(99);

        loom::Frame frame;
        QString error;
        QVERIFY(!loom::takeFrame(encoded, frame, &error));
        QVERIFY(error.contains(QStringLiteral("message type")));
    }

    void bundleRoundTrip()
    {
        const QByteArray contents = "import QtQuick\nItem {}\n";
        loom::Bundle source{
            .id = QStringLiteral("abc123"),
            .files =
                {
                    loom::BundleFile{
                        .path = QStringLiteral("qt/qml/dev/example/App/Main.qml"),
                        .contents = contents,
                        .sha256 = QCryptographicHash::hash(
                            contents, QCryptographicHash::Sha256),
                    },
                },
            .capabilities = {},
        };
        loom::Bundle decoded;
        QString error;
        QVERIFY2(
            loom::decodeBundle(loom::encodeBundle(source), decoded, &error),
            qPrintable(error));
        QCOMPARE(decoded.id, source.id);
        QCOMPARE(decoded.files.size(), 1);
        QCOMPARE(decoded.files.first().contents, contents);
    }

    // The path travels with the document because the runtime stages the bytes
    // elsewhere and cannot otherwise tell what a relative `iconRoot` is
    // relative to.
    void designRoundTrip()
    {
        const loom::Design source{
            .path = QStringLiteral("/home/dev/app/design/tokens.json"),
            .tokens = R"({"iconRoot": "assets/icons"})",
        };
        loom::Design decoded;
        QString error;
        QVERIFY2(
            loom::decodeDesign(loom::encodeDesign(source), decoded, &error),
            qPrintable(error));
        QCOMPARE(decoded.path, source.path);
        QCOMPARE(decoded.tokens, source.tokens);
    }

    void rejectsMalformedDesignPayloads()
    {
        loom::Design decoded;
        QString error;
        // Not CBOR at all -- e.g. the raw JSON an older server would have sent.
        QVERIFY(!loom::decodeDesign(R"({"colors": {}})", decoded, &error));
        QVERIFY(!error.isEmpty());

        // CBOR, but carrying no token document.
        QCborMap noTokens;
        noTokens.insert(QStringLiteral("version"), loom::ProtocolVersion);
        noTokens.insert(QStringLiteral("path"), QStringLiteral("/tmp/tokens.json"));
        QVERIFY(!loom::decodeDesign(QCborValue(noTokens).toCbor(), decoded, &error));

        // Over the size cap, which keeps a stray large file from being pushed
        // into the running process as configuration.
        QVERIFY(!loom::decodeDesign(
            loom::encodeDesign(
                loom::Design{
                    .path = QStringLiteral("/tmp/tokens.json"),
                    .tokens = QByteArray(loom::MaximumDesignSize + 1, 'x'),
                }),
            decoded, &error));
        QVERIFY(error.contains(QStringLiteral("limit")));
    }

    void rejectsUnsafePaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("absolute") << QStringLiteral("/qt/qml/App/Main.qml");
        QTest::newRow("parent") << QStringLiteral("qt/qml/App/../secret");
        QTest::newRow("backslash") << QStringLiteral("qt\\qml\\App\\Main.qml");
        QTest::newRow("outside") << QStringLiteral("assets/Main.qml");
    }

    void rejectsUnsafePaths()
    {
        QFETCH(QString, path);
        QVERIFY(!loom::isSafeBundlePath(path));
    }

    void rejectsUnsafeBundleId()
    {
        const QByteArray contents = "Item {}";
        auto bundle = loom::Bundle{
            .id = QStringLiteral("../../outside"),
            .files =
                {
                    loom::BundleFile{
                        .path = QStringLiteral("qt/qml/App/Main.qml"),
                        .contents = contents,
                        .sha256 = QCryptographicHash::hash(
                            contents, QCryptographicHash::Sha256),
                    },
                },
            .capabilities = {},
        };
        loom::Bundle decoded;
        QString error;
        QVERIFY(!loom::decodeBundle(loom::encodeBundle(bundle), decoded, &error));
        QVERIFY(error.contains(QStringLiteral("id")));
    }

    void rejectsDuplicateBundlePath()
    {
        const QByteArray contents = "Item {}";
        const auto file = loom::BundleFile{
            .path = QStringLiteral("qt/qml/App/Main.qml"),
            .contents = contents,
            .sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256),
        };
        loom::Bundle decoded;
        QString error;
        QVERIFY(!loom::decodeBundle(
            loom::encodeBundle(
                loom::Bundle{
                    .id = QStringLiteral("duplicate"),
                    .files = {file, file},
                    .capabilities = {},
                }),
            decoded, &error));
        QVERIFY(error.contains(QStringLiteral("duplicate")));
    }

    void styleEditRoundTrips()
    {
        const loom::StyleEdit sent{
            .path = QStringLiteral("qt/qml/com/example/App/Main.qml"),
            .line = 12,
            .column = 5,
            .oldStyle = QStringLiteral("bg-surface"),
            .newStyle = QStringLiteral("bg-accent rounded-lg"),
        };
        loom::StyleEdit received;
        QString error;
        QVERIFY2(
            loom::decodeStyleEdit(loom::encodeStyleEdit(sent), received, &error),
            qPrintable(error));
        QCOMPARE(received.path, sent.path);
        QCOMPARE(received.line, sent.line);
        QCOMPARE(received.column, sent.column);
        QCOMPARE(received.oldStyle, sent.oldStyle);
        QCOMPARE(received.newStyle, sent.newStyle);
    }

    void styleEditRefusesUnusablePositionsAndPaths()
    {
        loom::StyleEdit decoded;
        QString error;

        // QQmlData reports 0 for an object no document created, which no source
        // edit can reach.
        loom::StyleEdit noPosition{
            .path = QStringLiteral("qt/qml/App/Main.qml"),
            .line = 0,
            .column = 0,
            .oldStyle = {},
            .newStyle = {}};
        QVERIFY(
            !loom::decodeStyleEdit(loom::encodeStyleEdit(noPosition), decoded, &error));
        QVERIFY(error.contains(QStringLiteral("no source position")));

        // The path names a project file, so it gets the same treatment a bundle
        // path does: no traversal out of the tree.
        loom::StyleEdit escaping{
            .path = QStringLiteral("../../etc/passwd"),
            .line = 1,
            .column = 1,
            .oldStyle = {},
            .newStyle = {}};
        QVERIFY(!loom::decodeStyleEdit(loom::encodeStyleEdit(escaping), decoded, &error));
        QVERIFY(error.contains(QStringLiteral("unusable path")));
    }

    // Capability negotiation exists because takeFrame() treats an unknown type
    // as a *fatal* framing error: a new runtime meeting an older server must be
    // able to tell, or it drops hot reload for the rest of the session by
    // sending one frame. Both halves of that matter, so both are pinned.
    void bundleCapabilitiesAreAdditive()
    {
        loom::Bundle sent{
            .id = QStringLiteral("abc"),
            .files = {loom::BundleFile{QStringLiteral("qt/qml/App/Main.qml"), "x", {}}},
            .capabilities = {QString::fromLatin1(loom::CapabilityStyleEdit)},
        };
        loom::Bundle received;
        QString error;
        QVERIFY2(
            loom::decodeBundle(loom::encodeBundle(sent), received, &error),
            qPrintable(error));
        QCOMPARE(received.capabilities, sent.capabilities);

        // A bundle from a server that predates the field decodes to no
        // capabilities, which is exactly "do not offer the feature".
        sent.capabilities.clear();
        QVERIFY(loom::decodeBundle(loom::encodeBundle(sent), received, &error));
        QVERIFY(received.capabilities.isEmpty());
    }

    void anUnknownMessageTypeIsStillFatal()
    {
        QVERIFY(loom::isKnownMessageType(quint8(loom::MessageType::StyleEdit)));
        QVERIFY(!loom::isKnownMessageType(200));
    }
};

QTEST_APPLESS_MAIN(ProtocolTests)
#include "tst_protocol.moc"
