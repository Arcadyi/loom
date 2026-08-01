#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

// Public C++ entry points for applications that want to configure Loom before
// (or without) touching QML. Everything here is also reachable from QML via
// the `Loom` singleton.
namespace loom {

// Library version, e.g. "0.1.0".
const char *version();

// Switch the active theme ("light" and "dark" are built in; configs can add
// more). Unknown names warn and leave the theme unchanged.
void setTheme(const QString &name);
QString theme();

// Load a JSON config (the tailwind.config equivalent): custom colors and
// spacing, breakpoint thresholds, themes, defaultTheme. Merges into the
// defaults; everything styled re-resolves. Call before the engine loads for a
// flicker-free start, or at any later point. Returns false when the file
// cannot be read or parsed. Also available from QML: Loom.loadConfig(url).
bool loadConfig(const QString &filePath);

// Replaces the active config instead of merging into it: tokens reset to the
// built-in set before the file is applied, so a token deleted from the file
// stops resolving. `loom dev` calls this on every save of the project's design
// file, which is what lets an edit repaint the running window without the scene
// being recreated. A file that fails to parse changes nothing.
bool reloadConfig(const QString &filePath);

// As reloadConfig(), for a config that is not on disk where it belongs: `loom
// dev` receives the design document over the wire. A relative `iconRoot`
// resolves against `basePath` -- the design file's path inside the project --
// instead of against wherever the bytes happen to live, which is what keeps
// icons working across a hot design reload.
bool reloadConfigData(const QByteArray &json, const QString &basePath);

// Directory that a relative `Loom.icon()` source resolves against. Set it
// before the engine loads the scene: icon() bindings track colors, not this,
// so a later change will not repaint icons already on screen. Also reachable
// from QML as `Loom.iconRoot` and from a config's "iconRoot".
void setIconRoot(const QUrl &root);
QUrl iconRoot();

} // namespace loom
