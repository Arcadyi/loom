#pragma once

#include <QString>
#include <QStringList>

class BuildRunner {
public:
    static int run(const QString &program, const QStringList &arguments);
    // `generator` is only honoured for a build tree that has no cache yet.
    // Re-passing -G to a tree configured by a different generator makes CMake
    // hard-error, which turned "I tried Makefiles once" into a tree that could
    // never be built again without deleting it by hand.
    static int configure(
        const QString &sourceDirectory, const QString &buildDirectory,
        const QString &configuration, const QStringList &extraArguments = {},
        const QString &generator = QStringLiteral("Ninja"));
    static int build(
        const QString &buildDirectory, const QString &target = {},
        const QString &configuration = {});
};
