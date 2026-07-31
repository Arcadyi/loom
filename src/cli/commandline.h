#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace cli {

// One place for exit codes, so a script can tell "you typed it wrong" from "the
// build failed". BuildRunner's 127 (could not start) and 128 (crashed) are
// propagated unchanged by the commands that invoke it.
inline constexpr int Success = 0;
inline constexpr int Failure = 1;
inline constexpr int UsageError = 2;

struct OptionSpec {
    QString name;      // long name, no leading dashes
    QString valueName; // empty means the option is a flag
    QString description;
};

struct CommandSpec {
    QString name;
    QString summary;
    QString usage;
    QList<OptionSpec> options;
    int minimumPositional = 0;
    // Negative means unbounded, for a command that takes a list of paths
    // (`loom style`). Everything else caps it.
    int maximumPositional = 0;
    // Whether a bare "--" is allowed, after which everything is forwarded to
    // the launched application rather than parsed.
    bool acceptsPassthrough = false;
};

class ParsedCommand {
public:
    bool isSet(const QString &name) const;
    QString value(const QString &name, const QString &fallback = {}) const;
    const QStringList &positional() const;
    const QStringList &passthrough() const;

    void setFlag(const QString &name);
    void setValue(const QString &name, const QString &value);
    void setPositional(QStringList positional);
    void setPassthrough(QStringList passthrough);

private:
    QHash<QString, QString> m_values;
    QStringList m_flags;
    QStringList m_positional;
    QStringList m_passthrough;
};

enum class ParseOutcome {
    Ready,       // `parsed` is populated
    HelpPrinted, // --help was asked for; the caller should exit Success
    Rejected,    // already reported; the caller should exit UsageError
};

ParseOutcome parseCommand(
    const CommandSpec &spec, const QStringList &arguments, ParsedCommand &parsed);

void printCommandHelp(const CommandSpec &spec);

// The single error channel. Previously there were three: printError,
// BuildRunner's direct stderr write, and the "[loom]" dev-log writes, which
// additionally did not affect the exit code.
int reportError(const QString &message);
int reportUsageError(const CommandSpec &spec, const QString &message);

// Output level, set once from the parsed command line. Errors are always
// printed: --quiet suppresses progress, not failures.
enum class Verbosity { Quiet, Normal, Verbose };
void setVerbosity(Verbosity verbosity);
Verbosity verbosity();
bool isQuiet();
// Progress and status, suppressed by --quiet. Not for errors.
void reportProgress(const QString &message);

} // namespace cli
