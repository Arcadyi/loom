#include "commands.h"

#include "buildrunner.h"
#include "commandline.h"
#include "devserver.h"
#include "devsession.h"
#include "projectmanifest.h"
#include "projectscaffolder.h"
#include "toolchaindoctor.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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
                QStringLiteral("No respin.json found in this directory or its parents");
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
        QStringLiteral(".respin/build/%1-%2").arg(target, config.toLower()));
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

// respin_add_application routes application binaries into bin/ so that a target
// name never collides with a build-tree directory such as CTest's Testing/.
// The legacy top-level location is still accepted for build trees configured by
// an older respin.
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

// The prefix respin itself was installed into, or an empty string when running
// from a build tree where no CMake package exists. Generated projects do
// find_package(respin CONFIG REQUIRED), so without this every build in a fresh
// tree fails unless the user passes --prefix by hand.
QString installedPackagePrefix()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const auto prefix = QDir::cleanPath(binaryDirectory.filePath(QStringLiteral("..")));
    const auto config = QDir(prefix).filePath(
        QStringLiteral(RESPIN_RELATIVE_CMAKE_DIR)
        + QStringLiteral("/respinConfig.cmake"));
    if (!QFileInfo(config).isFile())
        return {};
    return prefix;
}

// CMAKE_PREFIX_PATH replaces rather than appends, so the user's value and the
// inferred one have to be combined into a single definition.
QStringList cmakePrefixArguments(const QString &userPrefix)
{
    QStringList prefixes;
    if (!userPrefix.isEmpty())
        prefixes.append(QDir::fromNativeSeparators(userPrefix));
    const auto inferred = installedPackagePrefix();
    if (!inferred.isEmpty() && !prefixes.contains(inferred))
        prefixes.append(inferred);
    if (prefixes.isEmpty())
        return {};
    return {QStringLiteral("-DCMAKE_PREFIX_PATH=") + prefixes.join(QLatin1Char(';'))};
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
         QStringLiteral("Extra CMake prefix path. respin finds its own package.")},
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
};

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
    // 'dekstop'" instead of sending the user to the roadmap with "the dekstop
    // adapter is not implemented".
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
    if (!manifest.selectApplication(
            parsed.value(QStringLiteral("app")), context.application, &error)) {
        return reportError(error);
    }
    // Gives the manifest's "platforms" list its first actual use.
    if (!context.application.platforms.isEmpty()
        && !context.application.platforms.contains(context.target)) {
        return reportError(
            QStringLiteral("application '%1' does not list '%2' in its platforms (%3)")
                .arg(
                    context.application.target, context.target,
                    context.application.platforms.join(QStringLiteral(", "))));
    }
    if (context.target != QStringLiteral("desktop")) {
        return reportError(
            QStringLiteral("the %1 adapter is not implemented in this build")
                .arg(context.target));
    }

    context.buildDirectory =
        buildDirectoryFor(context.root, context.target, context.configuration);
    context.cmakeArguments = cmakePrefixArguments(parsed.value(QStringLiteral("prefix")));
    context.generator =
        parsed.value(QStringLiteral("generator"), QStringLiteral("Ninja"));
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
        QTextStream(stdout) << "respin " << RESPIN_VERSION << Qt::endl;
        return cli::Success;
    }

    const auto command = arguments.first();
    const auto tail = arguments.sliced(1);
    if (command == QStringLiteral("new"))
        return createProject(tail);
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
    QTextStream(stderr) << "respin: unknown command '" << command
                        << "'; run 'respin help'" << Qt::endl;
    return cli::UsageError;
}

int Commands::createProject(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("new"),
        .summary = QStringLiteral("Create a Qt/QML application."),
        .usage = QStringLiteral(
            "respin new <name> [--org dev.example] "
            "[--directory path] [--loom]"),
        .options =
            {
                {QStringLiteral("org"), QStringLiteral("id"),
                 QStringLiteral("Reverse-DNS organization (default: dev.example).")},
                {QStringLiteral("directory"), QStringLiteral("path"),
                 QStringLiteral("Where to create the project (default: ./<name>).")},
                {QStringLiteral("ci"), QStringLiteral("provider"),
                 QStringLiteral(
                     "Generate a CI workflow: github, or none "
                     "(default: none).")},
                {QStringLiteral("loom"), QString(),
                 QStringLiteral(
                     "Pre-wire the loom styling library (find_package, "
                     "linking, a styled starter UI).")},
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

    const ProjectScaffolder::Options options{
        .organization =
            parsed.value(QStringLiteral("org"), QStringLiteral("dev.example")),
        .githubWorkflow = ci == QStringLiteral("github"),
        .loom = parsed.isSet(QStringLiteral("loom")),
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
           << QDir::toNativeSeparators(destination)
           << "\n  respin doctor\n  respin dev\n";
    if (options.githubWorkflow) {
        output << "\nThe generated .github/workflows/ci.yml has one step marked TODO: "
                  "respin has no published release to pin, so you must supply the "
                  "install step yourself.\n";
    }
    if (options.loom) {
        output << "\nThe project links the loom styling library. If configure cannot "
                  "find it, point CMAKE_PREFIX_PATH at a loom build or install "
                  "tree.\n";
    }
    return cli::Success;
}

int Commands::initializeProject(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("init"),
        .summary = QStringLiteral("Add a respin manifest to an existing CMake project."),
        .usage = QStringLiteral("respin init [options] [--apply]"),
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
                     "Application to emit CMake for, when respin.json "
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
    const auto manifestPath = QDir(destination).filePath(QStringLiteral("respin.json"));
    if (!QFileInfo::exists(cmakePath)) {
        return reportError(
            QStringLiteral("respin init requires an existing CMakeLists.txt"));
    }

    QFile cmakeInput(cmakePath);
    if (!cmakeInput.open(QIODevice::ReadOnly))
        return reportError(cmakeInput.errorString());
    const auto cmakeContents = QString::fromUtf8(cmakeInput.readAll());
    cmakeInput.close();

    const auto alreadyIntegrated =
        cmakeContents.contains(QStringLiteral("# respin: begin"));
    const auto manifestExists = QFileInfo::exists(manifestPath);
    if (manifestExists && alreadyIntegrated) {
        QTextStream(stdout) << "respin is already initialized in " << destination
                            << Qt::endl;
        return cli::Success;
    }

    QString name;
    ApplicationDefinition application;
    if (manifestExists) {
        // An existing manifest is the source of truth. Re-inferring from a
        // CMakeLists regex could append a block naming a different target than
        // the one `respin dev` reads out of respin.json.
        ProjectManifest existing;
        QString error;
        if (!ProjectManifest::load(manifestPath, existing, &error))
            return reportError(error);
        name = existing.projectName();
        if (!existing.selectApplication(
                parsed.value(QStringLiteral("app")), application, &error)) {
            return reportError(error);
        }
        QTextStream(stdout) << "Using the existing respin.json for target "
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
            "\n# respin: begin\n"
            "find_package(respin CONFIG REQUIRED)\n"
            "respin_enable_hot_reload(\n"
            "    TARGET %1\n"
            "    URI %2\n"
            "    ENTRY %3\n"
            "    QML_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/%4\"\n")
            .arg(application.target, application.uri, application.entry, qmlRoot);
    if (!application.assetRoots.isEmpty()) {
        block += QStringLiteral("    ASSET_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/%1\"\n")
                     .arg(application.assetRoots.constFirst());
    }
    block += QStringLiteral(")\n# respin: end\n");

    QTextStream output(stdout);
    output << "CMake integration preview:\n" << block << Qt::endl;
    if (!parsed.isSet(QStringLiteral("apply"))) {
        output
            << (manifestExists ? QStringLiteral("Left CMakeLists.txt unchanged. ")
                               : QStringLiteral(
                                     "Created respin.json without "
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
    output << "Initialized respin for target " << application.target << ".\n";
    return cli::Success;
}

int Commands::doctor(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("doctor"),
        .summary = QStringLiteral("Check the selected platform toolchain."),
        .usage = QStringLiteral("respin doctor [--target platform] [--json]"),
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
        .usage = QStringLiteral("respin setup [--target platform]"),
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
        .usage = QStringLiteral("respin build [options]"),
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
        context.buildDirectory, context.application.target, context.configuration);
}

int Commands::lint(const QStringList &arguments)
{
    const CommandSpec spec{
        .name = QStringLiteral("lint"),
        .summary = QStringLiteral("Run qmllint over the project's QML."),
        .usage = QStringLiteral("respin lint [options]"),
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

    // qt_add_qml_module generates a <target>_qmllint target that already knows
    // the module's import paths and qmltypes. Driving that is far more reliable
    // than reconstructing -I flags by hand.
    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            context.cmakeArguments, context.generator)) {
        return status;
    }
    return BuildRunner::build(
        context.buildDirectory, context.application.target + QStringLiteral("_qmllint"),
        context.configuration);
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
        .usage = QStringLiteral("respin fmt [--check] [options]"),
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
            "qmlformat was not found on PATH; it ships with Qt (run 'respin doctor')"));
    }

    const auto files = qmlFilesOf(context);
    if (files.isEmpty()) {
        cli::reportProgress(QStringLiteral("no QML files to format"));
        return cli::Success;
    }

    if (!parsed.isSet(QStringLiteral("check"))) {
        return BuildRunner::run(
            qmlformat, QStringList{QStringLiteral("--inplace")} + files);
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
        if (original.readAll() != process.readAllStandardOutput())
            unformatted.append(QDir(context.root).relativeFilePath(file));
    }

    if (unformatted.isEmpty()) {
        cli::reportProgress(
            QStringLiteral("%1 QML files are formatted").arg(files.size()));
        return cli::Success;
    }
    QTextStream(stderr) << "respin: these files are not formatted:\n  "
                        << unformatted.join(QStringLiteral("\n  ")) << "\n"
                        << "run 'respin fmt' to rewrite them" << Qt::endl;
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
        .summary = QStringLiteral("Remove respin's build and deploy trees."),
        .usage = QStringLiteral("respin clean [--all] [options]"),
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

    // Only ever respin's own directory: never the source tree, and never a
    // build tree somebody configured elsewhere.
    QStringList targets;
    if (parsed.isSet(QStringLiteral("all"))) {
        targets.append(QDir(context.root).filePath(QStringLiteral(".respin")));
    } else {
        targets.append(context.buildDirectory);
        targets.append(
            QDir(context.root)
                .filePath(QStringLiteral(".respin/dist/%1-%2")
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
        .usage = QStringLiteral("respin test [options]"),
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
    // OFF. Setting it explicitly is what makes `respin test` mean "with tests".
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
        .usage = QStringLiteral("respin dev [options] [-- <app arguments>]"),
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
            << "respin: warning: " << config
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
    if (const auto status = BuildRunner::build(buildDirectory, app.target, config))
        return status;

    const auto executable = executablePathFor(buildDirectory, app.target);
    if (!QFileInfo(executable).isFile()) {
        return reportError(
            QStringLiteral("built executable was not found at %1").arg(executable));
    }

    DevSession session(
        DevSession::Configuration{
            .projectRoot = root,
            .application = app,
            .buildDirectory = buildDirectory,
            .buildConfiguration = config,
            .cmakeArguments = extra,
            .generator = context.generator,
            .executable = executable,
            .applicationArguments = parsed.passthrough(),
        });
    const auto status = session.run(&error);
    if (!error.isEmpty())
        return reportError(error);
    return status;
}

int Commands::deploy(const QStringList &arguments)
{
    auto options = buildOptions();
    options.append(
        {QStringLiteral("output"), QStringLiteral("path"),
         QStringLiteral("Install prefix (default: .respin/dist/<target>-<config>).")});
    options.append(
        {QStringLiteral("package"),
         {},
         QStringLiteral("Also produce an archive with CPack.")});

    const CommandSpec spec{
        .name = QStringLiteral("deploy"),
        .summary = QStringLiteral("Build and install the application into a prefix."),
        .usage = QStringLiteral("respin deploy [options]"),
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
    // absolute here means `respin deploy --output dist` behaves.
    auto output = parsed.value(QStringLiteral("output"));
    if (output.isEmpty()) {
        output = QDir(context.root)
                     .filePath(QStringLiteral(".respin/dist/%1-%2")
                                   .arg(context.target, context.configuration.toLower()));
    }
    output = QFileInfo(output).absoluteFilePath();

    if (const auto status = BuildRunner::configure(
            context.root, context.buildDirectory, context.configuration,
            context.cmakeArguments, context.generator)) {
        return status;
    }
    if (const auto status = BuildRunner::build(
            context.buildDirectory, context.application.target, context.configuration)) {
        return status;
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
                  << "See respin_install_application in respinFunctions.cmake.\n";
    return cli::Success;
}

void Commands::printHelp() const
{
    QTextStream(stdout)
        << "respin " << RESPIN_VERSION
        << " — fast Qt/QML projects\n\n"
           "Usage: respin <command> [options]\n\n"
           "Commands:\n"
           "  new <name>  Create a Qt/QML application\n"
           "  init        Add a respin manifest to an existing CMake project\n"
           "  doctor      Check the selected platform toolchain\n"
           "  setup       Show the confirmed setup plan\n"
           "  dev         Build, run, watch, and hot-reload\n"
           "  build       Configure and build the application\n"
           "  lint        Run qmllint over the project's QML\n"
           "  fmt         Format the project's QML with qmlformat\n"
           "  clean       Remove respin's build and deploy trees\n"
           "  test        Build and run the project's tests\n"
           "  deploy      Package and deploy for a target\n\n"
           "Common options:\n"
           "  --target <desktop|android|ios|embedded>\n"
           "  --config <Debug|Release|RelWithDebInfo|MinSizeRel>\n"
           "  --prefix <extra CMake prefix path>\n\n"
           "Run 'respin <command> --help' for a command's own options.\n";
}
