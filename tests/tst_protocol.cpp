#include <respin/protocol.h>

#include <QCryptographicHash>
#include <QtTest>

class ProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void frameWaitsForCompletePayload()
    {
        const auto encoded =
            respin::encodeFrame(respin::MessageType::Ping, QByteArrayLiteral("hello"));
        QByteArray partial = encoded.first(6);
        respin::Frame frame;
        QVERIFY(!respin::takeFrame(partial, frame));
        partial.append(encoded.sliced(6));
        QVERIFY(respin::takeFrame(partial, frame));
        QCOMPARE(frame.type, respin::MessageType::Ping);
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
            respin::encodeFrame(respin::MessageType::Ping, QByteArrayLiteral("queued")));
        const auto originalSize = buffer.size();

        respin::Frame frame;
        QString error;
        QVERIFY(!respin::takeFrame(buffer, frame, &error));
        QVERIFY2(!error.isEmpty(), "a zero-length frame was accepted");
        QCOMPARE(buffer.size(), originalSize); // nothing consumed, nothing discarded
    }

    void rejectsOversizeDeclaredFrame()
    {
        QByteArray buffer;
        buffer.resize(4);
        qToBigEndian<quint32>(
            static_cast<quint32>(respin::MaximumFrameSize + 1),
            reinterpret_cast<uchar *>(buffer.data()));

        respin::Frame frame;
        QString error;
        QVERIFY(!respin::takeFrame(buffer, frame, &error));
        QVERIFY(error.contains(QStringLiteral("frame size")));
    }

    // The pre-auth limit is what stops an unauthenticated peer from making the
    // receiver buffer a declared 64 MiB before its type is even known.
    void preAuthLimitRejectsFramesTheFullLimitAccepts()
    {
        const QByteArray payload(respin::MaximumPreAuthFrameSize + 1, 'x');
        auto encoded = respin::encodeFrame(respin::MessageType::Hello, payload);

        respin::Frame frame;
        QString error;
        auto restricted = encoded;
        QVERIFY(!respin::takeFrame(
            restricted, frame, &error, respin::MaximumPreAuthFrameSize));
        QVERIFY(error.contains(QStringLiteral("frame size")));

        error.clear();
        QVERIFY2(respin::takeFrame(encoded, frame, &error), qPrintable(error));
        QCOMPARE(frame.payload.size(), payload.size());
    }

    void rejectsUnknownMessageType()
    {
        auto encoded =
            respin::encodeFrame(respin::MessageType::Ping, QByteArrayLiteral("x"));
        encoded[4] = static_cast<char>(99);

        respin::Frame frame;
        QString error;
        QVERIFY(!respin::takeFrame(encoded, frame, &error));
        QVERIFY(error.contains(QStringLiteral("message type")));
    }

    void bundleRoundTrip()
    {
        const QByteArray contents = "import QtQuick\nItem {}\n";
        respin::Bundle source{
            .id = QStringLiteral("abc123"),
            .files =
                {
                    respin::BundleFile{
                        .path = QStringLiteral("qt/qml/dev/example/App/Main.qml"),
                        .contents = contents,
                        .sha256 = QCryptographicHash::hash(
                            contents, QCryptographicHash::Sha256),
                    },
                },
        };
        respin::Bundle decoded;
        QString error;
        QVERIFY2(
            respin::decodeBundle(respin::encodeBundle(source), decoded, &error),
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
        QVERIFY(!respin::isSafeBundlePath(path));
    }

    void rejectsUnsafeBundleId()
    {
        const QByteArray contents = "Item {}";
        auto bundle = respin::Bundle{
            .id = QStringLiteral("../../outside"),
            .files =
                {
                    respin::BundleFile{
                        .path = QStringLiteral("qt/qml/App/Main.qml"),
                        .contents = contents,
                        .sha256 = QCryptographicHash::hash(
                            contents, QCryptographicHash::Sha256),
                    },
                },
        };
        respin::Bundle decoded;
        QString error;
        QVERIFY(!respin::decodeBundle(respin::encodeBundle(bundle), decoded, &error));
        QVERIFY(error.contains(QStringLiteral("id")));
    }

    void rejectsDuplicateBundlePath()
    {
        const QByteArray contents = "Item {}";
        const auto file = respin::BundleFile{
            .path = QStringLiteral("qt/qml/App/Main.qml"),
            .contents = contents,
            .sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256),
        };
        respin::Bundle decoded;
        QString error;
        QVERIFY(!respin::decodeBundle(
            respin::encodeBundle(
                respin::Bundle{
                    .id = QStringLiteral("duplicate"),
                    .files = {file, file},
                }),
            decoded, &error));
        QVERIFY(error.contains(QStringLiteral("duplicate")));
    }
};

QTEST_APPLESS_MAIN(ProtocolTests)
#include "protocol_tests.moc"
