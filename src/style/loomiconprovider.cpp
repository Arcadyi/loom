#include "loomiconprovider.h"

#include <QColor>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>

Q_STATIC_LOGGING_CATEGORY(lcLoomIcon, "loom.icon")

namespace {

// Spelling for "the caller asked for no recolour". Not a valid QColor name, so
// it cannot collide with a real tint.
constexpr QLatin1StringView untinted("-");

// QImageReader wants a filesystem or resource path, not a URL.
QString readablePath(const QUrl &url)
{
    if (url.isLocalFile())
        return url.toLocalFile();
    if (url.scheme() == QLatin1String("qrc"))
        return QLatin1Char(':') + url.path();
    // Anything written as a relative URL in a .qml file has already been
    // resolved against that file by the time it reaches the singleton, so a
    // scheme-less URL here is a bare path the caller built by hand.
    if (url.scheme().isEmpty())
        return url.path();
    return url.toString();
}

// Process-global, like the theme: an application configures it once, and any
// engine minting a URL sees the same root.
QUrl &iconRootStorage()
{
    static QUrl root;
    return root;
}

} // namespace

LoomIconProvider::LoomIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage
LoomIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype separator = id.indexOf(QLatin1Char('/'));
    if (separator < 0) {
        qCWarning(lcLoomIcon).noquote() << "malformed icon id:" << id;
        return {};
    }

    const QString tint = id.first(separator);
    const QUrl source(QUrl::fromPercentEncoding(id.sliced(separator + 1).toUtf8()));
    const QString path = readablePath(source);

    QImageReader reader(path);
    // An SVG rasterises at whatever size it is handed, so asking up front is
    // what keeps strokes crisp instead of scaling a default-size raster. Only
    // meaningful for scalable formats; for a PNG this is a plain scale.
    if (!requestedSize.isEmpty())
        reader.setScaledSize(requestedSize);

    QImage image = reader.read();
    if (image.isNull()) {
        qCWarning(lcLoomIcon).noquote()
            << "cannot read" << path << "--" << reader.errorString();
        return {};
    }

    if (tint != untinted) {
        const QColor color(QLatin1Char('#') + tint);
        if (color.isValid()) {
            image = image.convertToFormat(QImage::Format_ARGB32);
            // SourceIn keeps the coverage the asset drew and replaces only its
            // color, so anti-aliased edges stay soft and transparent stays
            // transparent. This is also what makes `currentColor` a non-issue:
            // whatever the SVG resolved it to is overwritten either way.
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(image.rect(), color);
        } else {
            qCWarning(lcLoomIcon).noquote()
                << "unparseable tint" << tint << "for" << path;
        }
    }

    if (size)
        *size = image.size();
    return image;
}

QUrl loomIconRoot()
{
    return iconRootStorage();
}

void setLoomIconRoot(const QUrl &root)
{
    QUrl normalized = root;
    // QUrl::resolved() treats the last segment of the base as a file and drops
    // it, so "assets/icons" would resolve "home.svg" to "assets/home.svg".
    // Storing the trailing slash makes the root behave as the directory every
    // caller means it to be.
    if (!normalized.isEmpty() && !normalized.path().endsWith(QLatin1Char('/')))
        normalized.setPath(normalized.path() + QLatin1Char('/'));
    iconRootStorage() = normalized;
}

QUrl loomResolveIconSource(const QUrl &source)
{
    // A scheme ("qrc:", "file:") or a leading slash means the caller already
    // said exactly where the asset is; only a bare relative path is ours to
    // complete. With no root configured a relative source is passed through
    // unchanged, so the provider reports the unreadable path rather than a
    // silently invented one.
    if (source.isEmpty() || !source.isRelative()
        || source.path().startsWith(QLatin1Char('/')))
        return source;
    const QUrl root = iconRootStorage();
    return root.isEmpty() ? source : root.resolved(source);
}

QString loomIconProviderName()
{
    return QStringLiteral("loom");
}

QUrl loomIconUrl(const QUrl &source, const QColor &color)
{
    // HexArgb round-trips through QColor's own parser, so a translucent tint
    // survives; mid(1) drops the '#' the provider puts back.
    const QString tint =
        color.isValid() ? color.name(QColor::HexArgb).mid(1) : QString(untinted);
    return QUrl(QStringLiteral("image://%1/%2/%3")
                    .arg(
                        loomIconProviderName(), tint,
                        QString::fromUtf8(QUrl::toPercentEncoding(source.toString()))));
}
