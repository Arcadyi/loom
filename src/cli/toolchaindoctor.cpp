#include "toolchaindoctor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

namespace {

constexpr int ProcessTimeoutMs = 5000;

ToolCheck executableCheck(
    const QString &name, const QString &executable, const QString &remediation,
    const bool validated = true)
{
    const auto path = QStandardPaths::findExecutable(executable);
    return ToolCheck{
        .name = name,
        .available = !path.isEmpty(),
        .detail = path,
        .remediation = remediation,
        .validated = validated,
    };
}

// A PATH-presence check and nothing more. Named so the report can say that
// plainly rather than implying the SDK behind it was inspected.
ToolCheck
presenceCheck(const QString &name, const QString &executable, const QString &remediation)
{
    return executableCheck(name, executable, remediation, false);
}

struct ProcessResult {
    bool ok = false;
    QString output;
    QString failure;
};

// The old version waited only on finished(), so a program that never started
// and one that exited non-zero both produced an empty string -- which the qmake
// check then reported as "not 6.11", an entirely wrong explanation. A hung
// process was also left running.
ProcessResult runTool(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(ProcessTimeoutMs)) {
        return {
            false,
            {},
            QStringLiteral("could not run %1: %2").arg(program, process.errorString())};
    }
    if (!process.waitForFinished(ProcessTimeoutMs)) {
        process.kill();
        process.waitForFinished(ProcessTimeoutMs);
        return {
            false,
            {},
            QStringLiteral("%1 did not respond within %2 seconds")
                .arg(program)
                .arg(ProcessTimeoutMs / 1000)};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {
            false,
            {},
            QStringLiteral("%1 exited with code %2")
                .arg(program)
                .arg(process.exitCode())};
    }
    return {true, QString::fromUtf8(process.readAllStandardOutput()).trimmed(), {}};
}

// The most common first-run failure, and the one check that was missing
// entirely: without a C++ compiler nothing else in the report matters.
ToolCheck compilerCheck()
{
    const QStringList candidates{
        qEnvironmentVariable("CXX"),
        QStringLiteral("c++"),
        QStringLiteral("g++"),
        QStringLiteral("clang++"),
    };
    for (const auto &candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        const auto path = QStandardPaths::findExecutable(candidate);
        if (path.isEmpty())
            continue;
        const auto result = runTool(path, {QStringLiteral("--version")});
        return ToolCheck{
            .name = QStringLiteral("C++ compiler"),
            .available = result.ok,
            .detail = result.ok ? path + QStringLiteral(" (")
                    + result.output.section(QLatin1Char('\n'), 0, 0) + QLatin1Char(')')
                                : path + QStringLiteral(": ") + result.failure,
            .remediation = QStringLiteral(
                "Install a C++20 compiler (g++ 12+ or clang++ 15+) and ensure it is "
                "on PATH, or set CXX."),
        };
    }
    return ToolCheck{
        .name = QStringLiteral("C++ compiler"),
        .available = false,
        .detail = {},
        .remediation = QStringLiteral(
            "Install a C++20 compiler (g++ 12+ or clang++ 15+) and ensure it is on "
            "PATH, or set CXX."),
    };
}

// qmake6 is the Linux distribution name; the official Qt installer ships plain
// qmake in its kit. Checking only the first meant every Qt-installer user got a
// "missing" for a Qt they had.
ToolCheck qtCheck()
{
    QString path;
    for (const auto &candidate : {QStringLiteral("qmake6"), QStringLiteral("qmake")}) {
        path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty())
            break;
    }
    const auto remediation = QStringLiteral(
        "Install a Qt 6.11 host kit with the official Qt installer or your OS "
        "provider.");
    if (path.isEmpty()) {
        return ToolCheck{
            .name = QStringLiteral("Qt qmake"),
            .available = false,
            .detail = {},
            .remediation = remediation,
        };
    }

    const auto result =
        runTool(path, {QStringLiteral("-query"), QStringLiteral("QT_VERSION")});
    if (!result.ok) {
        return ToolCheck{
            .name = QStringLiteral("Qt qmake"),
            .available = false,
            .detail = path + QStringLiteral(": ") + result.failure,
            .remediation = remediation,
        };
    }
    const bool matches = result.output.startsWith(QStringLiteral("6.11"));
    return ToolCheck{
        .name = QStringLiteral("Qt qmake"),
        .available = matches,
        .detail = path + QStringLiteral(" (Qt ") + result.output + QLatin1Char(')'),
        .remediation = matches
            ? QString()
            : QStringLiteral("Install and select a Qt 6.11 host kit; found %1.")
                  .arg(result.output),
    };
}

// The failure the owner actually hit: generated projects do
// find_package(loom CONFIG REQUIRED), so doctor should say when loom cannot
// find its own CMake package rather than leaving it to a raw CMake error.
ToolCheck packageCheck()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const auto prefix = QDir::cleanPath(binaryDirectory.filePath(QStringLiteral("..")));
    const auto config = QDir(prefix).filePath(
        QStringLiteral(LOOM_RELATIVE_CMAKE_DIR) + QStringLiteral("/loomConfig.cmake"));
    const bool found = QFileInfo(config).isFile();
    return ToolCheck{
        .name = QStringLiteral("loom CMake package"),
        .available = found,
        .detail = found ? config : QStringLiteral("not found at ") + config,
        .remediation = QStringLiteral(
            "Install loom (cmake --install <build> --prefix <prefix>) and run the "
            "installed binary, or pass --prefix <prefix> to build/test/dev."),
    };
}

// The styling half's own contract. loomConfig.cmake points generated projects
// at this directory so `import Loom` resolves for qmllint and qmlls; without
// the qmltypes the module still runs, but every styled file lints as unknown,
// which reads as a broken project rather than a missing file.
ToolCheck qmlModuleCheck()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const auto prefix = QDir::cleanPath(binaryDirectory.filePath(QStringLiteral("..")));
    // Derived from the CMake package directory rather than assuming "lib":
    // both are installed under CMAKE_INSTALL_LIBDIR, which may be lib, lib64 or
    // a multiarch triplet.
    const auto libraryDirectory =
        QFileInfo(QDir(prefix).filePath(QStringLiteral(LOOM_RELATIVE_CMAKE_DIR)))
            .absolutePath();
    const QDir module(
        QDir::cleanPath(QDir(libraryDirectory).filePath(QStringLiteral("../qml/Loom"))));
    const bool found = module.exists(QStringLiteral("qmldir"))
        && module.exists(QStringLiteral("loom.qmltypes"));
    return ToolCheck{
        .name = QStringLiteral("Loom QML module"),
        .available = found,
        .detail = found
            ? module.path()
            : QStringLiteral("qmldir/loom.qmltypes not found in ") + module.path(),
        .remediation = QStringLiteral(
            "Reinstall loom. Without this, `import Loom` still resolves at runtime, "
            "but qmllint and qmlls report every Loom type as unknown."),
    };
}

} // namespace

QList<ToolCheck> ToolchainDoctor::inspect(const QString &target)
{
    // Built as a list of named helpers. The old version mutated checks.last()
    // positionally, so inserting a check between qmake and its version
    // validation silently rewrote the wrong entry.
    QList<ToolCheck> checks{
        compilerCheck(),
        executableCheck(
            QStringLiteral("CMake"), QStringLiteral("cmake"),
            QStringLiteral(
                "Install CMake 3.22 or newer from cmake.org or your OS provider.")),
        executableCheck(
            QStringLiteral("Ninja"), QStringLiteral("ninja"),
            QStringLiteral("Install Ninja from ninja-build.org or your OS provider.")),
        executableCheck(
            QStringLiteral("CTest"), QStringLiteral("ctest"),
            QStringLiteral("CTest ships with CMake; install the full CMake package.")),
        qtCheck(),
        packageCheck(),
        qmlModuleCheck(),
    };

    if (target == QStringLiteral("android")) {
        checks.append(presenceCheck(
            QStringLiteral("Java"), QStringLiteral("java"),
            QStringLiteral("Install a 64-bit JDK 21.")));
        checks.append(presenceCheck(
            QStringLiteral("Android SDK manager"), QStringLiteral("sdkmanager"),
            QStringLiteral(
                "Install Android command-line tools, API 36, build-tools 36.0.0, and NDK "
                "27.2.12479018.")));
        checks.append(presenceCheck(
            QStringLiteral("ADB"), QStringLiteral("adb"),
            QStringLiteral("Install Android platform-tools.")));
    } else if (target == QStringLiteral("ios")) {
#if defined(Q_OS_MACOS)
        checks.append(presenceCheck(
            QStringLiteral("Xcode build tools"), QStringLiteral("xcodebuild"),
            QStringLiteral(
                "Install Xcode, launch it once, and select it with xcode-select.")));
#else
        checks.append(
            ToolCheck{
                .name = QStringLiteral("Xcode build tools"),
                .available = false,
                .detail = QStringLiteral("iOS builds require macOS; this host runs %1")
                              .arg(QSysInfo::prettyProductName()),
                .remediation = QStringLiteral("Build for iOS on a macOS host."),
            });
#endif
    } else if (target == QStringLiteral("embedded")) {
        checks.append(presenceCheck(
            QStringLiteral("SSH"), QStringLiteral("ssh"),
            QStringLiteral("Install an OpenSSH client.")));
        checks.append(presenceCheck(
            QStringLiteral("rsync"), QStringLiteral("rsync"),
            QStringLiteral("Install rsync on the host and target.")));
    }
    return checks;
}

int ToolchainDoctor::printReport(const QString &target, const bool asJson)
{
    const auto checks = inspect(target);
    const bool healthy =
        std::all_of(checks.cbegin(), checks.cend(), [](const ToolCheck &check) {
            return check.available;
        });

    if (asJson) {
        QJsonArray entries;
        for (const auto &check : checks) {
            entries.append(
                QJsonObject{
                    {QStringLiteral("name"), check.name},
                    {QStringLiteral("available"), check.available},
                    {QStringLiteral("detail"), check.detail},
                    {QStringLiteral("remediation"), check.remediation},
                    {QStringLiteral("validated"), check.validated},
                });
        }
        const QJsonObject report{
            {QStringLiteral("target"), target},
            {QStringLiteral("healthy"), healthy},
            {QStringLiteral("checks"), entries},
        };
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
        return healthy ? 0 : 1;
    }

    QTextStream output(stdout);
    QTextStream errors(stderr);
    output << "loom doctor — " << target << Qt::endl;
    for (const auto &check : checks) {
        output << (check.available ? "[ok]      " : "[missing] ") << check.name;
        if (!check.detail.isEmpty())
            output << ": " << check.detail;
        if (!check.validated)
            output << " (on PATH; version and SDK contents are not checked)";
        output << Qt::endl;
        if (!check.available)
            errors << "  " << check.remediation << Qt::endl;
    }
    return healthy ? 0 : 1;
}

int ToolchainDoctor::printSetupPlan(const QString &target)
{
    QTextStream output(stdout);
    output << "loom setup plan — " << target << "\n\n";
    bool hasActions = false;
    for (const auto &check : inspect(target)) {
        if (check.available)
            continue;
        hasActions = true;
        output << "- " << check.name << "\n  " << check.remediation << "\n";
    }
    if (!hasActions) {
        output << "No changes are required.\n";
        output.flush();
        return 0;
    }
    output << "\nNo downloads or license agreements have been accepted. "
              "Automatic provider execution will always require an explicit "
              "confirmation.\n";
    output.flush();
    // Non-zero while actions are outstanding, so a script can gate on it. The
    // old version always returned 0, which made it useless in CI.
    return 1;
}
