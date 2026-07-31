#include "commandline.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

namespace cli {

bool ParsedCommand::isSet(const QString &name) const
{
    return m_flags.contains(name) || m_values.contains(name);
}

QString ParsedCommand::value(const QString &name, const QString &fallback) const
{
    return m_values.value(name, fallback);
}

const QStringList &ParsedCommand::positional() const
{
    return m_positional;
}

const QStringList &ParsedCommand::passthrough() const
{
    return m_passthrough;
}

void ParsedCommand::setFlag(const QString &name)
{
    m_flags.append(name);
}

void ParsedCommand::setValue(const QString &name, const QString &value)
{
    m_values.insert(name, value);
}

void ParsedCommand::setPositional(QStringList positional)
{
    m_positional = std::move(positional);
}

void ParsedCommand::setPassthrough(QStringList passthrough)
{
    m_passthrough = std::move(passthrough);
}

namespace {
Verbosity currentVerbosity = Verbosity::Normal;
}

void setVerbosity(const Verbosity level)
{
    currentVerbosity = level;
}

Verbosity verbosity()
{
    return currentVerbosity;
}

bool isQuiet()
{
    return currentVerbosity == Verbosity::Quiet;
}

void reportProgress(const QString &message)
{
    if (isQuiet())
        return;
    QTextStream(stdout) << "respin: " << message << Qt::endl;
}

int reportError(const QString &message)
{
    QTextStream(stderr) << "respin: " << message << Qt::endl;
    return Failure;
}

int reportUsageError(const CommandSpec &spec, const QString &message)
{
    QTextStream(stderr) << "respin " << spec.name << ": " << message << '\n'
                        << "usage: " << spec.usage << '\n'
                        << "try 'respin " << spec.name << " --help'" << Qt::endl;
    return UsageError;
}

void printCommandHelp(const CommandSpec &spec)
{
    QTextStream output(stdout);
    output << spec.summary << "\n\nusage: " << spec.usage << "\n";
    if (spec.options.isEmpty()) {
        output << Qt::endl;
        return;
    }

    qsizetype width = 0;
    for (const auto &option : spec.options) {
        const auto rendered = option.valueName.isEmpty()
            ? option.name.size() + 2
            : option.name.size() + option.valueName.size() + 5;
        width = std::max(width, rendered);
    }

    output << "\noptions:\n";
    for (const auto &option : spec.options) {
        auto rendered = QStringLiteral("--") + option.name;
        if (!option.valueName.isEmpty())
            rendered += QStringLiteral(" <") + option.valueName + QLatin1Char('>');
        output << "  " << rendered.leftJustified(width + 2) << option.description << '\n';
    }
    output << "  " << QStringLiteral("--help").leftJustified(width + 2)
           << "Show this help." << Qt::endl;
}

ParseOutcome
parseCommand(const CommandSpec &spec, const QStringList &arguments, ParsedCommand &parsed)
{
    const auto reject = [&spec](const QString &message) {
        reportUsageError(spec, message);
        return ParseOutcome::Rejected;
    };

    // Split at the first bare "--" here rather than letting QCommandLineParser
    // fold everything after it into the positional list: the dev command needs
    // the boundary itself, not just the tokens.
    auto head = arguments;
    QStringList passthrough;
    const auto separator = arguments.indexOf(QStringLiteral("--"));
    if (separator >= 0) {
        head = arguments.first(separator);
        passthrough = arguments.sliced(separator + 1);
        if (!spec.acceptsPassthrough) {
            return reject(
                QStringLiteral("this command does not forward arguments after '--'"));
        }
    }

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    QCommandLineOption helpOption(
        QStringList{QStringLiteral("h"), QStringLiteral("help")},
        QStringLiteral("Show this help."));
    // Not parser.addHelpOption(): that installs Qt's own handler, and asking for
    // help through it calls showHelp(), which exits the process.
    parser.addOption(helpOption);

    QList<QCommandLineOption> options;
    options.reserve(spec.options.size());
    for (const auto &option : spec.options) {
        options.append(
            option.valueName.isEmpty()
                ? QCommandLineOption(option.name, option.description)
                : QCommandLineOption(option.name, option.description, option.valueName));
        parser.addOption(options.last());
    }

    // parse(), never process(): process() writes its own message and calls
    // exit(), which makes every failure unreportable and untestable.
    QStringList argv{QStringLiteral("respin ") + spec.name};
    argv.append(head);
    if (!parser.parse(argv))
        return reject(parser.errorText());

    if (!parser.unknownOptionNames().isEmpty()) {
        return reject(QStringLiteral("unknown option '%1'")
                          .arg(parser.unknownOptionNames().constFirst()));
    }

    if (parser.isSet(helpOption)) {
        printCommandHelp(spec);
        return ParseOutcome::HelpPrinted;
    }

    for (qsizetype index = 0; index < spec.options.size(); ++index) {
        const auto &option = spec.options.at(index);
        if (!parser.isSet(options.at(index)))
            continue;
        if (option.valueName.isEmpty()) {
            parsed.setFlag(option.name);
            continue;
        }
        const auto value = parser.value(options.at(index));
        // QCommandLineParser happily swallows the next token as a value, so
        // "--prefix --config Release" silently yields prefix="--config". No
        // real value for any of these options starts with a dash.
        if (value.startsWith(QLatin1Char('-'))) {
            return reject(QStringLiteral("option '--%1' expects a value but got '%2'")
                              .arg(option.name, value));
        }
        parsed.setValue(option.name, value);
    }

    const auto positional = parser.positionalArguments();
    if (positional.size() < spec.minimumPositional) {
        return reject(QStringLiteral("missing required argument"));
    }
    if (positional.size() > spec.maximumPositional) {
        return reject(QStringLiteral("unexpected argument '%1'")
                          .arg(positional.at(spec.maximumPositional)));
    }

    parsed.setPositional(positional);
    parsed.setPassthrough(std::move(passthrough));
    return ParseOutcome::Ready;
}

} // namespace cli
