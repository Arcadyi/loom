#pragma once

#include <QString>

// Loads a Loom JSON config (the tailwind.config equivalent) into the token
// registry: custom colors, spacing steps, breakpoint thresholds, themes and
// the default theme. Extends-by-default: everything merges into what is
// already defined. Returns false when the file cannot be read or parsed;
// individual bad entries warn (category loom.config) and are skipped. On
// success the compile cache is dropped (the vocabulary may have grown) and
// every token consumer re-resolves.
bool loomLoadConfigFile(const QString &filePath);

// Same, but resets every token to the built-in set before applying the file, so
// a token the file no longer defines actually stops resolving. This is what
// `loom dev` drives on each save of the project's design file. A file that
// fails to parse leaves the previous tokens in place rather than resetting to
// the built-ins: keeping the last good set beats dropping the project's own.
bool loomReloadConfigFile(const QString &filePath);
