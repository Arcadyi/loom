#include <QtTest>

#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QScopedPointer>
#include <QTemporaryDir>

#include "loomconfigloader.h"
#include "style/loomiconprovider.h"

namespace {

// Escaped rather than raw string literals on purpose: moc does not honour
// raw strings, and reads the "//" in the xmlns URL as a comment, which eats
// the Q_OBJECT below and leaves the test with no vtable.

// Left half opaque, right half untouched: enough to prove a tint replaces
// color without eating the asset's own coverage.
constexpr auto halfFilledSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
    "viewBox=\"0 0 16 16\">"
    "<rect x=\"0\" y=\"0\" width=\"8\" height=\"16\" fill=\"#0000ff\"/>"
    "</svg>";

// The shape that started this: Qt's SVG renderer has no notion of an inherited
// color and resolves currentColor to black.
constexpr auto currentColorSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
    "viewBox=\"0 0 16 16\">"
    "<rect x=\"0\" y=\"0\" width=\"8\" height=\"16\" fill=\"currentColor\"/>"
    "</svg>";

QString writeAsset(const QTemporaryDir &dir, const QString &name, const char *contents)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(contents);
    return path;
}

// Everything after image://loom/, still percent-encoded -- exactly the `id`
// QQuickPixmap hands a provider at runtime.
QString providerId(const QUrl &url)
{
    static constexpr QLatin1StringView prefix("image://loom/");
    return url.toString(QUrl::FullyEncoded).sliced(prefix.size());
}

} // namespace

class IconTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void initTestCase();
    void urlCarriesTintAndSource();
    void urlWithoutColorAsksForNoTint();
    void tintReplacesColorAndKeepsCoverage();
    void tintOverridesCurrentColor();
    void untintedRequestServesAssetAsAuthored();
    void requestedSizeIsHonoured();
    void unreadableSourceWarnsAndYieldsNull();
    void malformedIdYieldsNull();
    void relativeSourceResolvesAgainstIconRoot();
    void iconRootToleratesMissingTrailingSlash();
    void absoluteSourceIgnoresIconRoot();
    void relativeSourceSurvivesUnsetIconRoot();
    void configFileSetsIconRoot();
    void configFileInResourcesSetsQrcIconRoot();
    void singletonRegistersProviderOnItsEngine();

private:
    QTemporaryDir m_dir;
    QString m_halfFilled;
    QString m_currentColor;
};

void IconTests::cleanup()
{
    // The icon root is process-global; never leak one test's root into the next.
    setLoomIconRoot(QUrl());
}

void IconTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_halfFilled = writeAsset(m_dir, QStringLiteral("half.svg"), halfFilledSvg);
    m_currentColor = writeAsset(m_dir, QStringLiteral("current.svg"), currentColorSvg);
    QVERIFY(!m_halfFilled.isEmpty());
    QVERIFY(!m_currentColor.isEmpty());
}

void IconTests::urlCarriesTintAndSource()
{
    const QUrl source = QUrl::fromLocalFile(m_halfFilled);
    const QUrl url = loomIconUrl(source, QColor(0x16, 0xa3, 0x4a));

    QCOMPARE(url.scheme(), QStringLiteral("image"));
    QCOMPARE(url.host(), QStringLiteral("loom"));
    // Opaque green, spelled #aarrggbb so a translucent tint survives too.
    QVERIFY(providerId(url).startsWith(QStringLiteral("ff16a34a/")));
    // The source survives the round trip through percent-encoding.
    const QString id = providerId(url);
    const QString encoded = id.sliced(id.indexOf(QLatin1Char('/')) + 1);
    QCOMPARE(QUrl(QUrl::fromPercentEncoding(encoded.toUtf8())), source);
}

void IconTests::urlWithoutColorAsksForNoTint()
{
    const QUrl url = loomIconUrl(QUrl::fromLocalFile(m_halfFilled), QColor());
    QVERIFY(providerId(url).startsWith(QStringLiteral("-/")));
}

void IconTests::tintReplacesColorAndKeepsCoverage()
{
    LoomIconProvider provider;
    const QUrl url = loomIconUrl(QUrl::fromLocalFile(m_halfFilled), QColor(Qt::red));

    QSize size;
    const QImage image = provider.requestImage(providerId(url), &size, QSize(16, 16));

    QVERIFY(!image.isNull());
    QCOMPARE(size, QSize(16, 16));
    // The blue the asset authored is gone, replaced by the tint...
    QCOMPARE(image.pixelColor(2, 8), QColor(Qt::red));
    // ...and the half it never drew is still transparent.
    QCOMPARE(image.pixelColor(13, 8).alpha(), 0);
}

void IconTests::tintOverridesCurrentColor()
{
    LoomIconProvider provider;
    const QUrl url = loomIconUrl(QUrl::fromLocalFile(m_currentColor), QColor(Qt::green));

    QSize size;
    const QImage image = provider.requestImage(providerId(url), &size, QSize(16, 16));

    QVERIFY(!image.isNull());
    // Without a tint this pixel is the black Qt resolves currentColor to.
    QCOMPARE(image.pixelColor(2, 8), QColor(Qt::green));
}

void IconTests::untintedRequestServesAssetAsAuthored()
{
    LoomIconProvider provider;
    const QUrl url = loomIconUrl(QUrl::fromLocalFile(m_halfFilled), QColor());

    QSize size;
    const QImage image = provider.requestImage(providerId(url), &size, QSize(16, 16));

    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(2, 8), QColor(Qt::blue));
}

void IconTests::requestedSizeIsHonoured()
{
    LoomIconProvider provider;
    const QUrl url = loomIconUrl(QUrl::fromLocalFile(m_halfFilled), QColor(Qt::red));

    QSize size;
    // Rasterised at the asked-for size rather than scaled from the 16px
    // default, which is what keeps an upscaled icon sharp.
    const QImage image = provider.requestImage(providerId(url), &size, QSize(64, 64));

    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(64, 64));
    QCOMPARE(size, QSize(64, 64));
    QCOMPARE(image.pixelColor(8, 32), QColor(Qt::red));
    QCOMPARE(image.pixelColor(55, 32).alpha(), 0);
}

void IconTests::unreadableSourceWarnsAndYieldsNull()
{
    LoomIconProvider provider;
    const QUrl url = loomIconUrl(
        QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("absent.svg"))),
        QColor(Qt::red));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot read")));
    QSize size;
    QVERIFY(provider.requestImage(providerId(url), &size, QSize(16, 16)).isNull());
}

void IconTests::malformedIdYieldsNull()
{
    LoomIconProvider provider;

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("malformed icon id")));
    QSize size;
    QVERIFY(
        provider.requestImage(QStringLiteral("no-separator"), &size, QSize()).isNull());
}

QUrl IconTests_sourceOf(const QUrl &url)
{
    const QString id = providerId(url);
    return QUrl(
        QUrl::fromPercentEncoding(id.sliced(id.indexOf(QLatin1Char('/')) + 1).toUtf8()));
}

void IconTests::relativeSourceResolvesAgainstIconRoot()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "QtObject {\n"
        "    property url tinted\n"
        "    Component.onCompleted: {\n"
        "        Loom.iconRoot = \""
            + QUrl::fromLocalFile(m_dir.path()).toString().toUtf8()
            + "\"\n"
              "        tinted = Loom.icon(\"half.svg\", Loom.color.green600)\n"
              "    }\n"
              "}\n",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    QCOMPARE(
        IconTests_sourceOf(object->property("tinted").toUrl()),
        QUrl::fromLocalFile(m_halfFilled));
}

void IconTests::iconRootToleratesMissingTrailingSlash()
{
    // QUrl::resolved() would otherwise treat "icons" as a file and drop it.
    const QUrl bare =
        QUrl::fromLocalFile(m_dir.path()).adjusted(QUrl::StripTrailingSlash);
    setLoomIconRoot(bare);

    QCOMPARE(loomIconRoot().path().endsWith(QLatin1Char('/')), true);
    QCOMPARE(
        loomResolveIconSource(QUrl(QStringLiteral("half.svg"))),
        QUrl::fromLocalFile(m_halfFilled));
}

void IconTests::absoluteSourceIgnoresIconRoot()
{
    setLoomIconRoot(QUrl::fromLocalFile(QStringLiteral("/nowhere")));

    // A source that already carries a scheme is the caller being explicit.
    const QUrl absolute = QUrl::fromLocalFile(m_halfFilled);
    QCOMPARE(loomResolveIconSource(absolute), absolute);
    // As is one anchored at the filesystem root.
    const QUrl rooted(QStringLiteral("/tmp/elsewhere.svg"));
    QCOMPARE(loomResolveIconSource(rooted), rooted);
}

void IconTests::relativeSourceSurvivesUnsetIconRoot()
{
    // No root configured: pass the relative source through untouched so the
    // provider reports the path the caller actually wrote.
    const QUrl relative(QStringLiteral("half.svg"));
    QCOMPARE(loomResolveIconSource(relative), relative);
}

void IconTests::configFileSetsIconRoot()
{
    const QString config = writeAsset(
        m_dir, QStringLiteral("loom.json"),
        "{ \"schemaVersion\": 2, \"iconRoot\": \".\" }");
    QVERIFY(!config.isEmpty());

    QVERIFY(loomLoadConfigFile(config));

    // "." resolves against the config file's own directory.
    QCOMPARE(
        loomResolveIconSource(QUrl(QStringLiteral("half.svg"))),
        QUrl::fromLocalFile(m_halfFilled));
}

void IconTests::configFileInResourcesSetsQrcIconRoot()
{
    // The documented C++ spelling is loom::loadConfig(":/config/loom.json").
    // QUrl::fromLocalFile would turn that directory into "file::/config",
    // which resolves to a path no provider can open.
    QVERIFY(loomLoadConfigFile(QStringLiteral(":/data/tst_icon/loom.json")));

    QCOMPARE(
        loomResolveIconSource(QUrl(QStringLiteral("home.svg"))),
        QUrl(QStringLiteral("qrc:/data/tst_icon/icons/home.svg")));
}

void IconTests::singletonRegistersProviderOnItsEngine()
{
    QQmlEngine engine;
    QVERIFY(!engine.imageProvider(QStringLiteral("loom")));

    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "QtObject { property url tinted: Loom.icon(\"x.svg\", Loom.color.foreground) }\n",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    // Minting a URL is what installs the provider, so any URL that exists is
    // resolvable from a bare `import Loom` with no application wiring.
    QVERIFY(engine.imageProvider(QStringLiteral("loom")));
}

QTEST_GUILESS_MAIN(IconTests)
#include "tst_icon.moc"
