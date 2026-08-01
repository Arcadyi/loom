#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Offline access to the `Lo.style` vocabulary, behind `loom style` and the
// second half of `loom lint`.
//
// The checker exists because Loom's own diagnostics are necessarily runtime
// ones: an unknown class warns when the styled item is created, in a log nobody
// is watching, and only for code paths that actually run. Parsing the QML for
// literals catches the same typos at build time.
namespace stylecheck {

struct Finding {
    QString file;
    int line = 0;
    int column = 0;
    QString klass;
    QString code;
    QString message;
    bool error = true;
};

// Every *.qml under `path`, or `path` itself when it names a file. Sorted, so
// findings come out in a stable order.
QStringList qmlFilesUnder(const QString &path);

// Unknown utility classes in the `Lo.style` literals of one file. Sets `error`
// and returns nothing when the file cannot be read.
QList<Finding> checkFile(const QString &path, QString *error);

} // namespace stylecheck
