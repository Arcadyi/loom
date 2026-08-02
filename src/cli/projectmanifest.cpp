#include "projectmanifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>

QString identifierFromName(QString name)
{
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")));
    if (name.isEmpty())
        name = QStringLiteral("App");
    if (name.front().isDigit())
        name.prepend(QStringLiteral("App"));
    name[0] = name[0].toUpper();
    return name;
}

namespace {

QStringList readStringArray(const QJsonValue &value)
{
    QStringList result;
    for (const auto &item : value.toArray()) {
        if (item.isString())
            result.append(item.toString());
    }
    return result;
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray array;
    for (const auto &value : values)
        array.append(value);
    return array;
}

bool shapeError(QString *error, const QString &path, const QString &message)
{
    if (error)
        *error = QStringLiteral("Invalid loom.json: %1 %2").arg(path, message);
    return false;
}

bool hasOnlyKeys(
    const QJsonObject &object, const QStringList &known, const QString &path,
    QString *error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return shapeError(
                error, path + QLatin1Char('.') + it.key(),
                QStringLiteral("is not supported by schema v2"));
    }
    return true;
}

bool isStringArray(const QJsonValue &value, bool allowEmpty)
{
    if (!value.isArray() || (!allowEmpty && value.toArray().isEmpty()))
        return false;
    const QJsonArray values = value.toArray();
    return std::all_of(values.cbegin(), values.cend(), [](const QJsonValue &entry) {
        return entry.isString() && !entry.toString().isEmpty();
    });
}

bool validatePlatformOptions(
    const QString &platform, const QJsonValue &value, const QString &path, QString *error)
{
    if (!value.isObject())
        return shapeError(error, path, QStringLiteral("must be an object"));
    const QJsonObject options = value.toObject();
    QStringList keys;
    if (platform == QLatin1String("desktop")) {
        keys = {};
    } else if (platform == QLatin1String("android")) {
        keys = {QStringLiteral("qtPath"), QStringLiteral("hostQtPath"),
                QStringLiteral("abi"),    QStringLiteral("abis"),
                QStringLiteral("api"),    QStringLiteral("device")};
    } else if (platform == QLatin1String("ios")) {
        keys = {QStringLiteral("qtPath"), QStringLiteral("hostQtPath"),
                QStringLiteral("sdk"),    QStringLiteral("destination"),
                QStringLiteral("team"),   QStringLiteral("device"),
                QStringLiteral("host")};
    } else if (platform == QLatin1String("embedded")) {
        keys = {QStringLiteral("profile")};
    } else {
        return shapeError(error, path, QStringLiteral("names an unknown platform"));
    }
    if (!hasOnlyKeys(options, keys, path, error))
        return false;

    static const QStringList androidAbis{
        QStringLiteral("arm64-v8a"), QStringLiteral("armeabi-v7a"),
        QStringLiteral("x86_64")};
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (it.key() == QLatin1String("api")) {
            const double api = it->toDouble(-1);
            if (!it->isDouble() || std::floor(api) != api || api < 23)
                return shapeError(
                    error, path + QStringLiteral(".api"),
                    QStringLiteral("must be an integer of at least 23"));
        } else if (it.key() == QLatin1String("abis")) {
            const QJsonArray abis = it->toArray();
            if (!isStringArray(*it, true)
                || !std::all_of(abis.cbegin(), abis.cend(), [](const auto &abi) {
                       return androidAbis.contains(abi.toString());
                   }))
                return shapeError(
                    error, path + QStringLiteral(".abis"),
                    QStringLiteral("contains an unsupported Android ABI"));
        } else if (it.key() == QLatin1String("abi")) {
            if (!it->isString() || !androidAbis.contains(it->toString()))
                return shapeError(
                    error, path + QStringLiteral(".abi"),
                    QStringLiteral("must name a supported Android ABI"));
        } else if (it.key() == QLatin1String("sdk")) {
            if (!it->isString()
                || (it->toString() != QLatin1String("iphonesimulator")
                    && it->toString() != QLatin1String("iphoneos")))
                return shapeError(
                    error, path + QStringLiteral(".sdk"),
                    QStringLiteral("must be iphonesimulator or iphoneos"));
        } else if (it.key() == QLatin1String("destination")) {
            if (!it->isString()
                || (it->toString() != QLatin1String("simulator")
                    && it->toString() != QLatin1String("device")))
                return shapeError(
                    error, path + QStringLiteral(".destination"),
                    QStringLiteral("must be simulator or device"));
        } else if (!it->isString()) {
            return shapeError(
                error, path + QLatin1Char('.') + it.key(),
                QStringLiteral("must be a string"));
        } else if (
            (it.key() == QLatin1String("qtPath")
             || it.key() == QLatin1String("hostQtPath")
             || it.key() == QLatin1String("profile"))
            && it->toString().isEmpty()) {
            return shapeError(
                error, path + QLatin1Char('.') + it.key(),
                QStringLiteral("must not be empty"));
        }
    }
    return true;
}

bool validateManifestShape(const QJsonObject &root, QString *error)
{
    if (!hasOnlyKeys(
            root,
            {QStringLiteral("$schema"), QStringLiteral("schemaVersion"),
             QStringLiteral("project"), QStringLiteral("design"), QStringLiteral("qt"),
             QStringLiteral("embeddedProfiles"), QStringLiteral("applications")},
            QStringLiteral("root"), error))
        return false;
    const QJsonValue schemaVersion = root.value(QLatin1String("schemaVersion"));
    if (!schemaVersion.isDouble() || schemaVersion.toDouble() != 2.0)
        return shapeError(
            error, QStringLiteral("schemaVersion"), QStringLiteral("must be 2"));
    if ((root.contains(QLatin1String("$schema"))
         && !root.value(QLatin1String("$schema")).isString())
        || (root.contains(QLatin1String("design"))
            && (!root.value(QLatin1String("design")).isString()
                || root.value(QLatin1String("design")).toString().isEmpty())))
        return shapeError(
            error, QStringLiteral("root"),
            QStringLiteral("has a non-string $schema or design field"));
    if (!root.value(QLatin1String("project")).isObject()
        || !root.value(QLatin1String("qt")).isObject()
        || !root.value(QLatin1String("applications")).isObject())
        return shapeError(
            error, QStringLiteral("root"),
            QStringLiteral("requires project, qt, and applications objects"));

    const QJsonObject project = root.value(QLatin1String("project")).toObject();
    if (!hasOnlyKeys(
            project, {QStringLiteral("name"), QStringLiteral("defaultApplication")},
            QStringLiteral("project"), error))
        return false;
    if (!project.value(QLatin1String("name")).isString())
        return shapeError(
            error, QStringLiteral("project.name"), QStringLiteral("must be a string"));
    if (project.contains(QLatin1String("defaultApplication"))
        && (!project.value(QLatin1String("defaultApplication")).isString()
            || project.value(QLatin1String("defaultApplication")).toString().isEmpty()))
        return shapeError(
            error, QStringLiteral("project.defaultApplication"),
            QStringLiteral("must be a string"));

    const QJsonObject qt = root.value(QLatin1String("qt")).toObject();
    if (!hasOnlyKeys(qt, {QStringLiteral("version")}, QStringLiteral("qt"), error))
        return false;
    if (!qt.value(QLatin1String("version")).isString())
        return shapeError(
            error, QStringLiteral("qt.version"), QStringLiteral("must be a string"));

    const QJsonObject applications = root.value(QLatin1String("applications")).toObject();
    if (applications.isEmpty())
        return shapeError(
            error, QStringLiteral("applications"),
            QStringLiteral("must contain at least one application"));
    static const QStringList applicationKeys{
        QStringLiteral("name"),       QStringLiteral("target"),
        QStringLiteral("id"),         QStringLiteral("uri"),
        QStringLiteral("entry"),      QStringLiteral("qmlRoots"),
        QStringLiteral("assetRoots"), QStringLiteral("platforms")};
    for (auto it = applications.constBegin(); it != applications.constEnd(); ++it) {
        const QString path = QStringLiteral("applications.") + it.key();
        if (!it->isObject())
            return shapeError(error, path, QStringLiteral("must be an object"));
        const QJsonObject application = it->toObject();
        if (!hasOnlyKeys(application, applicationKeys, path, error))
            return false;
        for (const auto &field : {"name", "target", "id", "uri", "entry"}) {
            const QJsonValue value = application.value(QLatin1String(field));
            if (!value.isString() || value.toString().isEmpty())
                return shapeError(
                    error, path + QLatin1Char('.') + QLatin1String(field),
                    QStringLiteral("must be a non-empty string"));
        }
        if (application.value(QLatin1String("target")).toString() != it.key())
            return shapeError(
                error, path + QStringLiteral(".target"),
                QStringLiteral("must match its applications map key"));
        if (!isStringArray(application.value(QLatin1String("qmlRoots")), false)
            || !isStringArray(application.value(QLatin1String("assetRoots")), true))
            return shapeError(
                error, path,
                QStringLiteral("requires string-array qmlRoots and assetRoots"));
        if (!application.value(QLatin1String("platforms")).isObject())
            return shapeError(
                error, path + QStringLiteral(".platforms"),
                QStringLiteral("must be an object in schema v2"));
        const QJsonObject platforms =
            application.value(QLatin1String("platforms")).toObject();
        if (platforms.isEmpty())
            return shapeError(
                error, path + QStringLiteral(".platforms"),
                QStringLiteral("must enable at least one platform"));
        for (auto platform = platforms.constBegin(); platform != platforms.constEnd();
             ++platform) {
            if (!validatePlatformOptions(
                    platform.key(), platform.value(),
                    path + QStringLiteral(".platforms.") + platform.key(), error))
                return false;
        }
    }

    if (root.contains(QLatin1String("embeddedProfiles"))) {
        if (!root.value(QLatin1String("embeddedProfiles")).isObject())
            return shapeError(
                error, QStringLiteral("embeddedProfiles"),
                QStringLiteral("must be an object"));
        const QJsonObject profiles =
            root.value(QLatin1String("embeddedProfiles")).toObject();
        static const QStringList profileKeys{
            QStringLiteral("toolchainFile"), QStringLiteral("hostQtPath"),
            QStringLiteral("sysroot"),       QStringLiteral("host"),
            QStringLiteral("port"),          QStringLiteral("user"),
            QStringLiteral("remoteDir"),     QStringLiteral("environment"),
            QStringLiteral("launchCommand")};
        for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
            const QString path = QStringLiteral("embeddedProfiles.") + it.key();
            if (it.key().isEmpty() || !it->isObject())
                return shapeError(error, path, QStringLiteral("is invalid"));
            const QJsonObject profile = it->toObject();
            if (!hasOnlyKeys(profile, profileKeys, path, error))
                return false;
            for (const auto &field : {"toolchainFile", "sysroot", "host", "remoteDir"}) {
                if (!profile.value(QLatin1String(field)).isString()
                    || profile.value(QLatin1String(field)).toString().isEmpty())
                    return shapeError(
                        error, path + QLatin1Char('.') + QLatin1String(field),
                        QStringLiteral("must be a non-empty string"));
            }
            if (profile.contains(QLatin1String("hostQtPath"))
                && (!profile.value(QLatin1String("hostQtPath")).isString()
                    || profile.value(QLatin1String("hostQtPath")).toString().isEmpty()))
                return shapeError(
                    error, path + QStringLiteral(".hostQtPath"),
                    QStringLiteral("must be a non-empty string"));
            for (const auto &field : {"user", "launchCommand"}) {
                if (profile.contains(QLatin1String(field))
                    && !profile.value(QLatin1String(field)).isString())
                    return shapeError(
                        error, path + QLatin1Char('.') + QLatin1String(field),
                        QStringLiteral("must be a string"));
            }
            if (profile.contains(QLatin1String("port"))) {
                const QJsonValue port = profile.value(QLatin1String("port"));
                const double number = port.toDouble(-1);
                if (!port.isDouble() || std::floor(number) != number || number < 1
                    || number > 65535)
                    return shapeError(
                        error, path + QStringLiteral(".port"),
                        QStringLiteral("must be an integer from 1 through 65535"));
            }
            if (profile.contains(QLatin1String("environment"))) {
                const QJsonValue environment =
                    profile.value(QLatin1String("environment"));
                if (!environment.isObject())
                    return shapeError(
                        error, path + QStringLiteral(".environment"),
                        QStringLiteral("must be an object of strings"));
                const QJsonObject variables = environment.toObject();
                static const QRegularExpression variableName(
                    QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
                for (auto variable = variables.constBegin();
                     variable != variables.constEnd(); ++variable) {
                    if (!variableName.match(variable.key()).hasMatch())
                        return shapeError(
                            error,
                            path + QStringLiteral(".environment.") + variable.key(),
                            QStringLiteral("is not a valid environment variable name"));
                    if (!variable->isString())
                        return shapeError(
                            error,
                            path + QStringLiteral(".environment.") + variable.key(),
                            QStringLiteral("must be a string"));
                }
            }
        }
    }
    return true;
}

} // namespace

const QStringList &ProjectManifest::supportedPlatforms()
{
    static const QStringList platforms{
        QStringLiteral("desktop"),
        QStringLiteral("android"),
        QStringLiteral("ios"),
        QStringLiteral("embedded"),
    };
    return platforms;
}

ProjectManifest ProjectManifest::createDefault(
    const QString &projectName, const QString &organization, const QStringList &platforms)
{
    ProjectManifest manifest;
    const auto identifier = identifierFromName(projectName);
    auto organizationId = organization.isEmpty()
        ? QStringLiteral("dev.example")
        : QString(organization)
              .toLower()
              .replace(QRegularExpression(QStringLiteral("[^a-z0-9.]")), QString());
    if (organizationId.isEmpty() || organizationId.startsWith(QLatin1Char('.'))
        || organizationId.endsWith(QLatin1Char('.'))
        || organizationId.contains(QStringLiteral(".."))) {
        organizationId = QStringLiteral("dev.example");
    }

    manifest.m_projectName = projectName;
    // Matches templates/app/design/tokens.json, which the scaffolder writes and
    // the generated CMakeLists.txt passes to loom_add_application as DESIGN.
    // The three have to agree or `loom dev` watches a file that is not there.
    manifest.m_design = QStringLiteral("design/tokens.json");
    manifest.m_applications.append(
        ApplicationDefinition{
            .name = projectName,
            .target = identifier,
            .id = organizationId + QLatin1Char('.') + identifier.toLower(),
            .uri = organizationId + QLatin1Char('.') + identifier,
            .entry = QStringLiteral("Main"),
            .qmlRoots = {QStringLiteral("qml")},
            .assetRoots = {QStringLiteral("assets")},
            .platforms = platforms.isEmpty() ? supportedPlatforms() : platforms,
            .platformOptions = {},
        });
    return manifest;
}

ProjectManifest ProjectManifest::create(
    const QString &projectName, const ApplicationDefinition &application)
{
    ProjectManifest manifest;
    manifest.m_projectName = projectName;
    manifest.m_applications.append(application);
    return manifest;
}

bool ProjectManifest::load(
    const QString &filePath, ProjectManifest &manifest, QString *error)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error =
                QStringLiteral("Cannot open %1: %2").arg(filePath, input.errorString());
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid JSON in %1 at offset %2: %3")
                         .arg(filePath)
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        }
        return false;
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 2) {
        if (error)
            *error = QStringLiteral(
                "Unsupported loom.json schemaVersion; run 'loom migrate --to 2 --apply'");
        return false;
    }
    if (!validateManifestShape(root, error))
        return false;

    ProjectManifest decoded;
    decoded.m_projectName = root.value(QStringLiteral("project"))
                                .toObject()
                                .value(QStringLiteral("name"))
                                .toString();
    decoded.m_qtVersion = root.value(QStringLiteral("qt"))
                              .toObject()
                              .value(QStringLiteral("version"))
                              .toString();
    decoded.m_defaultApplication = root.value(QStringLiteral("project"))
                                       .toObject()
                                       .value(QStringLiteral("defaultApplication"))
                                       .toString();
    decoded.m_design = root.value(QStringLiteral("design")).toString();
    decoded.m_embeddedProfiles =
        root.value(QStringLiteral("embeddedProfiles")).toObject();

    const auto applications = root.value(QStringLiteral("applications")).toObject();
    for (auto iterator = applications.begin(); iterator != applications.end();
         ++iterator) {
        const auto object = iterator.value().toObject();
        decoded.m_applications.append(
            ApplicationDefinition{
                .name = object.value(QStringLiteral("name")).toString(iterator.key()),
                .target = object.value(QStringLiteral("target")).toString(iterator.key()),
                .id = object.value(QStringLiteral("id")).toString(),
                .uri = object.value(QStringLiteral("uri")).toString(),
                .entry = object.value(QStringLiteral("entry")).toString(),
                .qmlRoots = readStringArray(object.value(QStringLiteral("qmlRoots"))),
                .assetRoots = readStringArray(object.value(QStringLiteral("assetRoots"))),
                .platforms = readStringArray(object.value(QStringLiteral("platforms"))),
                .platformOptions = object.value(QStringLiteral("platforms")).toObject(),
            });
        if (!decoded.m_applications.last().platformOptions.isEmpty())
            decoded.m_applications.last().platforms =
                decoded.m_applications.last().platformOptions.keys();
    }

    if (!decoded.validate(error))
        return false;
    manifest = std::move(decoded);
    return true;
}

bool ProjectManifest::save(const QString &filePath, QString *error) const
{
    if (!validate(error))
        return false;
    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error =
                QStringLiteral("Cannot write %1: %2").arg(filePath, output.errorString());
        return false;
    }
    const auto bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        if (error)
            *error = QStringLiteral("Cannot commit %1: %2")
                         .arg(filePath, output.errorString());
        return false;
    }
    return true;
}

bool ProjectManifest::validate(QString *error) const
{
    if (m_projectName.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("project.name must not be empty");
        return false;
    }
    // A minimum, not an exact match: CMake asks for Qt 6.11 *or newer* and the
    // documentation promises the same. Requiring equality here would have made
    // every existing loom.json invalid the day 6.12 shipped, with no upgrade
    // path -- the project's own tool rejecting a toolchain its build accepts.
    const auto declared = QVersionNumber::fromString(m_qtVersion);
    static const QVersionNumber minimumQt(6, 11);
    if (declared.isNull() || declared < minimumQt) {
        if (error) {
            *error = QStringLiteral("loom requires qt.version 6.11 or newer, not \"%1\"")
                         .arg(m_qtVersion);
        }
        return false;
    }
    if (m_applications.isEmpty()) {
        if (error)
            *error = QStringLiteral("At least one application is required");
        return false;
    }

    // Enforcement catching up to the schema that ships alongside it. An
    // installed schema nothing validates against is a promise, not a feature.
    const QStringList &knownPlatforms = supportedPlatforms();
    const QRegularExpression uriPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+$"));
    for (const auto &application : m_applications) {
        if (application.name.trimmed().isEmpty() || application.target.isEmpty()
            || application.entry.isEmpty() || application.qmlRoots.isEmpty()
            || application.platforms.isEmpty()
            || std::any_of(
                application.qmlRoots.cbegin(), application.qmlRoots.cend(),
                [](const QString &path) { return path.isEmpty(); })
            || std::any_of(
                application.assetRoots.cbegin(), application.assetRoots.cend(),
                [](const QString &path) { return path.isEmpty(); })
            || !uriPattern.match(application.uri).hasMatch()) {
            if (error) {
                *error = QStringLiteral(
                             "Application '%1' requires name, target, entry, non-empty "
                             "qmlRoots/platforms, and a valid dotted URI")
                             .arg(application.name);
            }
            return false;
        }
        // The schema has always listed id as required; the loader never checked,
        // so a hand-written manifest missing it loaded fine and then produced an
        // application with an empty bundle identifier.
        if (application.id.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Application '%1' requires an id")
                             .arg(application.target);
            }
            return false;
        }
        for (const auto &platform : application.platforms) {
            if (knownPlatforms.contains(platform))
                continue;
            if (error) {
                *error = QStringLiteral(
                             "Application '%1' lists unknown platform '%2'; "
                             "expected one of %3")
                             .arg(
                                 application.target, platform,
                                 knownPlatforms.join(QStringLiteral(", ")));
            }
            return false;
        }
    }

    if (!m_defaultApplication.isEmpty()
        && !applicationTargets().contains(m_defaultApplication)) {
        if (error) {
            *error = QStringLiteral("project.defaultApplication '%1' is not one of %2")
                         .arg(
                             m_defaultApplication,
                             applicationTargets().join(QStringLiteral(", ")));
        }
        return false;
    }
    return true;
}

QJsonObject ProjectManifest::projectObject() const
{
    QJsonObject project{{QStringLiteral("name"), m_projectName}};
    // Omitted rather than written empty: the schema forbids unknown keys, and an
    // empty string is not a valid target either.
    if (!m_defaultApplication.isEmpty())
        project.insert(QStringLiteral("defaultApplication"), m_defaultApplication);
    return project;
}

QJsonObject ProjectManifest::toJson() const
{
    QJsonObject applications;
    for (const auto &application : m_applications) {
        QJsonObject platforms = application.platformOptions;
        if (platforms.isEmpty()) {
            for (const QString &name : application.platforms)
                platforms.insert(name, QJsonObject{});
        }
        applications.insert(
            application.target,
            QJsonObject{
                {QStringLiteral("name"), application.name},
                {QStringLiteral("target"), application.target},
                {QStringLiteral("id"), application.id},
                {QStringLiteral("uri"), application.uri},
                {QStringLiteral("entry"), application.entry},
                {QStringLiteral("qmlRoots"), stringArray(application.qmlRoots)},
                {QStringLiteral("assetRoots"), stringArray(application.assetRoots)},
                {QStringLiteral("platforms"), platforms},
            });
    }
    QJsonObject root{
        {QStringLiteral("$schema"),
         QStringLiteral(
             "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/"
             "project-v2.schema.json")},
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("project"), projectObject()},
        {QStringLiteral("qt"),
         QJsonObject{
             {QStringLiteral("version"), m_qtVersion},
         }},
        {QStringLiteral("applications"), applications},
    };
    // Omitted rather than written empty, for the same reason as
    // project.defaultApplication: the schema forbids unknown keys and an empty
    // string is not a usable path.
    if (!m_design.isEmpty())
        root.insert(QStringLiteral("design"), m_design);
    if (!m_embeddedProfiles.isEmpty())
        root.insert(QStringLiteral("embeddedProfiles"), m_embeddedProfiles);
    return root;
}

QString ProjectManifest::designPath() const
{
    return m_design;
}

void ProjectManifest::setDesignPath(const QString &path)
{
    m_design = path;
}

QString ProjectManifest::resolvedDesignPath(const QString &manifestPath) const
{
    if (m_design.isEmpty())
        return {};
    // Anchored at the manifest's directory, never the working directory: every
    // command that reads this runs from wherever the user happened to be.
    // An absolute path in the manifest is honoured as-is.
    return QFileInfo(QFileInfo(manifestPath).absolutePath(), m_design).absoluteFilePath();
}

QString ProjectManifest::projectName() const
{
    return m_projectName;
}

QString ProjectManifest::qtVersion() const
{
    return m_qtVersion;
}

QJsonObject ProjectManifest::embeddedProfiles() const
{
    return m_embeddedProfiles;
}

QList<ApplicationDefinition> ProjectManifest::applications() const
{
    return m_applications;
}

QStringList ProjectManifest::applicationTargets() const
{
    QStringList targets;
    targets.reserve(m_applications.size());
    for (const auto &application : m_applications)
        targets.append(application.target);
    return targets;
}

QString ProjectManifest::defaultApplication() const
{
    return m_defaultApplication;
}

void ProjectManifest::setDefaultApplication(const QString &target)
{
    m_defaultApplication = target;
}

ApplicationDefinition ProjectManifest::primaryApplication() const
{
    return m_applications.constFirst();
}

bool ProjectManifest::selectApplication(
    const QString &requested, ApplicationDefinition &application, QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_applications.isEmpty())
        return fail(QStringLiteral("the manifest defines no applications"));

    const auto wanted = requested.isEmpty() ? m_defaultApplication : requested;
    if (wanted.isEmpty()) {
        if (m_applications.size() > 1) {
            return fail(
                QStringLiteral(
                    "this project defines %1 applications (%2); choose one with "
                    "--app <target>, or set project.defaultApplication in loom.json")
                    .arg(m_applications.size())
                    .arg(applicationTargets().join(QStringLiteral(", "))));
        }
        application = m_applications.constFirst();
        return true;
    }

    for (const auto &candidate : m_applications) {
        if (candidate.target == wanted) {
            application = candidate;
            return true;
        }
    }
    return fail(QStringLiteral("unknown application '%1'; this project defines %2")
                    .arg(wanted, applicationTargets().join(QStringLiteral(", "))));
}

QString findManifest(const QString &startingDirectory)
{
    // Bounded on purpose. An unbounded walk meant a stray loom.json in $HOME
    // became "your project" from anywhere on the filesystem, so a mistyped cd
    // could build something entirely unrelated.
    static const QStringList repositoryMarkers{
        QStringLiteral(".git"),
        QStringLiteral(".hg"),
        QStringLiteral(".jj"),
        QStringLiteral(".svn"),
    };
    const auto home = QDir::cleanPath(QDir::homePath());

    QDir directory(startingDirectory);
    while (true) {
        const auto candidate = directory.filePath(QStringLiteral("loom.json"));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();

        const auto current = QDir::cleanPath(directory.absolutePath());
        // A repository root is the outermost thing that can sensibly be "the
        // project", and $HOME is never one.
        if (current == home)
            return {};
        for (const auto &marker : repositoryMarkers) {
            if (QFileInfo::exists(directory.filePath(marker)))
                return {};
        }
        if (!directory.cdUp())
            return {};
    }
}
