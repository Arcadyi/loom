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
