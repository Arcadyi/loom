#include "commandline.h"
#include "projectscaffolder.h"

#include <QtTest>

namespace {

cli::CommandSpec buildLikeSpec()
{
    return cli::CommandSpec{
        .name = QStringLiteral("build"),
        .summary = QStringLiteral("Configure and build."),
        .usage = QStringLiteral("respin build [options]"),
        .options =
            {
                {QStringLiteral("target"), QStringLiteral("platform"),
                 QStringLiteral("Platform.")},
                {QStringLiteral("config"), QStringLiteral("configuration"),
                 QStringLiteral("Build type.")},
                {QStringLiteral("prefix"), QStringLiteral("path"),
                 QStringLiteral("Prefix path.")},
                {QStringLiteral("apply"), {}, QStringLiteral("A flag.")},
            },
        .minimumPositional = 0,
        .maximumPositional = 0,
    };
}

} // namespace

class CommandLineTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Every rejection path writes to stderr; keep the test output readable.
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));
    }

    void acceptsSpaceAndEqualsForms()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(
                buildLikeSpec(),
                {QStringLiteral("--config"), QStringLiteral("Release"),
                 QStringLiteral("--prefix=/opt/qt")},
                parsed),
            cli::ParseOutcome::Ready);
        QCOMPARE(parsed.value(QStringLiteral("config")), QStringLiteral("Release"));
        // "--prefix=/x" never matched the old hand-rolled indexOf() scan.
        QCOMPARE(parsed.value(QStringLiteral("prefix")), QStringLiteral("/opt/qt"));
        QVERIFY(!parsed.isSet(QStringLiteral("apply")));
    }

    void flagsAreRecognizedWithoutAValue()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(buildLikeSpec(), {QStringLiteral("--apply")}, parsed),
            cli::ParseOutcome::Ready);
        QVERIFY(parsed.isSet(QStringLiteral("apply")));
    }

    // Previously ignored outright: "respin build --targt android" built desktop
    // and exited 0.
    void unknownOptionIsRejected()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(
                buildLikeSpec(), {QStringLiteral("--targt"), QStringLiteral("android")},
                parsed),
            cli::ParseOutcome::Rejected);
    }

    void optionMissingItsValueIsRejected()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(buildLikeSpec(), {QStringLiteral("--prefix")}, parsed),
            cli::ParseOutcome::Rejected);
    }

    // The trap that motivated the migration: QCommandLineParser will happily
    // consume the next token as a value, so this used to yield prefix="--config"
    // and silently drop the configuration.
    void anOptionIsNotSwallowedAsAnotherOptionsValue()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(
                buildLikeSpec(),
                {QStringLiteral("--prefix"), QStringLiteral("--config"),
                 QStringLiteral("Release")},
                parsed),
            cli::ParseOutcome::Rejected);
        QVERIFY2(
            parsed.value(QStringLiteral("prefix")) != QStringLiteral("--config"),
            "an option name was accepted as another option's value");
    }

    void strayPositionalIsRejected()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(buildLikeSpec(), {QStringLiteral("extra")}, parsed),
            cli::ParseOutcome::Rejected);
    }

    void missingRequiredPositionalIsRejected()
    {
        auto spec = buildLikeSpec();
        spec.minimumPositional = 1;
        spec.maximumPositional = 1;
        cli::ParsedCommand parsed;
        QCOMPARE(cli::parseCommand(spec, {}, parsed), cli::ParseOutcome::Rejected);
    }

    void duplicateOptionTakesTheLastValue()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(
                buildLikeSpec(),
                {QStringLiteral("--config"), QStringLiteral("Debug"),
                 QStringLiteral("--config"), QStringLiteral("Release")},
                parsed),
            cli::ParseOutcome::Ready);
        QCOMPARE(parsed.value(QStringLiteral("config")), QStringLiteral("Release"));
    }

    // "respin dev --help" used to start a dev server: help was only recognized
    // as the very first argument of the whole command line.
    void helpIsRecognizedPerCommand()
    {
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(buildLikeSpec(), {QStringLiteral("--help")}, parsed),
            cli::ParseOutcome::HelpPrinted);
    }

    void passthroughIsSeparatedOnlyWhereAllowed()
    {
        auto spec = buildLikeSpec();
        spec.acceptsPassthrough = true;
        cli::ParsedCommand parsed;
        QCOMPARE(
            cli::parseCommand(
                spec,
                {QStringLiteral("--config"), QStringLiteral("Debug"),
                 QStringLiteral("--"), QStringLiteral("--not-mine"),
                 QStringLiteral("value")},
                parsed),
            cli::ParseOutcome::Ready);
        QCOMPARE(parsed.value(QStringLiteral("config")), QStringLiteral("Debug"));
        QCOMPARE(
            parsed.passthrough(),
            QStringList({QStringLiteral("--not-mine"), QStringLiteral("value")}));

        cli::ParsedCommand rejected;
        QCOMPARE(
            cli::parseCommand(
                buildLikeSpec(), {QStringLiteral("--"), QStringLiteral("anything")},
                rejected),
            cli::ParseOutcome::Rejected);
    }

    void projectNameValidation_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("valid");
        QTest::newRow("ordinary") << QStringLiteral("MyApp") << true;
        QTest::newRow("digits") << QStringLiteral("App2") << true;
        QTest::newRow("dashes") << QStringLiteral("my-app") << true;
        // Substituted unquoted into project(...), so this generates CMake that
        // cannot parse.
        QTest::newRow("space") << QStringLiteral("My App") << false;
        QTest::newRow("empty") << QString() << false;
        // Used to scaffold into the current directory.
        QTest::newRow("dot") << QStringLiteral(".") << false;
        QTest::newRow("parent") << QStringLiteral("..") << false;
        QTest::newRow("separator") << QStringLiteral("a/b") << false;
        QTest::newRow("quote") << QStringLiteral("say\"hi") << false;
        QTest::newRow("backslash") << QStringLiteral("back\\slash") << false;
        QTest::newRow("cmake-variable") << QStringLiteral("${EVIL}") << false;
        QTest::newRow("paren") << QStringLiteral("App(1)") << false;
        QTest::newRow("punctuation-only") << QStringLiteral("!!!") << false;
    }

    void projectNameValidation()
    {
        QFETCH(QString, name);
        QFETCH(bool, valid);
        QString error;
        QCOMPARE(ProjectScaffolder::isValidProjectName(name, &error), valid);
        if (!valid)
            QVERIFY2(!error.isEmpty(), "a rejected name produced no explanation");
    }
};

QTEST_GUILESS_MAIN(CommandLineTests)
#include "commandline_tests.moc"
