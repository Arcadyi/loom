#include "lspdocument.h"
#include "lspjsonrpc.h"
#include "styleintelligence.h"
#include "styleworkspace.h"

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <functional>

class LspTests final : public QObject {
    Q_OBJECT

private slots:
    void framingHandlesFragmentedAndCoalescedMessages();
    void documentFindsOnlyStyleResultLiterals();
    void incrementalChangesHonorUtf8Positions();
    void projectTokensDriveIntelligence();
    void proxyMergesQmllsAndLoomFeatures();
};

namespace {

QJsonObject request(int id, const QString &method, const QJsonObject &params = {})
{
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
}

QJsonObject notification(const QString &method, const QJsonObject &params = {})
{
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
}

void writeMessage(QProcess *process, const QJsonObject &message)
{
    const QByteArray frame = lsp::JsonRpcFramer::frame(message);
    QCOMPARE(process->write(frame), frame.size());
    QVERIFY(process->waitForBytesWritten(3000));
}

QList<QJsonObject> readUntil(
    QProcess *process, lsp::JsonRpcFramer *framer,
    const std::function<bool(const QJsonObject &)> &done)
{
    QList<QJsonObject> all;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        if (process->bytesAvailable() == 0 && !process->waitForReadyRead(250))
            continue;
        all.append(framer->push(process->readAllStandardOutput()));
        for (const auto &message : all) {
            if (done(message))
                return all;
        }
    }
    return all;
}

QJsonObject responseWithId(const QList<QJsonObject> &messages, int id)
{
    for (const auto &message : messages) {
        if (message.value(QStringLiteral("id")).toInt(-1) == id
            && !message.contains(QStringLiteral("method"))) {
            return message;
        }
    }
    return {};
}

} // namespace

void LspTests::framingHandlesFragmentedAndCoalescedMessages()
{
    const QJsonObject first = request(1, QStringLiteral("initialize"));
    const QJsonObject second = notification(QStringLiteral("initialized"));
    const QByteArray bytes =
        lsp::JsonRpcFramer::frame(first) + lsp::JsonRpcFramer::frame(second);

    lsp::JsonRpcFramer framer;
    QCOMPARE(framer.push(bytes.first(11)).size(), 0);
    QCOMPARE(framer.push(bytes.sliced(11, 17)).size(), 0);
    const auto messages = framer.push(bytes.sliced(28));
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0), first);
    QCOMPARE(messages.at(1), second);
}

void LspTests::documentFindsOnlyStyleResultLiterals()
{
    lsp::Document document;
    document.open(
        QStringLiteral(
            "Item {\n"
            "  property string unrelated: \"bg-nope\"\n"
            "  // Lo.style: \"also-nope\"\n"
            "  Lo.style: \"brand\" === Loom.theme ? \"bg-red-500\"\n"
            "    : \"hover:bg-blue-500 p-4\"\n"
            "}\n"),
        1);
    const auto tokens = document.styleTokens();
    QStringList names;
    for (const auto &token : tokens)
        names.append(token.text);
    QCOMPARE(
        names,
        QStringList(
            {QStringLiteral("bg-red-500"), QStringLiteral("hover:bg-blue-500"),
             QStringLiteral("p-4")}));
}

void LspTests::incrementalChangesHonorUtf8Positions()
{
    lsp::Document document;
    document.open(QString::fromUtf8("🙂 Lo.style: \"p-4\"\n"), 1);
    QString error;
    QVERIFY(document.applyChanges(
        QJsonArray{QJsonObject{
            {QStringLiteral("range"),
             QJsonObject{
                 {QStringLiteral("start"),
                  QJsonObject{
                      {QStringLiteral("line"), 0},
                      {QStringLiteral("character"), 16},
                  }},
                 {QStringLiteral("end"),
                  QJsonObject{
                      {QStringLiteral("line"), 0},
                      {QStringLiteral("character"), 19},
                  }},
             }},
            {QStringLiteral("text"), QStringLiteral("m-2")},
        }},
        2, lsp::PositionEncoding::Utf8, &error));
    QCOMPARE(document.styleTokens().constFirst().text, QStringLiteral("m-2"));
}

void LspTests::projectTokensDriveIntelligence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("qml"))));

    QFile manifest(directory.filePath(QStringLiteral("loom.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"({
      "schemaVersion": 1,
      "project": {"name": "LspTest"},
      "design": "tokens.json",
      "qt": {"version": "6.11"},
      "applications": {"App": {
        "name": "App", "target": "App", "id": "dev.test.app",
        "uri": "dev.test.App", "entry": "Main", "qmlRoots": ["qml"],
        "assetRoots": [], "platforms": ["desktop"]
      }}
    })");
    manifest.close();
    QFile design(directory.filePath(QStringLiteral("tokens.json")));
    QVERIFY(design.open(QIODevice::WriteOnly));
    design.write(R"({
      "colors":{"brand":"#123456"},
      "space":{"18":72},
      "themes":{"midnight":{"extends":"dark","surface":"#010203"}},
      "defaultTheme":"midnight"
    })");
    design.close();

    const QString qmlPath = directory.filePath(QStringLiteral("qml/Main.qml"));
    lsp::Document document;
    document.open(
        QStringLiteral("Item { Lo.style: \"bg-br bg-brnad text-white/50 p-18\" }"), 1);
    lsp::StyleWorkspace workspace;
    lsp::StyleIntelligence intelligence(&workspace);

    const qsizetype completionOffset =
        document.text().indexOf(QStringLiteral("bg-br")) + 5;
    bool inStyle = false;
    const QJsonObject completion = intelligence.completion(
        qmlPath, document, completionOffset, lsp::PositionEncoding::Utf16, &inStyle);
    QVERIFY(inStyle);
    bool foundBrand = false;
    for (const auto &item : completion.value(QStringLiteral("items")).toArray()) {
        if (item.toObject().value(QStringLiteral("label")).toString()
            == QStringLiteral("bg-brand")) {
            foundBrand = true;
        }
    }
    QVERIFY(foundBrand);
    workspace.activateForFile(qmlPath);
    QCOMPARE(
        workspace.metadata(QStringLiteral("bg-surface")).color.name(),
        QStringLiteral("#010203"));

    const auto diagnostics =
        intelligence.diagnostics(qmlPath, document, lsp::PositionEncoding::Utf16);
    QCOMPARE(diagnostics.size(), 2); // incomplete bg-br and misspelled bg-brnad
    const auto actions =
        intelligence.codeActions(QUrl::fromLocalFile(qmlPath).toString(), diagnostics);
    bool fixesBrand = false;
    for (const auto &action : actions) {
        fixesBrand |= action.toObject()
                          .value(QStringLiteral("title"))
                          .toString()
                          .contains(QStringLiteral("bg-brand"));
    }
    QVERIFY(fixesBrand);

    const qsizetype paddingOffset = document.text().indexOf(QStringLiteral("p-18")) + 2;
    const QJsonObject hover =
        intelligence.hover(qmlPath, document, paddingOffset, lsp::PositionEncoding::Utf16)
            .toObject();
    QVERIFY(hover.value(QStringLiteral("contents"))
                .toObject()
                .value(QStringLiteral("value"))
                .toString()
                .contains(QStringLiteral("72px")));

    const auto colors =
        intelligence.colors(qmlPath, document, lsp::PositionEncoding::Utf16);
    QCOMPARE(colors.size(), 1);
    const double alpha = colors.first()
                             .toObject()
                             .value(QStringLiteral("color"))
                             .toObject()
                             .value(QStringLiteral("alpha"))
                             .toDouble();
    QVERIFY(qAbs(alpha - 0.5) < 0.001);
}

void LspTests::proxyMergesQmllsAndLoomFeatures()
{
    QProcess proxy;
    proxy.setProgram(QStringLiteral(LOOM_TEST_EXE));
    proxy.setArguments(
        {QStringLiteral("lsp"), QStringLiteral("--qmlls"),
         QStringLiteral(LOOM_FAKE_QMLLS)});
    proxy.setProcessChannelMode(QProcess::SeparateChannels);
    proxy.start();
    QVERIFY2(proxy.waitForStarted(5000), qPrintable(proxy.errorString()));

    lsp::JsonRpcFramer framer;
    writeMessage(
        &proxy,
        request(
            1, QStringLiteral("initialize"),
            QJsonObject{
                {QStringLiteral("rootUri"),
                 QUrl::fromLocalFile(QDir::currentPath()).toString()},
                {QStringLiteral("capabilities"), QJsonObject{}},
            }));
    auto messages = readUntil(&proxy, &framer, [](const QJsonObject &message) {
        return message.value(QStringLiteral("id")).toInt(-1) == 1;
    });
    const QJsonObject initialize = responseWithId(messages, 1);
    QVERIFY(!initialize.isEmpty());
    const QJsonObject capabilities = initialize.value(QStringLiteral("result"))
                                         .toObject()
                                         .value(QStringLiteral("capabilities"))
                                         .toObject();
    const auto triggers = capabilities.value(QStringLiteral("completionProvider"))
                              .toObject()
                              .value(QStringLiteral("triggerCharacters"))
                              .toArray();
    QVERIFY(triggers.contains(QStringLiteral(".")));
    QVERIFY(triggers.contains(QStringLiteral(":")));
    QVERIFY(capabilities.value(QStringLiteral("colorProvider")).toBool());

    writeMessage(&proxy, notification(QStringLiteral("initialized")));
    const QString uri =
        QUrl::fromLocalFile(QDir::current().filePath(QStringLiteral("Proxy.qml")))
            .toString();
    const QString source = QStringLiteral("Item { Lo.style: \"bg-noep\" }");
    writeMessage(
        &proxy,
        notification(
            QStringLiteral("textDocument/didOpen"),
            QJsonObject{
                {QStringLiteral("textDocument"),
                 QJsonObject{
                     {QStringLiteral("uri"), uri},
                     {QStringLiteral("languageId"), QStringLiteral("qml")},
                     {QStringLiteral("version"), 1},
                     {QStringLiteral("text"), source},
                 }},
            }));
    messages = readUntil(&proxy, &framer, [](const QJsonObject &message) {
        if (message.value(QStringLiteral("method")).toString()
            != QStringLiteral("textDocument/publishDiagnostics"))
            return false;
        const auto diagnostics = message.value(QStringLiteral("params"))
                                     .toObject()
                                     .value(QStringLiteral("diagnostics"))
                                     .toArray();
        bool loom = false;
        bool qmlls = false;
        for (const auto &entry : diagnostics) {
            const QString sourceName =
                entry.toObject().value(QStringLiteral("source")).toString();
            loom |= sourceName == QStringLiteral("loom");
            qmlls |= sourceName == QStringLiteral("qmlls");
        }
        return loom && qmlls;
    });
    QVERIFY(!messages.isEmpty());

    const QString completionSource = QStringLiteral("Item { Lo.style: \"bg-b\" }");
    writeMessage(
        &proxy,
        notification(
            QStringLiteral("textDocument/didChange"),
            QJsonObject{
                {QStringLiteral("textDocument"),
                 QJsonObject{
                     {QStringLiteral("uri"), uri},
                     {QStringLiteral("version"), 2},
                 }},
                {QStringLiteral("contentChanges"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("text"), completionSource},
                 }}},
            }));

    writeMessage(
        &proxy,
        request(
            2, QStringLiteral("textDocument/completion"),
            QJsonObject{
                {QStringLiteral("textDocument"),
                 QJsonObject{{QStringLiteral("uri"), uri}}},
                {QStringLiteral("position"),
                 QJsonObject{
                     {QStringLiteral("line"), 0},
                     {QStringLiteral("character"), 22},
                 }},
            }));
    messages = readUntil(&proxy, &framer, [](const QJsonObject &message) {
        return message.value(QStringLiteral("id")).toInt(-1) == 2;
    });
    const QJsonArray items = responseWithId(messages, 2)
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("items"))
                                 .toArray();
    bool qmlItem = false;
    bool loomItem = false;
    for (const auto &item : items) {
        const QString label = item.toObject().value(QStringLiteral("label")).toString();
        qmlItem |= label == QStringLiteral("qmlItem");
        loomItem |= label == QStringLiteral("bg-blue-500");
    }
    QVERIFY(qmlItem);
    QVERIFY(loomItem);

    writeMessage(&proxy, request(3, QStringLiteral("shutdown")));
    messages = readUntil(&proxy, &framer, [](const QJsonObject &message) {
        return message.value(QStringLiteral("id")).toInt(-1) == 3;
    });
    QVERIFY(!responseWithId(messages, 3).isEmpty());
    writeMessage(&proxy, notification(QStringLiteral("exit")));
    proxy.closeWriteChannel();
    QVERIFY(proxy.waitForFinished(5000));
    QCOMPARE(proxy.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proxy.exitCode(), 0);
}

QTEST_GUILESS_MAIN(LspTests)
#include "tst_lsp.moc"
