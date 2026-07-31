#include "projectmanifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

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

} // namespace

ProjectManifest
ProjectManifest::createDefault(const QString &projectName, const QString &organization)
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
    manifest.m_applications.append(
        ApplicationDefinition{
            .name = projectName,
            .target = identifier,
            .id = organizationId + QLatin1Char('.') + identifier.toLower(),
            .uri = organizationId + QLatin1Char('.') + identifier,
            .entry = QStringLiteral("Main"),
            .qmlRoots = {QStringLiteral("qml")},
            .assetRoots = {QStringLiteral("assets")},
            .platforms =
                {
                    QStringLiteral("desktop"),
                    QStringLiteral("android"),
                    QStringLiteral("ios"),
                    QStringLiteral("embedded"),
                },
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
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error)
            *error = QStringLiteral("Unsupported loom.json schemaVersion");
        return false;
    }

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
            });
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
    if (m_qtVersion != QStringLiteral("6.11")) {
        if (error)
            *error = QStringLiteral("loom v1 requires qt.version \"6.11\"");
        return false;
    }
    if (m_applications.isEmpty()) {
        if (error)
            *error = QStringLiteral("At least one application is required");
        return false;
    }

    // Enforcement catching up to the schema that ships alongside it. An
    // installed schema nothing validates against is a promise, not a feature.
    static const QStringList knownPlatforms{
        QStringLiteral("desktop"),
        QStringLiteral("android"),
        QStringLiteral("ios"),
        QStringLiteral("embedded"),
    };
    const QRegularExpression uriPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+$"));
    for (const auto &application : m_applications) {
        if (application.target.isEmpty() || application.entry.isEmpty()
            || application.qmlRoots.isEmpty()
            || !uriPattern.match(application.uri).hasMatch()) {
            if (error) {
                *error = QStringLiteral(
                             "Application '%1' requires target, entry, qmlRoots, and a "
                             "valid dotted URI")
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
                {QStringLiteral("platforms"), stringArray(application.platforms)},
            });
    }
    return QJsonObject{
        {QStringLiteral("$schema"),
         QStringLiteral("https://raw.githubusercontent.com/Arcadyi/loom/main/schemas/project-v1.schema.json")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("project"), projectObject()},
        {QStringLiteral("qt"),
         QJsonObject{
             {QStringLiteral("version"), m_qtVersion},
         }},
        {QStringLiteral("applications"), applications},
    };
}

QString ProjectManifest::projectName() const
{
    return m_projectName;
}

QString ProjectManifest::qtVersion() const
{
    return m_qtVersion;
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
