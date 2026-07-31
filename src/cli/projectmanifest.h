#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct ApplicationDefinition {
    QString name;
    QString target;
    QString id;
    QString uri;
    QString entry;
    QStringList qmlRoots;
    QStringList assetRoots;
    QStringList platforms;
};

class ProjectManifest {
public:
    static ProjectManifest
    createDefault(const QString &projectName, const QString &organization);
    static ProjectManifest
    create(const QString &projectName, const ApplicationDefinition &application);
    static bool
    load(const QString &filePath, ProjectManifest &manifest, QString *error = nullptr);

    bool save(const QString &filePath, QString *error = nullptr) const;
    bool validate(QString *error = nullptr) const;

    QJsonObject toJson() const;
    QString projectName() const;
    QString qtVersion() const;
    QList<ApplicationDefinition> applications() const;
    QStringList applicationTargets() const;
    // Only valid where the manifest is known to hold exactly one application,
    // such as one this process just created. Commands must use
    // selectApplication() instead: after load this is the *alphabetically*
    // first target, not a meaningful default.
    ApplicationDefinition primaryApplication() const;
    QString defaultApplication() const;
    void setDefaultApplication(const QString &target);

    // Resolves which application a command should act on. `requested` is the
    // --app value, empty when the user did not pass one. Previously every
    // command used constFirst(), which after load is the *alphabetically* first
    // target, so adding an "Admin" app to a "Viewer" project silently changed
    // what `respin dev` ran.
    bool selectApplication(
        const QString &requested, ApplicationDefinition &application,
        QString *error = nullptr) const;

private:
    QJsonObject projectObject() const;

    QString m_projectName;
    QString m_qtVersion = QStringLiteral("6.11");
    QString m_defaultApplication;
    QList<ApplicationDefinition> m_applications;
};

QString findManifest(const QString &startingDirectory);

// Turns a display name into a C++/CMake identifier. Lives here, and is used by
// the scaffolder too, because the two had character-identical private copies:
// the moment they diverged, `respin dev` would look for a target the generated
// CMakeLists.txt never defined.
QString identifierFromName(QString name);
