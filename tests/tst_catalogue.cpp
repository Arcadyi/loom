#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <loom/loom.h>
#include <loom/loomcatalogue.h>

#include "style/loomstylecompiler.h"

class CatalogueTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void everyClassParses();
    void everyVariantCombines();
    void unknownClassesAreReported();
    void knownStyleReportsNothing();
    void catalogueWidensWithConfig();
    void jsonShape();

private:
    QString writeConfig(const char *json);
    QTemporaryDir m_dir;
};

void CatalogueTests::cleanup()
{
    loom::setTheme(QStringLiteral("light"));
}

QString CatalogueTests::writeConfig(const char *json)
{
    static int counter = 0;
    const QString path = m_dir.filePath(QStringLiteral("config%1.json").arg(counter++));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(json);
    return path;
}

// The catalogue's prefix list is the one place that restates the parse chain
// rather than reading a table from it. This is what keeps the two honest: if a
// prefix is wrong or a token scale is paired with the wrong utility, the class
// it generates will not parse.
void CatalogueTests::everyClassParses()
{
    const loom::StyleCatalogue catalogue = loom::styleCatalogue();
    QVERIFY(!catalogue.classes.isEmpty());

    QStringList rejected;
    for (const QString &klass : catalogue.classes) {
        if (!loom::unknownStyleClasses(klass).isEmpty())
            rejected.append(klass);
    }
    QVERIFY2(
        rejected.isEmpty(),
        qPrintable(QStringLiteral("catalogue emits unparseable classes: %1")
                       .arg(rejected.join(QLatin1String(", ")))));
}

void CatalogueTests::everyVariantCombines()
{
    const loom::StyleCatalogue catalogue = loom::styleCatalogue();
    for (const auto &expected :
         {QStringLiteral("sm"), QStringLiteral("2xl"), QStringLiteral("hover"),
          QStringLiteral("focus-visible"), QStringLiteral("checked"),
          QStringLiteral("motion-reduce"), QStringLiteral("first"),
          QStringLiteral("group-hover")})
        QVERIFY2(
            catalogue.variants.contains(expected),
            qPrintable(QStringLiteral("catalogue omitted variant: %1").arg(expected)));

    for (const QString &variant : catalogue.variants) {
        const QString klass = variant + QLatin1Char(':') + QLatin1String("bg-surface");
        QVERIFY2(
            loom::unknownStyleClasses(klass).isEmpty(),
            qPrintable(QStringLiteral("variant rejected: %1").arg(klass)));
    }
}

void CatalogueTests::unknownClassesAreReported()
{
    // Typos of each shape: bad variant, bad token key, bad prefix entirely.
    const QStringList unknown =
        loom::unknownStyleClasses(QStringLiteral("hoverr:bg-surface bg-nope flex-1"));
    QCOMPARE(unknown.size(), 3);
    QCOMPARE(unknown.at(0), QStringLiteral("hoverr:bg-surface"));
    QCOMPARE(unknown.at(1), QStringLiteral("bg-nope"));
    QCOMPARE(unknown.at(2), QStringLiteral("flex-1"));
}

void CatalogueTests::knownStyleReportsNothing()
{
    // The string from loomapp's TopBar, plus a numeric border the catalogue
    // cannot enumerate but the parser accepts.
    QVERIFY(
        loom::unknownStyleClasses(
            QStringLiteral(
                "bg-transparent hover:bg-surface pressed:bg-surface-alt "
                "rounded-full transition-colors border-1"))
            .isEmpty());
}

void CatalogueTests::catalogueWidensWithConfig()
{
    const loom::StyleCatalogue before = loom::styleCatalogue();
    QVERIFY(!before.classes.contains(QStringLiteral("bg-brand")));

    const QString path =
        writeConfig(R"({"schemaVersion":2,"tokens":{"colors":{"brand":"#123456"}}})");
    QVERIFY(loom::loadConfig(path));

    const loom::StyleCatalogue after = loom::styleCatalogue();
    QVERIFY(after.classes.contains(QStringLiteral("bg-brand")));
    QVERIFY(loom::unknownStyleClasses(QStringLiteral("bg-brand")).isEmpty());

    // Application-declared states widen the same way, and reach the catalogue
    // through variantNames() alone -- no completion, checker or hover code
    // knows they exist. everyVariantCombines() above then round-trips the new
    // names through the parser without being told about them either.
    const QString withStates =
        writeConfig(R"({"schemaVersion":2,"states":{"syncing":"has unsaved changes"}})");
    QVERIFY(loom::loadConfig(withStates));

    const loom::StyleCatalogue states = loom::styleCatalogue();
    QVERIFY(states.variants.contains(QStringLiteral("syncing")));
    QVERIFY(states.variants.contains(QStringLiteral("not-syncing")));
    QVERIFY(states.variants.contains(QStringLiteral("group-syncing")));
    QVERIFY(loom::unknownStyleClasses(QStringLiteral("syncing:border-warning")).isEmpty());
}

void CatalogueTests::jsonShape()
{
    const QJsonDocument document = QJsonDocument::fromJson(loom::styleCatalogueJson());
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();

    QCOMPARE(
        root.value(QStringLiteral("version")).toString(),
        QString::fromLatin1(loom::version()));
    QVERIFY(!root.value(QStringLiteral("classes")).toArray().isEmpty());
    QVERIFY(!root.value(QStringLiteral("families")).toArray().isEmpty());
    QCOMPARE(
        root.value(QStringLiteral("numericPrefixes")).toArray().first().toString(),
        QStringLiteral("border-"));

    const QJsonObject family =
        root.value(QStringLiteral("families")).toArray().first().toObject();
    QVERIFY(family.contains(QStringLiteral("prefix")));
    QVERIFY(family.contains(QStringLiteral("scale")));
    QVERIFY(!family.value(QStringLiteral("values")).toArray().isEmpty());
}

QTEST_APPLESS_MAIN(CatalogueTests)
#include "tst_catalogue.moc"
