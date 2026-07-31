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
                }),
            decoded, &error));
        QVERIFY(error.contains(QStringLiteral("duplicate")));
    }
};

QTEST_APPLESS_MAIN(ProtocolTests)
#include "tst_protocol.moc"
