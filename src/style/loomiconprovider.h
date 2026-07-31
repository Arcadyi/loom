#pragma once

#include <QQuickImageProvider>
#include <QUrl>

class QColor;

// Serves an image asset repainted in a caller-chosen color, under the provider
// name "loom". Registered on every engine that instantiates the `Loom`
// singleton, so `Loom.icon()` URLs resolve with no application wiring.
//
// It exists because Qt exposes no reachable recolouring hook for a control's
// icon. The icon item only tints when it is a mask, and a plain file source
// never is, so `icon.color` is accepted and then silently ignored. Recolouring
// on the way out of the provider instead works for every consumer that takes a
// URL -- `icon.source`, `Image.source` -- without overriding a delegate.
class LoomIconProvider : public QQuickImageProvider {
public:
    LoomIconProvider();

    // `id` is "<tint>/<percent-encoded source url>" as built by loomIconUrl().
    QImage
    requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Builds the image://loom/... URL LoomIconProvider answers. An invalid color
// means "no recolour": the asset is served exactly as authored.
QUrl loomIconUrl(const QUrl &source, const QColor &color);

// The base that a relative Loom.icon() source resolves against, set once via
// Loom.iconRoot or a config's "iconRoot". Empty by default, which leaves a
// relative source alone -- and unreadable, which the provider warns about.
QUrl loomIconRoot();
void setLoomIconRoot(const QUrl &root);

// `source` resolved against the icon root when it is relative. A source that
// carries a scheme or starts at the filesystem root is already absolute and
// comes back untouched, so a call site can always opt out by passing one.
QUrl loomResolveIconSource(const QUrl &source);

// The name LoomIconProvider is registered under, i.e. the host part of the
// URLs above.
QString loomIconProviderName();
