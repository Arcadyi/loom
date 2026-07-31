#include "buildrunner.h"

#include "commandline.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include <algorithm>

namespace {

// Quotes anything that would not survive a copy-paste back into a shell. The
// echoed line used a plain join(' '), so any path containing a space produced a
// command that looked runnable and was not.
QString shellQuoted(const QString &argument)
{
    static const QString safe = QStringLiteral(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "-_./=:,+@%^");
    const bool needsQuoting = argument.isEmpty()
        || std::any_of(argument.cbegin(), argument.cend(),
                       [](const QChar c) { return !safe.contains(c); });
    if (!needsQuoting)
        return argument;
    return QLatin1Char('\'')
        + QString(argument).replace(QStringLiteral("'"), QStringLiteral("'\\''"))
        + QLatin1Char('\'');
}

QString commandLineFor(const QString &program, const QStringList &arguments)
{
    QStringList rendered{shellQuoted(program)};
    for (const auto &argument : arguments)
        rendered.append(shellQuoted(argument));
    return rendered.join(QLatin1Char(' '));
}

} // namespace

int BuildRunner::run(const QString &program, const QStringList &arguments)
{
    // Suppressed by --quiet: this is progress, not a result.
    if (!cli::isQuiet())
        QTextStream(stdout) << "+ " << commandLineFor(program, arguments) << Qt::endl;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted()) {
        cli::reportError(
            QStringLiteral("could not start %1: %2").arg(program, process.errorString()));
        return 127;
    }
    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit)
        return 128;
    return process.exitCode();
}

int BuildRunner::configure(
    const QString &sourceDirectory, const QString &buildDirectory,
    const QString &configuration, const QStringList &extraArguments,
    const QString &generator)
{
    QStringList arguments{
        QStringLiteral("-S"),
        sourceDirectory,
        QStringLiteral("-B"),
        buildDirectory,
    };
    const auto cache = QDir(buildDirectory).filePath(QStringLiteral("CMakeCache.txt"));
    if (!QFileInfo::exists(cache) && !generator.isEmpty())
        arguments.append({QStringLiteral("-G"), generator});
    arguments.append(QStringLiteral("-DCMAKE_BUILD_TYPE=") + configuration);
    arguments.append(extraArguments);
    return run(QStringLiteral("cmake"), arguments);
}

int BuildRunner::build(
    const QString &buildDirectory, const QString &target, const QString &configuration)
{
    QStringList arguments{QStringLiteral("--build"), buildDirectory};
    if (!target.isEmpty())
        arguments.append({QStringLiteral("--target"), target});
    if (!configuration.isEmpty())
        arguments.append({QStringLiteral("--config"), configuration});
    return run(QStringLiteral("cmake"), arguments);
}
