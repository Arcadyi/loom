#include "commands.h"

#include "buildrunner.h"
#include "commandline.h"
#include "devserver.h"
#include "devsession.h"
#include "languageserverproxy.h"
#include "projectmanifest.h"
#include "projectscaffolder.h"
#include "stylecheck.h"
#include "styleorder.h"
#include "toolchaindoctor.h"

#include <loom/loom.h>
#include <loom/loomcatalogue.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <memory>

namespace {

using cli::CommandSpec;
using cli::OptionSpec;
using cli::ParsedCommand;
using cli::ParseOutcome;
using cli::reportError;

const QStringList &knownTargets()
{
    static const QStringList targets{
        QStringLiteral("desktop"),
        QStringLiteral("android"),
        QStringLiteral("ios"),
        QStringLiteral("embedded"),
    };
    return targets;
}

// One validator for both the build directory name and -DCMAKE_BUILD_TYPE.
// Previously the directory used config.toLower() while the cache value used the
// raw string, so "Debug" and "debug" shared a directory but wrote different
// cache entries -- a full rebuild every time you alternated. It also means
// "--config ../../../tmp/x" can no longer traverse out of the build path.
QString normalizedConfiguration(const QString &value, QString *error)
{
    static const QStringList known{
        QStringLiteral("Debug"),
        QStringLiteral("Release"),
        QStringLiteral("RelWithDebInfo"),
        QStringLiteral("MinSizeRel"),
    };
    for (const auto &candidate : known) {
        if (value.compare(candidate, Qt::CaseInsensitive) == 0)
            return candidate;
    }
    if (error) {
        *error = QStringLiteral("unknown configuration '%1'; expected one of %2")
                     .arg(value, known.join(QStringLiteral(", ")));
    }
    return {};
}

bool loadCurrentProject(QString &manifestPath, ProjectManifest &manifest, QString *error)
{
    manifestPath = findManifest(QDir::currentPath());
    if (manifestPath.isEmpty()) {
        if (error)
            *error =
                QStringLiteral("No loom.json found in this directory or its parents");
        return false;
    }
    return ProjectManifest::load(manifestPath, manifest, error);
}

bool isKnownTarget(const QString &target)
{
    return knownTargets().contains(target);
}

QString
buildDirectoryFor(const QString &root, const QString &target, const QString &config)
{
    return QDir(root).filePath(
        QStringLiteral(".loom/build/%1-%2").arg(target, config.toLower()));
}

// Release and RelWithDebInfo both define NDEBUG, and the generated
// src/main.cpp gates enableDevelopmentRuntime() on #ifndef NDEBUG. Such a build
// launches, serves bundles, and never reloads, with nothing to explain why.
bool definesNDebug(const QString &configuration)
{
    return configuration == QStringLiteral("Release")
        || configuration == QStringLiteral("RelWithDebInfo")
        || configuration == QStringLiteral("MinSizeRel");
}

// loom_add_application routes application binaries into bin/ so that a target
// name never collides with a build-tree directory such as CTest's Testing/.
// The legacy top-level location is still accepted for build trees configured by
// an older loom.
QString executablePathFor(const QString &buildDirectory, const QString &target)
{
    auto name = target;
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    const QDir directory(buildDirectory);
#ifdef Q_OS_MACOS
    // The template sets MACOSX_BUNDLE, so on macOS bin/<T> does not exist at
    // all and dev failed with "built executable was not found" *after* a
    // successful build. Probed first because a stale plain binary may linger.
    const auto bundled = directory.filePath(
        QStringLiteral("bin/") + name + QStringLiteral(".app/Contents/MacOS/") + name);
    if (QFileInfo(bundled).isFile())
        return bundled;
#endif
    const auto preferred = directory.filePath(QStringLiteral("bin/") + name);
    // isFile(), not exists(): a *directory* named after the target sits at
    // exactly this path when the bin/ redirect is missing.
    if (QFileInfo(preferred).isFile())
        return preferred;
    const auto legacy = directory.filePath(name);
    if (QFileInfo(legacy).isFile())
        return legacy;
    return preferred;
}

// Executable suffixes and bundle layout are properties of the *target* system,
// not the host running loom. In particular a Windows host cross-compiling
// embedded Linux must not look for Target.exe merely because loom.exe itself
// was built on Windows.
QString embeddedExecutablePathFor(const QString &buildDirectory, const QString &target)
{
    const QDir directory(buildDirectory);
    const QString preferred = directory.filePath(QStringLiteral("bin/") + target);
    if (QFileInfo(preferred).isFile())
        return preferred;
    const QString legacy = directory.filePath(target);
    return QFileInfo(legacy).isFile() ? legacy : preferred;
}

// The prefix loom itself was installed into, or an empty string when running
// from a build tree where no CMake package exists. Generated projects do
// find_package(loom CONFIG REQUIRED), so without this every build in a fresh
// tree fails unless the user passes --prefix by hand.
QString installedPackagePrefix()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const auto prefix = QDir::cleanPath(binaryDirectory.filePath(QStringLiteral("..")));
    const auto config = QDir(prefix).filePath(
        QStringLiteral(LOOM_RELATIVE_CMAKE_DIR) + QStringLiteral("/loomConfig.cmake"));
    if (!QFileInfo(config).isFile())
        return {};
    return prefix;
}

// Every target that builds against a Qt other than the host's, and so needs a
// loom built for it rather than the one this CLI was installed beside.
bool isCrossTarget(const QString &target)
{
    return target == QLatin1String("android") || target == QLatin1String("ios")
        || target == QLatin1String("embedded");
}

// Qt packages an Android application through a build target of its own, and the
// APK it produces is what deploy, dev and test all go looking for afterwards.
// Building the application target alone leaves just the shared library that
// belongs inside the package, and every adapter then reports a build that
// succeeded with nothing to install. The packaging target depends on the
// application, so naming it builds both.
QString buildTargetFor(const QString &platform, const QString &application)
{
    if (platform == QLatin1String("android") && !application.isEmpty())
        return application + QStringLiteral("_make_apk");
    return application;
}

// CMAKE_PREFIX_PATH replaces rather than appends, so the user's value and the
// inferred one have to be combined into a single definition.
//
// Cross targets need the same list in CMAKE_FIND_ROOT_PATH as well. A sysroot
// build sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE to ONLY -- CMake's own
// Platform/Darwin.cmake does it for the iOS SDK -- which re-roots every
// find_package search path, including an explicit PATHS ... NO_DEFAULT_PATH,
// underneath the target sysroot. A prefix outside it is then invisible however
// it was named. Toolchain files append to this variable, so seeding it from the
// command line leaves the sysroot they add intact.
QStringList cmakePrefixArguments(const QString &userPrefix, const QString &target)
{
    QStringList prefixes;
    if (!userPrefix.isEmpty())
        prefixes.append(QDir::fromNativeSeparators(userPrefix));
    const auto inferred = installedPackagePrefix();
    if (!inferred.isEmpty() && !prefixes.contains(inferred))
        prefixes.append(inferred);
    if (prefixes.isEmpty())
        return {};
    const auto joined = prefixes.join(QLatin1Char(';'));
    QStringList arguments{QStringLiteral("-DCMAKE_PREFIX_PATH=") + joined};
    if (isCrossTarget(target))
        arguments.append(QStringLiteral("-DCMAKE_FIND_ROOT_PATH=") + joined);
    return arguments;
}

// Options shared by every command that configures or builds a project.
QList<OptionSpec> buildOptions()
{
    return {
        {QStringLiteral("target"), QStringLiteral("platform"),
         QStringLiteral("Platform to build for (default: desktop).")},
        {QStringLiteral("config"), QStringLiteral("configuration"),
         QStringLiteral("CMake build type (default: Debug).")},
        {QStringLiteral("prefix"), QStringLiteral("path"),
         QStringLiteral("Extra CMake prefix path. loom finds its own package.")},
        {QStringLiteral("app"), QStringLiteral("target"),
         QStringLiteral("Application to act on in a multi-application project.")},
        {QStringLiteral("generator"), QStringLiteral("name"),
         QStringLiteral("CMake generator for a fresh build tree (default: Ninja).")},
        {QStringLiteral("verbose"), {}, QStringLiteral("Print more detail.")},
        {QStringLiteral("quiet"), {}, QStringLiteral("Print only errors and results.")},
    };
}

// Applied once per command, before anything is printed.
void applyVerbosity(const ParsedCommand &parsed)
{
    if (parsed.isSet(QStringLiteral("quiet")))
        cli::setVerbosity(cli::Verbosity::Quiet);
    else if (parsed.isSet(QStringLiteral("verbose")))
        cli::setVerbosity(cli::Verbosity::Verbose);
}

// What every build/test/dev/deploy invocation needs after validation, so the
// checks live in one place instead of being copied four times.
struct ProjectContext {
    QString root;
    ApplicationDefinition application;
    QString target;
    QString configuration;
    QString buildDirectory;
    QStringList cmakeArguments;
    QString generator;
    // Absolute path to the manifest's design token file, empty when it declares
    // none. Resolved here rather than at each use so every command anchors it
    // to the manifest instead of the working directory.
    QString designPath;
    QJsonObject platformOptions;
    QJsonObject embeddedProfile;
};

QString findArtifact(const QString &directory, const QStringList &suffixes)
{
    QStringList matches;
    QDirIterator iterator(
        directory, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        for (const QString &suffix : suffixes) {
            if (path.endsWith(suffix, Qt::CaseInsensitive)) {
                matches.append(path);
                break;
            }
        }
    }
    matches.sort();
    return matches.isEmpty() ? QString() : matches.constLast();
}

QString findBundle(const QString &directory, const QString &name)
{
    QDirIterator iterator(
        directory, QStringList{name + QStringLiteral(".app")}, QDir::Dirs,
        QDirIterator::Subdirectories);
    QStringList matches;
    while (iterator.hasNext())
        matches.append(iterator.next());
    matches.sort();
    return matches.isEmpty() ? QString() : matches.constLast();
}

QStringList adbArguments(const QJsonObject &options)
{
    const QString device = options.value(QStringLiteral("device")).toString();
    return device.isEmpty() ? QStringList{} : QStringList{QStringLiteral("-s"), device};
}

QString embeddedHost(const QJsonObject &profile)
{
    const QString host = profile.value(QStringLiteral("host")).toString();
    const QString user = profile.value(QStringLiteral("user")).toString();
    return user.isEmpty() ? host : user + QLatin1Char('@') + host;
}

QStringList sshArguments(const QJsonObject &profile)
{
    QStringList arguments;
    const int port = profile.value(QStringLiteral("port")).toInt(22);
    if (port != 22)
        arguments.append({QStringLiteral("-p"), QString::number(port)});
    arguments.append(embeddedHost(profile));
    return arguments;
}

// Rewrites the `Lo.style` literals of one file into canonical class order.
// Returns the number of literals changed, or -1 on an I/O failure.
//
// Driven off the AST literal ranges lspdocument already produces rather than a
// regular expression, so a class string inside a comment or a comparison is
// not touched -- and off *ranges* rather than decoded values, so the rewrite
// is exact even for escaped content. Concatenations and ternaries are handled
// for free: each literal fragment is its own range.
int sortStyleLiterals(const QString &path, bool apply, bool *changed)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    const QString source = QString::fromUtf8(input.readAll());
    input.close();

    lsp::Document document;
    document.open(source, 1);
    const auto literals = document.styleLiterals();

    QString updated = source;
    int rewritten = 0;
    // Back to front, so an earlier replacement cannot shift a later offset.
    for (auto it = literals.crbegin(); it != literals.crend(); ++it) {
        const qsizetype length = it->content.end - it->content.start;
        if (length <= 0)
            continue;
        const QString text = source.mid(it->content.start, length);
        const QString sorted = loom::styleorder::canonicalOrder(text);
        if (sorted == text)
            continue;
        updated.replace(it->content.start, length, sorted);
        ++rewritten;
    }

    if (changed)
        *changed = rewritten > 0;
    if (!apply || rewritten == 0)
        return rewritten;

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
        return -1;
    output.write(updated.toUtf8());
    return output.commit() ? rewritten : -1;
}

// Shared by `loom lint` and `loom style`. Loads the project's design tokens
// first, so a class built from a project-defined token (bg-brand-500) is not
// reported as unknown. A default-constructed context means "no project", which
// is how an explicit path outside any project is checked.
int runStyleCheck(
    const ProjectContext &context, const QStringList &files, bool json = false)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!context.designPath.isEmpty() && !loom::loadConfig(context.designPath)) {
        return reportError(
            QStringLiteral("could not load design tokens %1").arg(context.designPath));
    }

    QList<stylecheck::Finding> findings;
    for (const auto &file : files) {
        QString error;
        findings.append(stylecheck::checkFile(file, &error));
        if (!error.isEmpty())
            return reportError(error);
    }

    int errors = 0;
    int warnings = 0;
    QJsonArray diagnostics;
    for (const auto &finding : findings) {
        if (finding.error)
            ++errors;
        else
            ++warnings;
        if (json) {
            diagnostics.append(
                QJsonObject{
                    {QStringLiteral("file"), finding.file},
                    {QStringLiteral("line"), finding.line},
                    {QStringLiteral("column"), finding.column},
                    {QStringLiteral("severity"),
                     finding.error ? QStringLiteral("error") : QStringLiteral("warning")},
                    {QStringLiteral("code"), finding.code},
                    {QStringLiteral("class"), finding.klass},
                    {QStringLiteral("message"), finding.message},
                });
        } else {
            err << finding.file << ':' << finding.line << ':' << finding.column << ": "
                << (finding.error ? "error" : "warning") << ": " << finding.message
                << " [" << finding.code << "]\n";
        }
    }
    if (json) {
        out << QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("filesChecked"), files.size()},
                       {QStringLiteral("errors"), errors},
                       {QStringLiteral("warnings"), warnings},
                       {QStringLiteral("diagnostics"), diagnostics},
                   })
                   .toJson(QJsonDocument::Indented);
    } else if (!findings.isEmpty()) {
        err << "loom: " << errors << " error(s), " << warnings << " warning(s) in "
            << files.size() << " file(s)\n";
    }
    if (findings.isEmpty() && !json)
        out << "loom: " << files.size() << " file(s) checked, no findings\n";
    return errors > 0 ? cli::Failure : cli::Success;
}

// Every QML file the manifest declares, which is what qmlformat and the
// fallback qmllint path operate on.
QStringList qmlFilesOf(const ProjectContext &context)
{
    QStringList files;
    for (const auto &root : context.application.qmlRoots) {
        QDirIterator iterator(
            QDir(context.root).filePath(root), QStringList{QStringLiteral("*.qml")},
            QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (iterator.hasNext())
            files.append(iterator.next());
    }
    files.sort();
    return files;
}

// Returns cli::Success and fills `context`, or an exit code to return directly.
int resolveProjectContext(
    const ParsedCommand &parsed, const CommandSpec &spec, ProjectContext &context)
{
    context.target = parsed.value(QStringLiteral("target"), QStringLiteral("desktop"));
    // Checked against the known list first, so a typo says "unknown target
    // 'dekstop'" rather than looking like a platform-adapter failure.
    if (!isKnownTarget(context.target)) {
        return cli::reportUsageError(
            spec,
            QStringLiteral("unknown target '%1'; expected one of %2")
                .arg(context.target, knownTargets().join(QStringLiteral(", "))));
    }

    QString error;
    context.configuration = normalizedConfiguration(
        parsed.value(QStringLiteral("config"), QStringLiteral("Debug")), &error);
    if (context.configuration.isEmpty())
        return cli::reportUsageError(spec, error);

    QString manifestPath;
    ProjectManifest manifest;
    if (!loadCurrentProject(manifestPath, manifest, &error))
        return reportError(error);

    context.root = QFileInfo(manifestPath).absolutePath();
    context.designPath = manifest.resolvedDesignPath(manifestPath);
    if (!manifest.selectApplication(
            parsed.value(QStringLiteral("app")), context.application, &error)) {
        return reportError(error);
    }
    if (!context.application.platforms.contains(context.target)) {
        return reportError(
            QStringLiteral("application '%1' does not list '%2' in its platforms (%3)")
                .arg(
                    context.application.target, context.target,
                    context.application.platforms.join(QStringLiteral(", "))));
    }
    context.buildDirectory =
        buildDirectoryFor(context.root, context.target, context.configuration);
    context.cmakeArguments =
        cmakePrefixArguments(parsed.value(QStringLiteral("prefix")), context.target);
    context.generator =
        parsed.value(QStringLiteral("generator"), QStringLiteral("Ninja"));
    context.platformOptions =
        context.application.platformOptions.value(context.target).toObject();

    const auto absoluteOption = [&context](const QString &value) {
        if (value.isEmpty() || QFileInfo(value).isAbsolute())
            return value;
        return QDir(context.root).absoluteFilePath(value);
    };
    if (context.target == QLatin1String("android")) {
        QString qtPath =
            context.platformOptions.value(QStringLiteral("qtPath")).toString();
        if (qtPath.isEmpty())
            qtPath = qEnvironmentVariable("QT_ANDROID_PATH");
        qtPath = absoluteOption(qtPath);
        if (qtPath.isEmpty())
            return reportError(QStringLiteral(
                "android platforms.qtPath (or QT_ANDROID_PATH) is required"));
        const QString toolchain =
            QDir(qtPath).filePath(QStringLiteral("lib/cmake/Qt6/qt.toolchain.cmake"));
        if (!QFileInfo(toolchain).isFile())
            return reportError(QStringLiteral("Qt Android toolchain was not found at %1")
                                   .arg(toolchain));
        context.cmakeArguments.append(
            QStringLiteral("-DCMAKE_TOOLCHAIN_FILE=") + toolchain);
        QString hostQt =
            context.platformOptions.value(QStringLiteral("hostQtPath")).toString();
        if (hostQt.isEmpty())
            hostQt = qEnvironmentVariable("QT_HOST_PATH");
        if (!hostQt.isEmpty())
            context.cmakeArguments.append(
                QStringLiteral("-DQT_HOST_PATH=") + absoluteOption(hostQt));
        QString abi = context.platformOptions.value(QStringLiteral("abi")).toString();
        const auto abis = context.platformOptions.value(QStringLiteral("abis")).toArray();
        if (abi.isEmpty())
            abi = abis.isEmpty() ? QStringLiteral("arm64-v8a") : abis.at(0).toString();
        const int api = context.platformOptions.value(QStringLiteral("api")).toInt(36);
        context.cmakeArguments.append(QStringLiteral("-DANDROID_ABI=") + abi);
        if (!abis.isEmpty()) {
            QStringList abiNames;
            for (const auto &entry : abis)
                abiNames.append(entry.toString());
            context.cmakeArguments.append(
                QStringLiteral("-DQT_ANDROID_ABIS=") + abiNames.join(QLatin1Char(';')));
        }
        context.cmakeArguments.append(
            QStringLiteral("-DANDROID_PLATFORM=android-") + QString::number(api));
    } else if (context.target == QLatin1String("ios")) {
#if !defined(Q_OS_MACOS)
        return reportError(QStringLiteral("the iOS adapter requires macOS and Xcode"));
#else
        QString qtPath =
            context.platformOptions.value(QStringLiteral("qtPath")).toString();
        if (qtPath.isEmpty())
            qtPath = qEnvironmentVariable("QT_IOS_PATH");
        qtPath = absoluteOption(qtPath);
        if (qtPath.isEmpty())
            return reportError(
                QStringLiteral("ios platforms.qtPath (or QT_IOS_PATH) is required"));
        const QString toolchain =
            QDir(qtPath).filePath(QStringLiteral("lib/cmake/Qt6/qt.toolchain.cmake"));
        if (!QFileInfo(toolchain).isFile())
            return reportError(
                QStringLiteral("Qt iOS toolchain was not found at %1").arg(toolchain));
        context.cmakeArguments.append(
            QStringLiteral("-DCMAKE_TOOLCHAIN_FILE=") + toolchain);
        QString hostQt =
            context.platformOptions.value(QStringLiteral("hostQtPath")).toString();
        if (hostQt.isEmpty())
            hostQt = qEnvironmentVariable("QT_HOST_PATH");
        if (!hostQt.isEmpty())
            context.cmakeArguments.append(
                QStringLiteral("-DQT_HOST_PATH=") + absoluteOption(hostQt));
        const QString destination =
            context.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"));
        const QString sdk = context.platformOptions.value(QStringLiteral("sdk"))
                                .toString(
                                    destination == QLatin1String("device")
                                        ? QStringLiteral("iphoneos")
                                        : QStringLiteral("iphonesimulator"));
        context.cmakeArguments.append(QStringLiteral("-DCMAKE_OSX_SYSROOT=") + sdk);
        const QString team =
            context.platformOptions.value(QStringLiteral("team")).toString();
        if (!team.isEmpty()) {
            context.cmakeArguments.append(
                QStringLiteral("-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=") + team);
        }
        if (!parsed.isSet(QStringLiteral("generator")))
            context.generator = QStringLiteral("Xcode");
#endif
    } else if (context.target == QLatin1String("embedded")) {
        const QString profileName =
            context.platformOptions.value(QStringLiteral("profile")).toString();
        context.embeddedProfile =
            manifest.embeddedProfiles().value(profileName).toObject();
        if (profileName.isEmpty() || context.embeddedProfile.isEmpty())
            return reportError(QStringLiteral(
                "embedded platforms.profile must name an embeddedProfiles entry"));
        const QString toolchain = absoluteOption(
            context.embeddedProfile.value(QStringLiteral("toolchainFile")).toString());
        if (toolchain.isEmpty() || !QFileInfo(toolchain).isFile())
            return reportError(
                QStringLiteral("embedded profile '%1' has no usable toolchainFile")
                    .arg(profileName));
        context.cmakeArguments.append(
            QStringLiteral("-DCMAKE_TOOLCHAIN_FILE=") + toolchain);
        const QString sysroot = absoluteOption(
            context.embeddedProfile.value(QStringLiteral("sysroot")).toString());
        if (!sysroot.isEmpty())
            context.cmakeArguments.append(QStringLiteral("-DCMAKE_SYSROOT=") + sysroot);
        const QString hostQt = absoluteOption(
            context.embeddedProfile.value(QStringLiteral("hostQtPath")).toString());
        if (!hostQt.isEmpty())
            context.cmakeArguments.append(QStringLiteral("-DQT_HOST_PATH=") + hostQt);
    }
    // Always names the application, not just the manifest: which one gets built
    // used to be invisible, and was decided by alphabetical order.
    cli::reportProgress(QStringLiteral("using %1 (application %2)")
                            .arg(manifestPath, context.application.target));
    return cli::Success;
}

} // namespace

Commands::Commands(QObject *parent)
    : QObject(parent)
{
}

int Commands::execute(const QStringList &arguments)
{
    if (arguments.isEmpty() || arguments.first() == QStringLiteral("help")
        || arguments.first() == QStringLiteral("--help")
        || arguments.first() == QStringLiteral("-h")) {
        printHelp();
        return cli::Success;
    }
    if (arguments.first() == QStringLiteral("--version")
        || arguments.first() == QStringLiteral("-v")) {
        QTextStream(stdout) << "loom " << LOOM_VERSION_STR << Qt::endl;
        return cli::Success;
    }

    const auto command = arguments.first();
    const auto tail = arguments.sliced(1);
    if (command == QStringLiteral("new"))
        return createProject(tail);
    if (command == QStringLiteral("add"))
        return addSource(tail);
    if (command == QStringLiteral("init"))
        return initializeProject(tail);
    if (command == QStringLiteral("doctor"))
        return doctor(tail);
    if (command == QStringLiteral("setup"))
        return setup(tail);
    if (command == QStringLiteral("build"))
        return build(tail);
    if (command == QStringLiteral("lint"))
        return lint(tail);
    if (command == QStringLiteral("style"))
        return style(tail);
    if (command == QStringLiteral("lsp"))
        return languageServer(tail);
    if (command == QStringLiteral("fmt"))
        return format(tail);
    if (command == QStringLiteral("clean"))
        return clean(tail);
    if (command == QStringLiteral("test"))
        return test(tail);
    if (command == QStringLiteral("dev"))
        return develop(tail);
    if (command == QStringLiteral("deploy"))
        return deploy(tail);
    if (command == QStringLiteral("migrate"))
        return migrate(tail);
    QTextStream(stderr) << "loom: unknown command '" << command << "'; run 'loom help'"
                        << Qt::endl;
    return cli::UsageError;
}

int Commands::createProject(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("new"),
        .summary = QStringLiteral("Create a Qt/QML application."),
        .usage = QStringLiteral(
            "loom new <name> [--org dev.example] [--platforms desktop,android] "
            "[--directory path] [--ci github|none]"),
        .options =
            {
                {QStringLiteral("org"), QStringLiteral("id"),
                 QStringLiteral("Reverse-DNS organization (default: dev.example).")},
                {QStringLiteral("platforms"), QStringLiteral("list"),
                 QStringLiteral("Comma-separated platforms to target (default: %1).")
                     .arg(ProjectManifest::supportedPlatforms().join(QLatin1Char(',')))},
                {QStringLiteral("directory"), QStringLiteral("path"),
                 QStringLiteral("Where to create the project (default: ./<name>).")},
                {QStringLiteral("ci"), QStringLiteral("provider"),
                 QStringLiteral(
                     "Generate a CI workflow: github, or none "
                     "(default: none).")},
            },
        .minimumPositional = 1,
        .maximumPositional = 1,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    const auto name = parsed.positional().constFirst();
    QString nameError;
    if (!ProjectScaffolder::isValidProjectName(name, &nameError))
        return cli::reportUsageError(spec, nameError);

    const auto ci = parsed.value(QStringLiteral("ci"), QStringLiteral("none"));
    if (ci != QStringLiteral("none") && ci != QStringLiteral("github")) {
        return cli::reportUsageError(
            spec,
            QStringLiteral("unknown --ci provider '%1'; expected github or none")
                .arg(ci));
    }

    // Rebuilt in the order supportedPlatforms() declares rather than the order
    // they were typed, which drops repeats and keeps the list the adapters
    // report back to a user the same whatever they typed.
    QStringList platforms;
    const auto requestedPlatforms = parsed.value(QStringLiteral("platforms"));
    if (!requestedPlatforms.isEmpty()) {
        QStringList requested;
        for (const auto &entry : requestedPlatforms.split(QLatin1Char(','))) {
            const auto platform = entry.trimmed().toLower();
            if (platform.isEmpty())
                continue;
            if (!ProjectManifest::supportedPlatforms().contains(platform)) {
                return cli::reportUsageError(
                    spec,
                    QStringLiteral("unknown platform '%1'; expected one of %2")
                        .arg(
                            platform,
                            ProjectManifest::supportedPlatforms().join(
                                QStringLiteral(", "))));
            }
            requested.append(platform);
        }
        if (requested.isEmpty()) {
            return cli::reportUsageError(
                spec, QStringLiteral("--platforms needs at least one platform"));
        }
        for (const auto &platform : ProjectManifest::supportedPlatforms()) {
            if (requested.contains(platform))
                platforms.append(platform);
        }
    }

    const ProjectScaffolder::Options options{
        .organization =
            parsed.value(QStringLiteral("org"), QStringLiteral("dev.example")),
        .platforms = platforms,
        .githubWorkflow = ci == QStringLiteral("github"),
    };
    const auto destination =
        QFileInfo(
            parsed.value(QStringLiteral("directory"), QDir::current().filePath(name)))
            .absoluteFilePath();
    QString error;
    if (!ProjectScaffolder::create(name, destination, options, &error))
        return reportError(error);
    QTextStream output(stdout);
    output << "Created " << name << " in " << destination << "\n\n  cd "
           << QDir::toNativeSeparators(destination) << "\n  loom doctor\n  loom dev\n";
    output << "\nCLion: open the project directory and let CMake reload; QML "
              "language-server completion is preconfigured locally.\n";
    if (options.githubWorkflow) {
        output << "\nThe generated .github/workflows/ci.yml has one step marked TODO: "
                  "loom has no published release to pin, so you must supply the "
                  "install step yourself.\n";
    }
    return cli::Success;
}

int Commands::addSource(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("add"),
        .summary = QStringLiteral("Add a page or component to this project."),
        .usage = QStringLiteral("loom add <page|component> <Name> [--app name]"),
        .options =
            {
                {QStringLiteral("app"), QStringLiteral("name"),
                 QStringLiteral(
                     "Which application to add to, in a multi-application "
                     "project (default: the manifest's).")},
            },
        .minimumPositional = 2,
        .maximumPositional = 2,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    const auto kindName = parsed.positional().at(0);
    ProjectScaffolder::SourceKind kind{};
    if (kindName == QStringLiteral("page")) {
        kind = ProjectScaffolder::SourceKind::Page;
    } else if (kindName == QStringLiteral("component")) {
        kind = ProjectScaffolder::SourceKind::Component;
    } else {
        return cli::reportUsageError(
            spec,
            QStringLiteral("unknown kind '%1'; expected 'page' or 'component'")
                .arg(kindName));
    }

    const auto typeName = parsed.positional().at(1);
    QString error;
    if (!ProjectScaffolder::isValidTypeName(typeName, &error))
        return cli::reportUsageError(spec, error);

    // Deliberately not resolveProjectContext(): that resolves a build target
    // and refuses an application whose platforms do not list it. Writing a
    // source file has nothing to do with any target, and failing on one would
    // be a puzzle rather than a diagnostic.
    QString manifestPath;
    ProjectManifest manifest;
    if (!loadCurrentProject(manifestPath, manifest, &error))
        return reportError(error);

    ApplicationDefinition application;
    if (!manifest.selectApplication(
            parsed.value(QStringLiteral("app")), application, &error))
        return reportError(error);
    if (application.qmlRoots.isEmpty()) {
        return reportError(
            QStringLiteral("application '%1' declares no qmlRoots to add to")
                .arg(application.target));
    }

    // The first root: it is the one loom_add_application is given, and the one
    // `loom new` fills. A project with several has chosen a layout this command
    // has no way to guess between.
    const QString projectRoot = QFileInfo(manifestPath).absolutePath();
    const QString qmlRoot = QDir(projectRoot).filePath(application.qmlRoots.constFirst());

    QString created;
    if (!ProjectScaffolder::addSource(kind, typeName, qmlRoot, &created, &error))
        return reportError(error);

    QTextStream out(stdout);
    out << "created " << QDir(projectRoot).relativeFilePath(created) << '\n';
    // Said explicitly because the alternative -- silence -- reads as "you have
    // probably forgotten to register this somewhere".
    out << "no CMake change is needed: the QML root is globbed on the next "
           "configure.\n";
    return cli::Success;
}

int Commands::initializeProject(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("init"),
        .summary = QStringLiteral("Add a loom manifest to an existing CMake project."),
        .usage = QStringLiteral("loom init [options] [--apply]"),
        .options =
            {
                {QStringLiteral("name"), QStringLiteral("name"),
                 QStringLiteral("Project name (default: the directory name).")},
                {QStringLiteral("target"), QStringLiteral("name"),
                 QStringLiteral("CMake target holding the QML module.")},
                {QStringLiteral("uri"), QStringLiteral("dotted.uri"),
                 QStringLiteral("QML module URI.")},
                {QStringLiteral("entry"), QStringLiteral("type"),
                 QStringLiteral("Root QML type name (default: Main).")},
                {QStringLiteral("qml-root"), QStringLiteral("path"),
                 QStringLiteral("Directory holding QML sources.")},
                {QStringLiteral("asset-root"), QStringLiteral("path"),
                 QStringLiteral("Directory holding bundled assets.")},
                {QStringLiteral("app-id"), QStringLiteral("id"),
                 QStringLiteral("Reverse-DNS application identifier.")},
                {QStringLiteral("app"), QStringLiteral("target"),
                 QStringLiteral(
                     "Application to emit CMake for, when loom.json "
                     "already defines several.")},
                {QStringLiteral("apply"),
                 {},
                 QStringLiteral(
                     "Write the CMake integration block instead of "
                     "only previewing it.")},
            },
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    const auto destination = QDir::currentPath();
    const auto cmakePath = QDir(destination).filePath(QStringLiteral("CMakeLists.txt"));
    const auto manifestPath = QDir(destination).filePath(QStringLiteral("loom.json"));
    if (!QFileInfo::exists(cmakePath)) {
        return reportError(
            QStringLiteral("loom init requires an existing CMakeLists.txt"));
    }

    QFile cmakeInput(cmakePath);
    if (!cmakeInput.open(QIODevice::ReadOnly))
        return reportError(cmakeInput.errorString());
    const auto cmakeContents = QString::fromUtf8(cmakeInput.readAll());
    cmakeInput.close();

    const auto alreadyIntegrated =
        cmakeContents.contains(QStringLiteral("# loom: begin"));
    const auto manifestExists = QFileInfo::exists(manifestPath);
    if (manifestExists && alreadyIntegrated) {
        QTextStream(stdout) << "loom is already initialized in " << destination
                            << Qt::endl;
        return cli::Success;
    }

    QString name;
    ApplicationDefinition application;
    if (manifestExists) {
        // An existing manifest is the source of truth. Re-inferring from a
        // CMakeLists regex could append a block naming a different target than
        // the one `loom dev` reads out of loom.json.
        ProjectManifest existing;
        QString error;
        if (!ProjectManifest::load(manifestPath, existing, &error))
            return reportError(error);
        name = existing.projectName();
        if (!existing.selectApplication(
                parsed.value(QStringLiteral("app")), application, &error)) {
            return reportError(error);
        }
        QTextStream(stdout) << "Using the existing loom.json for target "
                            << application.target << Qt::endl;
    } else {
        const QRegularExpression qmlModulePattern(QStringLiteral(
            "qt_add_qml_module\\s*\\(\\s*([^\\s\\)]+)[\\s\\S]{0,4096}?"
            "\\bURI\\s+\"?([A-Za-z_][A-Za-z0-9_.]*)\"?"));
        const auto moduleMatch = qmlModulePattern.match(cmakeContents);
        name = parsed.value(QStringLiteral("name"), QFileInfo(destination).fileName());
        application.name = name;
        application.target = parsed.value(
            QStringLiteral("target"),
            moduleMatch.hasMatch() ? moduleMatch.captured(1) : QString());
        application.uri = parsed.value(
            QStringLiteral("uri"),
            moduleMatch.hasMatch() ? moduleMatch.captured(2) : QString());
        if (application.target.isEmpty() || application.uri.isEmpty()) {
            return reportError(QStringLiteral(
                "could not infer the QML target and URI; pass --target <name> --uri "
                "<dotted.uri>"));
        }
        application.entry = parsed.value(QStringLiteral("entry"), QStringLiteral("Main"));
        application.qmlRoots = {parsed.value(
            QStringLiteral("qml-root"),
            QFileInfo::exists(QDir(destination).filePath(QStringLiteral("qml")))
                ? QStringLiteral("qml")
                : QStringLiteral("."))};
        const auto assetRoot = parsed.value(
            QStringLiteral("asset-root"),
            QFileInfo::exists(QDir(destination).filePath(QStringLiteral("assets")))
                ? QStringLiteral("assets")
                : QString());
        application.assetRoots =
            assetRoot.isEmpty() ? QStringList{} : QStringList{assetRoot};
        application.id =
            parsed.value(QStringLiteral("app-id"), QString(application.uri).toLower());
        application.platforms = knownTargets();

        auto manifest = ProjectManifest::create(name, application);
        QString error;
        if (!manifest.save(manifestPath, &error))
            return reportError(error);
    }

    const auto qmlRoot = application.qmlRoots.isEmpty()
        ? QStringLiteral(".")
        : application.qmlRoots.constFirst();
    QString block =
        QStringLiteral(
            "\n# loom: begin\n"
            "find_package(loom CONFIG REQUIRED)\n"
            "loom_enable_hot_reload(\n"
            "    TARGET %1\n"
            "    URI %2\n"
            "    ENTRY %3\n"
            "    QML_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/%4\"\n")
            .arg(application.target, application.uri, application.entry, qmlRoot);
    if (!application.assetRoots.isEmpty()) {
        block += QStringLiteral("    ASSET_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/%1\"\n")
                     .arg(application.assetRoots.constFirst());
    }
    block += QStringLiteral(")\n# loom: end\n");

    QTextStream output(stdout);
    output << "CMake integration preview:\n" << block << Qt::endl;
    if (!parsed.isSet(QStringLiteral("apply"))) {
        output
            << (manifestExists ? QStringLiteral("Left CMakeLists.txt unchanged. ")
                               : QStringLiteral(
                                     "Created loom.json without "
                                     "changing CMakeLists.txt. "))
            << "Re-run with --apply to append the marked integration block.\n";
        return cli::Success;
    }
    if (alreadyIntegrated) {
        output << "The marked CMake integration block already exists.\n";
        return cli::Success;
    }

    // Read-modify-QSaveFile rather than QIODevice::Append: a short append leaves
    // a truncated CMake block in a file the user did not ask us to break.
    QSaveFile cmakeOutput(cmakePath);
    if (!cmakeOutput.open(QIODevice::WriteOnly))
        return reportError(cmakeOutput.errorString());
    const auto updated = (cmakeContents + block).toUtf8();
    if (cmakeOutput.write(updated) != updated.size() || !cmakeOutput.commit())
        return reportError(cmakeOutput.errorString());
    output << "Initialized loom for target " << application.target << ".\n";
    return cli::Success;
}

int Commands::doctor(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("doctor"),
        .summary = QStringLiteral("Check the selected platform toolchain."),
        .usage = QStringLiteral("loom doctor [--target platform] [--json]"),
        .options =
            {
                buildOptions().constFirst(),
                {QStringLiteral("json"),
                 {},
                 QStringLiteral(
                     "Emit the report as JSON, which also makes it "
                     "scriptable.")},
            },
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);
    const auto target = parsed.value(QStringLiteral("target"), QStringLiteral("desktop"));
    if (!isKnownTarget(target)) {
        return cli::reportUsageError(
            spec,
            QStringLiteral("unknown target '%1'; expected one of %2")
                .arg(target, knownTargets().join(QStringLiteral(", "))));
    }
    return ToolchainDoctor::printReport(target, parsed.isSet(QStringLiteral("json")));
}

int Commands::setup(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("setup"),
        .summary = QStringLiteral("Show the confirmed setup plan."),
        .usage = QStringLiteral("loom setup [--target platform]"),
        .options = {buildOptions().constFirst()},
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);
    const auto target = parsed.value(QStringLiteral("target"), QStringLiteral("desktop"));
    if (!isKnownTarget(target)) {
        return cli::reportUsageError(
            spec,
            QStringLiteral("unknown target '%1'; expected one of %2")
                .arg(target, knownTargets().join(QStringLiteral(", "))));
    }
    return ToolchainDoctor::printSetupPlan(target);
}

int Commands::build(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("build"),
        .summary = QStringLiteral("Configure and build the application."),
        .usage = QStringLiteral("loom build [options]"),
        .options = buildOptions(),
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // BuildRunner's 127 (could not start) and 128 (crashed) carry information a
    // collapsed "return 1" threw away.
    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            context.cmakeArguments, context.generator)) {
        return status;
    }
    return BuildRunner::build(
        context.buildDirectory,
        buildTargetFor(context.target, context.application.target),
        context.configuration);
}

int Commands::lint(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("lint"),
        .summary =
            QStringLiteral("Run qmllint and check Lo.style classes over the project."),
        .usage = QStringLiteral("loom lint [options]"),
        // `loom style` has had --json since it shipped and lint has not, so a
        // CI job wanting structured output had to run the style half twice --
        // once through lint for the qmllint pass, once through style to parse
        // the result.
        .options = buildOptions()
            + QList<OptionSpec>{
                {QStringLiteral("json"),
                 {},
                 QStringLiteral("Emit machine-readable diagnostics as JSON.")},
            },
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // qt_add_qml_module generates a <target>_qmllint target that already knows
    // the module's import paths and qmltypes. Driving that is far more reliable
    // than reconstructing -I flags by hand.
    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            context.cmakeArguments, context.generator)) {
        return status;
    }
    const auto qmllintStatus = BuildRunner::build(
        context.buildDirectory, context.application.target + QStringLiteral("_qmllint"),
        context.configuration);

    // Both halves always run, and the worse status wins. Stopping at the first
    // failure would hide every utility-class typo behind one qmllint complaint,
    // which is exactly the round trip this command exists to avoid.
    const auto styleStatus = runStyleCheck(
        context, qmlFilesOf(context), parsed.isSet(QStringLiteral("json")));
    return qmllintStatus != cli::Success ? qmllintStatus : styleStatus;
}

int Commands::style(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("style"),
        .summary = QStringLiteral("Check or list the Lo.style utility vocabulary."),
        .usage = QStringLiteral("loom style [--check | --catalogue] [path...]"),
        .options =
            {
                {QStringLiteral("check"),
                 {},
                 QStringLiteral(
                     "Report unknown classes in Lo.style literals "
                     "(the default).")},
                {QStringLiteral("catalogue"),
                 {},
                 QStringLiteral(
                     "Print the utility vocabulary as JSON, for editor "
                     "completion.")},
                {QStringLiteral("json"),
                 {},
                 QStringLiteral("Emit machine-readable diagnostics as JSON.")},
                {QStringLiteral("design"), QStringLiteral("path"),
                 QStringLiteral(
                     "Load a schema-v2 design file for tokens, recipes, and lint "
                     "policy.")},
                {QStringLiteral("app"), QStringLiteral("target"),
                 QStringLiteral("Application whose qmlRoots to check.")},
            },
        .minimumPositional = 0,
        // Any number of explicit paths; without one the manifest's qmlRoots are
        // used, which is what makes a bare `loom style` work in a project.
        .maximumPositional = -1,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }

    // The catalogue is the whole vocabulary, so it needs no project at all --
    // an editor asking for completion data should not have to be inside one.
    // A --config-less catalogue is still the built-in vocabulary, which is the
    // useful answer.
    if (parsed.isSet(QStringLiteral("catalogue"))) {
        if (parsed.isSet(QStringLiteral("check"))) {
            return cli::reportUsageError(
                spec, QStringLiteral("--check and --catalogue are mutually exclusive"));
        }
        const QString design = parsed.value(QStringLiteral("design"));
        if (!design.isEmpty() && !loom::loadConfig(QFileInfo(design).absoluteFilePath()))
            return reportError(
                QStringLiteral("could not load design tokens %1").arg(design));
        QTextStream(stdout) << QString::fromUtf8(loom::styleCatalogueJson());
        return cli::Success;
    }

    // Explicit paths skip the manifest entirely, so the checker can be pointed
    // at a directory outside any project.
    const auto paths = parsed.positional();
    if (!paths.isEmpty()) {
        QStringList files;
        for (const auto &path : paths) {
            if (!QFileInfo::exists(path))
                return reportError(QStringLiteral("no such path %1").arg(path));
            files.append(stylecheck::qmlFilesUnder(path));
        }
        ProjectContext explicitContext;
        const QString design = parsed.value(QStringLiteral("design"));
        if (!design.isEmpty())
            explicitContext.designPath = QFileInfo(design).absoluteFilePath();
        return runStyleCheck(
            explicitContext, files, parsed.isSet(QStringLiteral("json")));
    }

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;
    const QString design = parsed.value(QStringLiteral("design"));
    if (!design.isEmpty())
        context.designPath = QFileInfo(design).absoluteFilePath();
    return runStyleCheck(
        context, qmlFilesOf(context), parsed.isSet(QStringLiteral("json")));
}

int Commands::languageServer(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("lsp"),
        .summary = QStringLiteral("Run the qmlls proxy with Lo.style IntelliSense."),
        .usage = QStringLiteral("loom lsp [--qmlls path] [-- qmlls arguments...]"),
        .options =
            {
                {QStringLiteral("qmlls"), QStringLiteral("path"),
                 QStringLiteral("Real qmlls executable (otherwise auto-discovered).")},
            },
        .minimumPositional = 0,
        .maximumPositional = 0,
        .acceptsPassthrough = true,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }

    lsp::LanguageServerProxy proxy;
    return proxy.run(parsed.value(QStringLiteral("qmlls")), parsed.passthrough());
}

int Commands::format(const QStringList &arguments)
{
    auto options = buildOptions();
    options.append(
        {QStringLiteral("check"),
         {},
         QStringLiteral("Report unformatted files instead of rewriting them.")});

    const CommandSpec spec{
        .name = QStringLiteral("fmt"),
        .summary = QStringLiteral("Format the project's QML with qmlformat."),
        .usage = QStringLiteral("loom fmt [--check] [options]"),
        .options = options,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    const auto qmlformat = QStandardPaths::findExecutable(QStringLiteral("qmlformat"));
    if (qmlformat.isEmpty()) {
        return reportError(QStringLiteral(
            "qmlformat was not found on PATH; it ships with Qt (run 'loom doctor')"));
    }

    const auto files = qmlFilesOf(context);
    if (files.isEmpty()) {
        cli::reportProgress(QStringLiteral("no QML files to format"));
        return cli::Success;
    }

    if (!parsed.isSet(QStringLiteral("check"))) {
        const auto status = BuildRunner::run(
            qmlformat, QStringList{QStringLiteral("--inplace")} + files);
        if (status != cli::Success)
            return status;
        // qmlformat rewrites QML; it has no idea what is inside a string. The
        // class order is the other half of "formatted" in a loom project.
        int sorted = 0;
        for (const auto &file : files) {
            const int rewritten = sortStyleLiterals(file, true, nullptr);
            if (rewritten < 0)
                return reportError(
                    QStringLiteral("could not rewrite class order in %1").arg(file));
            sorted += rewritten;
        }
        if (sorted > 0) {
            cli::reportProgress(
                QStringLiteral("reordered %1 class string(s)").arg(sorted));
        }
        return cli::Success;
    }

    // qmlformat has no check mode, so compare its output with the file. Done
    // here rather than through BuildRunner because this needs the output
    // captured, not forwarded.
    QStringList unformatted;
    for (const auto &file : files) {
        QProcess process;
        process.start(qmlformat, QStringList{file});
        if (!process.waitForStarted() || !process.waitForFinished(30000)) {
            return reportError(QStringLiteral("could not run qmlformat on %1: %2")
                                   .arg(file, process.errorString()));
        }
        if (process.exitCode() != 0) {
            return reportError(
                QStringLiteral("qmlformat failed on %1:\n%2")
                    .arg(file, QString::fromUtf8(process.readAllStandardError())));
        }
        QFile original(file);
        if (!original.open(QIODevice::ReadOnly))
            return reportError(original.errorString());
        bool classesUnsorted = false;
        if (sortStyleLiterals(file, false, &classesUnsorted) < 0)
            return reportError(QStringLiteral("could not read %1").arg(file));
        if (original.readAll() != process.readAllStandardOutput() || classesUnsorted)
            unformatted.append(QDir(context.root).relativeFilePath(file));
    }

    if (unformatted.isEmpty()) {
        cli::reportProgress(
            QStringLiteral("%1 QML files are formatted").arg(files.size()));
        return cli::Success;
    }
    QTextStream(stderr) << "loom: these files are not formatted:\n  "
                        << unformatted.join(QStringLiteral("\n  ")) << "\n"
                        << "run 'loom fmt' to rewrite them" << Qt::endl;
    return cli::Failure;
}

int Commands::clean(const QStringList &arguments)
{
    auto options = buildOptions();
    options.append(
        {QStringLiteral("all"),
         {},
         QStringLiteral("Remove every configuration, not just the selected one.")});

    const CommandSpec spec{
        .name = QStringLiteral("clean"),
        .summary = QStringLiteral("Remove loom's build and deploy trees."),
        .usage = QStringLiteral("loom clean [--all] [options]"),
        .options = options,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // Only ever loom's own directory: never the source tree, and never a
    // build tree somebody configured elsewhere.
    QStringList targets;
    if (parsed.isSet(QStringLiteral("all"))) {
        targets.append(QDir(context.root).filePath(QStringLiteral(".loom")));
    } else {
        targets.append(context.buildDirectory);
        targets.append(
            QDir(context.root)
                .filePath(QStringLiteral(".loom/dist/%1-%2")
                              .arg(context.target, context.configuration.toLower())));
    }

    int removed = 0;
    for (const auto &path : targets) {
        if (!QFileInfo::exists(path))
            continue;
        if (!QDir(path).removeRecursively())
            return reportError(QStringLiteral("could not remove %1").arg(path));
        cli::reportProgress(QStringLiteral("removed %1").arg(path));
        ++removed;
    }
    if (removed == 0)
        cli::reportProgress(QStringLiteral("nothing to remove"));
    return cli::Success;
}

int Commands::test(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("test"),
        .summary = QStringLiteral("Build and run the project's tests."),
        .usage = QStringLiteral("loom test [options]"),
        .options = buildOptions(),
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // The generated template gates its tests on BUILD_TESTING, which
    // include(CTest) defaults to ON -- but only if nothing has already cached it
    // OFF. Setting it explicitly is what makes `loom test` mean "with tests".
    auto configureArguments = context.cmakeArguments;
    configureArguments.append(QStringLiteral("-DBUILD_TESTING=ON"));
    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            configureArguments, context.generator)) {
        return status;
    }
    if (const auto status =
            BuildRunner::build(context.buildDirectory, {}, context.configuration)) {
        return status;
    }
    if (context.target != QLatin1String("desktop")) {
        QTextStream(stdout)
            << "Cross-compiled the project and its test targets for " << context.target
            << ". Device UI smoke coverage is provided by the hosted emulator jobs; "
               "CTest execution remains a desktop-host operation.\n";
        return cli::Success;
    }
    // --no-tests=error: without it, a project whose tests silently stopped being
    // built reports a green run with zero tests.
    return BuildRunner::run(
        QStringLiteral("ctest"),
        {QStringLiteral("--test-dir"), context.buildDirectory,
         QStringLiteral("--output-on-failure"), QStringLiteral("--no-tests=error"),
         QStringLiteral("-C"), context.configuration});
}

int Commands::develop(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("dev"),
        .summary = QStringLiteral("Build, run, watch, and hot-reload the application."),
        .usage = QStringLiteral("loom dev [options] [-- <app arguments>]"),
        .options = buildOptions(),
        .acceptsPassthrough = true,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // dev used to hardcode Debug while advertising --config under "Common
    // options", so the flag was accepted and ignored.
    const auto config = context.configuration;
    if (definesNDebug(config)) {
        QTextStream(stderr)
            << "loom: warning: " << config
            << " defines NDEBUG, and the generated src/main.cpp enables the "
               "development runtime only when NDEBUG is undefined. This build will "
               "run and serve bundles but will not hot-reload.\n";
    }

    const auto root = context.root;
    const auto app = context.application;
    const auto buildDirectory = context.buildDirectory;
    const auto extra = context.cmakeArguments;
    QString error;
    if (const auto status = BuildRunner::configure(
            root, buildDirectory, config, extra, context.generator)) {
        return status;
    }
    if (const auto status = BuildRunner::build(
            buildDirectory, buildTargetFor(context.target, app.target), config)) {
        return status;
    }

    QString executable;
    if (context.target == QLatin1String("android"))
        executable = findArtifact(buildDirectory, {QStringLiteral(".apk")});
    else if (context.target == QLatin1String("ios"))
        executable = findBundle(buildDirectory, app.target);
    else if (context.target == QLatin1String("embedded"))
        executable = embeddedExecutablePathFor(buildDirectory, app.target);
    else
        executable = executablePathFor(buildDirectory, app.target);
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        return reportError(QStringLiteral("built %1 artifact was not found under %2")
                               .arg(context.target, buildDirectory));
    }

    DevSession session(
        DevSession::Configuration{
            .projectRoot = root,
            .application = app,
            .designPath = context.designPath,
            .buildDirectory = buildDirectory,
            .buildConfiguration = config,
            .cmakeArguments = extra,
            .generator = context.generator,
            .executable = executable,
            .applicationArguments = parsed.passthrough(),
            .platform = context.target,
            .platformOptions = context.platformOptions,
            .embeddedProfile = context.embeddedProfile,
        });
    const auto status = session.run(&error);
    if (!error.isEmpty())
        return reportError(error);
    return status;
}

int Commands::migrate(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("migrate"),
        .summary = QStringLiteral("Preview or apply the schema-v2 project migration."),
        .usage = QStringLiteral("loom migrate --to 2 [--apply]"),
        .options =
            {
                {QStringLiteral("to"), QStringLiteral("version"),
                 QStringLiteral("Target schema version; only 2 is supported.")},
                {QStringLiteral("apply"),
                 {},
                 QStringLiteral("Atomically write migrated files.")},
            },
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    if (parsed.value(QStringLiteral("to")) != QLatin1String("2"))
        return cli::reportUsageError(spec, QStringLiteral("--to must be 2"));

    const QString manifestPath = findManifest(QDir::currentPath());
    if (manifestPath.isEmpty())
        return reportError(
            QStringLiteral("No loom.json found in this directory or its parents"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly))
        return reportError(QStringLiteral("could not read %1").arg(manifestPath));
    QJsonParseError parseError;
    const QByteArray originalManifestBytes = manifestFile.readAll();
    // Closed before the rewrite below rather than at the end of the function:
    // QSaveFile::commit() replaces the original, and Windows refuses that with
    // "Access is denied" while this process still holds the file open. POSIX
    // renames over an open file without complaint, which is why leaving it to
    // the destructor looked correct.
    manifestFile.close();
    QJsonDocument manifestDocument =
        QJsonDocument::fromJson(originalManifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject())
        return reportError(QStringLiteral("invalid JSON in %1").arg(manifestPath));

    QJsonObject manifest = manifestDocument.object();
    const QJsonValue sourceVersion = manifest.value(QStringLiteral("schemaVersion"));
    if (!sourceVersion.isDouble() || sourceVersion.toDouble() != 1.0) {
        if (sourceVersion.isDouble() && sourceVersion.toDouble() == 2.0) {
            QTextStream(stdout) << manifestPath << " already uses schema v2.\n";
            return cli::Success;
        }
        return reportError(QStringLiteral("only project schema v1 can migrate to v2"));
    }
    manifest.insert(QStringLiteral("schemaVersion"), 2);
    manifest.insert(
        QStringLiteral("$schema"),
        QStringLiteral(
            "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/"
            "project-v2.schema.json"));
    QJsonObject applications = manifest.value(QStringLiteral("applications")).toObject();
    for (auto it = applications.begin(); it != applications.end(); ++it) {
        QJsonObject application = it->toObject();
        if (application.value(QStringLiteral("platforms")).isArray()) {
            QJsonObject platforms;
            for (const auto &entry :
                 application.value(QStringLiteral("platforms")).toArray())
                if (entry.isString())
                    platforms.insert(entry.toString(), QJsonObject{});
            application.insert(QStringLiteral("platforms"), platforms);
            it.value() = application;
        }
    }
    manifest.insert(QStringLiteral("applications"), applications);

    QString designPath;
    QJsonObject design;
    QByteArray originalDesignBytes;
    const QString relativeDesign = manifest.value(QStringLiteral("design")).toString();
    if (!relativeDesign.isEmpty()) {
        designPath = QFileInfo(QFileInfo(manifestPath).absolutePath(), relativeDesign)
                         .absoluteFilePath();
        QFile designFile(designPath);
        if (!designFile.open(QIODevice::ReadOnly))
            return reportError(QStringLiteral("could not read %1").arg(designPath));
        originalDesignBytes = designFile.readAll();
        QJsonDocument designDocument =
            QJsonDocument::fromJson(originalDesignBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !designDocument.isObject())
            return reportError(QStringLiteral("invalid JSON in %1").arg(designPath));
        const QJsonObject old = designDocument.object();
        const QJsonValue designVersion = old.value(QStringLiteral("schemaVersion"));
        if (designVersion.isDouble() && designVersion.toDouble() == 2.0) {
            design = old;
        } else {
            if (!designVersion.isUndefined()
                && (!designVersion.isDouble() || designVersion.toDouble() != 1.0)) {
                return reportError(
                    QStringLiteral("only design schema v1 can migrate to v2"));
            }
            design.insert(
                QStringLiteral("$schema"),
                QStringLiteral(
                    "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/"
                    "design-v2.schema.json"));
            design.insert(QStringLiteral("schemaVersion"), 2);
            QJsonObject tokens;
            for (const auto &family :
                 {QStringLiteral("colors"), QStringLiteral("space"),
                  QStringLiteral("breakpoints")}) {
                if (old.contains(family))
                    tokens.insert(family, old.value(family));
            }
            if (!tokens.isEmpty())
                design.insert(QStringLiteral("tokens"), tokens);
            QJsonObject themes;
            const QJsonObject oldThemes = old.value(QStringLiteral("themes")).toObject();
            for (auto themeIt = oldThemes.constBegin(); themeIt != oldThemes.constEnd();
                 ++themeIt) {
                const QJsonObject oldTheme = themeIt->toObject();
                QJsonObject converted;
                QJsonObject colors;
                for (auto value = oldTheme.constBegin(); value != oldTheme.constEnd();
                     ++value) {
                    if (value.key() == QLatin1String("extends")
                        || value.key() == QLatin1String("dark"))
                        converted.insert(value.key(), value.value());
                    else
                        colors.insert(value.key(), value.value());
                }
                if (!colors.isEmpty())
                    converted.insert(
                        QStringLiteral("tokens"),
                        QJsonObject{{QStringLiteral("colors"), colors}});
                themes.insert(themeIt.key(), converted);
            }
            if (!themes.isEmpty())
                design.insert(QStringLiteral("themes"), themes);
            design.insert(
                QStringLiteral("theme"),
                QJsonObject{
                    {QStringLiteral("default"),
                     old.value(QStringLiteral("defaultTheme"))
                         .toString(QStringLiteral("light"))},
                    {QStringLiteral("light"), QStringLiteral("light")},
                    {QStringLiteral("dark"), QStringLiteral("dark")},
                });
            if (old.contains(QStringLiteral("iconRoot")))
                design.insert(
                    QStringLiteral("iconRoot"), old.value(QStringLiteral("iconRoot")));
        }
    }

    const QByteArray manifestBytes =
        QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    const QByteArray designBytes = QJsonDocument(design).toJson(QJsonDocument::Indented);
    if (!parsed.isSet(QStringLiteral("apply"))) {
        QTextStream(stdout) << "Would migrate " << manifestPath << ":\n" << manifestBytes;
        if (!designPath.isEmpty())
            QTextStream(stdout) << "Would migrate " << designPath << ":\n" << designBytes;
        return cli::Success;
    }
    const auto write = [](const QString &path, const QByteArray &bytes, QString *error) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
            || !file.commit()) {
            *error = file.errorString();
            return false;
        }
        return true;
    };
    QString error;
    if (!designPath.isEmpty() && !write(designPath, designBytes, &error))
        return reportError(
            QStringLiteral("could not migrate %1: %2").arg(designPath, error));
    if (!write(manifestPath, manifestBytes, &error)) {
        const QString manifestError = error;
        if (!designPath.isEmpty()) {
            QString rollbackError;
            if (!write(designPath, originalDesignBytes, &rollbackError)) {
                return reportError(
                    QStringLiteral(
                        "could not migrate %1 (%2), and rollback of %3 also failed: %4")
                        .arg(manifestPath, manifestError, designPath, rollbackError));
            }
        }
        return reportError(
            QStringLiteral("could not migrate %1: %2; prior files were restored")
                .arg(manifestPath, manifestError));
    }
    QTextStream(stdout) << "Migrated project and design documents to schema v2.\n";
    return cli::Success;
}

int Commands::deploy(const QStringList &arguments)
{
    auto options = buildOptions();
    options.append(
        {QStringLiteral("output"), QStringLiteral("path"),
         QStringLiteral("Install prefix (default: .loom/dist/<target>-<config>).")});
    options.append(
        {QStringLiteral("package"),
         {},
         QStringLiteral("Also produce a native desktop package with CPack.")});

    const CommandSpec spec{
        .name = QStringLiteral("deploy"),
        .summary = QStringLiteral("Build and install the application into a prefix."),
        .usage = QStringLiteral("loom deploy [options]"),
        .options = options,
    };
    ParsedCommand parsed;
    switch (cli::parseCommand(spec, arguments, parsed)) {
    case ParseOutcome::HelpPrinted:
        return cli::Success;
    case ParseOutcome::Rejected:
        return cli::UsageError;
    case ParseOutcome::Ready:
        break;
    }
    applyVerbosity(parsed);

    ProjectContext context;
    if (const auto status = resolveProjectContext(parsed, spec, context))
        return status;

    // Qt's deploy script writes RPATHs and resolves runtime dependencies
    // relative to the prefix, and fails outright on a relative one. Making it
    // absolute here means `loom deploy --output dist` behaves.
    auto output = parsed.value(QStringLiteral("output"));
    if (output.isEmpty()) {
        output = QDir(context.root)
                     .filePath(QStringLiteral(".loom/dist/%1-%2")
                                   .arg(context.target, context.configuration.toLower()));
    }
    output = QFileInfo(output).absoluteFilePath();

    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            context.cmakeArguments, context.generator)) {
        return status;
    }
    if (const auto status = BuildRunner::build(
            context.buildDirectory,
            buildTargetFor(context.target, context.application.target),
            context.configuration)) {
        return status;
    }
    if (context.target != QLatin1String("desktop")
        && parsed.isSet(QStringLiteral("package"))) {
        return cli::reportUsageError(
            spec,
            QStringLiteral(
                "--package produces native desktop packages only; mobile and "
                "embedded deploy directly to their selected target"));
    }

    if (context.target == QLatin1String("android")) {
        const QString apk =
            findArtifact(context.buildDirectory, {QStringLiteral(".apk")});
        if (apk.isEmpty())
            return reportError(
                QStringLiteral("Android build succeeded but no APK was found under %1")
                    .arg(context.buildDirectory));
        QStringList adbInstallArguments = adbArguments(context.platformOptions);
        adbInstallArguments.append(
            {QStringLiteral("install"), QStringLiteral("-r"), apk});
        if (const int status =
                BuildRunner::run(QStringLiteral("adb"), adbInstallArguments))
            return status;
        QTextStream(stdout) << "Installed " << apk
                            << " on the selected Android device.\n";
        return cli::Success;
    }
    if (context.target == QLatin1String("ios")) {
        const QString bundle =
            findBundle(context.buildDirectory, context.application.target);
        if (bundle.isEmpty())
            return reportError(
                QStringLiteral(
                    "iOS build succeeded but no .app bundle was found under %1")
                    .arg(context.buildDirectory));
        const QString destination =
            context.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"));
        const QString device = context.platformOptions.value(QStringLiteral("device"))
                                   .toString(QStringLiteral("booted"));
        if (destination == QLatin1String("simulator")) {
            return BuildRunner::run(
                QStringLiteral("xcrun"),
                {QStringLiteral("simctl"), QStringLiteral("install"), device, bundle});
        }
        return BuildRunner::run(
            QStringLiteral("xcrun"),
            {QStringLiteral("devicectl"), QStringLiteral("device"),
             QStringLiteral("install"), QStringLiteral("app"), QStringLiteral("--device"),
             device, bundle});
    }
    if (context.target == QLatin1String("embedded")) {
        const QString executable =
            embeddedExecutablePathFor(context.buildDirectory, context.application.target);
        if (!QFileInfo(executable).isFile())
            return reportError(QStringLiteral("embedded executable was not found at %1")
                                   .arg(executable));
        const QString remoteDir =
            context.embeddedProfile.value(QStringLiteral("remoteDir")).toString();
        QStringList mkdirArguments = sshArguments(context.embeddedProfile);
        mkdirArguments.append(QStringLiteral("mkdir -p -- '%1'")
                                  .arg(QString(remoteDir).replace(
                                      QLatin1Char('\''), QStringLiteral("'\\''"))));
        if (const int status = BuildRunner::run(QStringLiteral("ssh"), mkdirArguments))
            return status;
        QStringList rsyncArguments{QStringLiteral("-az")};
        const int port = context.embeddedProfile.value(QStringLiteral("port")).toInt(22);
        if (port != 22)
            rsyncArguments.append(
                {QStringLiteral("-e"), QStringLiteral("ssh -p %1").arg(port)});
        rsyncArguments.append(
            {executable,
             embeddedHost(context.embeddedProfile) + QLatin1Char(':') + remoteDir
                 + QLatin1Char('/')});
        if (const int status = BuildRunner::run(QStringLiteral("rsync"), rsyncArguments))
            return status;
        QTextStream(stdout) << "Transferred " << executable << " to "
                            << embeddedHost(context.embeddedProfile) << ':' << remoteDir
                            << "/.\n";
        return cli::Success;
    }

    if (const auto status = BuildRunner::run(
            QStringLiteral("cmake"),
            {QStringLiteral("--install"), context.buildDirectory,
             QStringLiteral("--prefix"), output, QStringLiteral("--config"),
             context.configuration})) {
        return status;
    }

    if (parsed.isSet(QStringLiteral("package"))) {
        if (const auto status = BuildRunner::run(
                QStringLiteral("cpack"),
                {QStringLiteral("--config"),
                 QDir(context.buildDirectory)
                     .filePath(QStringLiteral("CPackConfig.cmake")),
                 QStringLiteral("-B"), output, QStringLiteral("-C"),
                 context.configuration})) {
            return status;
        }
    }

    QTextStream output_stream(stdout);
    output_stream << "Installed " << context.application.target << " into " << output
                  << "\n";
    // Said plainly rather than left for the user to discover on another machine.
    output_stream << "Qt is not bundled: this tree needs Qt " << QStringLiteral("6.11")
                  << " installed on the target host. "
                  << "See loom_install_application in loomFunctions.cmake.\n";
    return cli::Success;
}

void Commands::printHelp() const
{
    QTextStream(stdout)
        << "loom " << LOOM_VERSION_STR
        << " — fast Qt/QML projects\n\n"
           "Usage: loom <command> [options]\n\n"
           "Commands:\n"
           "  new <name>  Create a Qt/QML application\n"
           "  add <kind> <Name>\n"
           "              Add a page or component to this project\n"
           "  init        Add a loom manifest to an existing CMake project\n"
           "  doctor      Check the selected platform toolchain\n"
           "  setup       Show the confirmed setup plan\n"
           "  dev         Build, run, watch, and hot-reload\n"
           "  build       Configure and build the application\n"
           "  lint        Run qmllint and check Lo.style classes\n"
           "  style       Check Lo.style classes, or dump the vocabulary\n"
           "  lsp         Run qmlls with Lo.style IntelliSense\n"
           "  fmt         Format the project's QML with qmlformat\n"
           "  clean       Remove loom's build and deploy trees\n"
           "  test        Build and run the project's tests\n"
           "  deploy      Package and deploy for a target\n\n"
           "  migrate     Preview or apply schema migrations\n\n"
           "Common options:\n"
           "  --target <desktop|android|ios|embedded>\n"
           "  --config <Debug|Release|RelWithDebInfo|MinSizeRel>\n"
           "  --prefix <extra CMake prefix path>\n\n"
           "Run 'loom <command> --help' for a command's own options.\n";
}
