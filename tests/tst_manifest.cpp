#include "projectmanifest.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ManifestTests final : public QObject {
    Q_OBJECT

private slots:
    // qt.version is a minimum, matching find_package(Qt6 6.11) and the docs.
    // It used to be an exact-match check, which would have rejected every
    // existing loom.json the day 6.12 shipped -- loom refusing a toolchain its
    // own build system accepts, with no upgrade path.
    void qtVersionIsAMinimumNotAnExactMatch()
    {
        const auto manifestWith = [](const char *version) {
            auto manifest = ProjectManifest::createDefault(
                QStringLiteral("Sample"), QStringLiteral("com.acme"));
            QJsonObject json = manifest.toJson();
            QJsonObject qt = json.value(QStringLiteral("qt")).toObject();
            qt.insert(QStringLiteral("version"), QLatin1String(version));
            json.insert(QStringLiteral("qt"), qt);
            return json;
        };

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        int index = 0;
        const auto validates = [&](const char *version, QString *error) {
            const auto path =
                temporary.filePath(QStringLiteral("loom%1.json").arg(index++));
            QFile output(path);
            if (!output.open(QIODevice::WriteOnly))
                return false;
            output.write(QJsonDocument(manifestWith(version)).toJson());
            output.close();
            ProjectManifest loaded;
            return ProjectManifest::load(path, loaded, error);
        };

        QString error;
        QVERIFY2(validates("6.11", &error), qPrintable(error));
        QVERIFY2(validates("6.12", &error), qPrintable(error));
        QVERIFY2(validates("7.0", &error), qPrintable(error));

        QVERIFY(!validates("6.10", &error));
        QVERIFY(error.contains(QStringLiteral("6.11 or newer")));
        QVERIFY(!validates("banana", &error));
    }

    void defaultManifestRoundTrips()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath(QStringLiteral("loom.json"));
        const auto source = ProjectManifest::createDefault(
            QStringLiteral("Sample App"), QStringLiteral("com.acme"));
        QString error;
        QVERIFY2(source.save(path, &error), qPrintable(error));

        ProjectManifest decoded;
        QVERIFY2(ProjectManifest::load(path, decoded, &error), qPrintable(error));
        QCOMPARE(decoded.projectName(), QStringLiteral("Sample App"));
        QCOMPARE(decoded.qtVersion(), QStringLiteral("6.11"));
        QCOMPARE(decoded.primaryApplication().uri, QStringLiteral("com.acme.SampleApp"));
        QCOMPARE(decoded.primaryApplication().entry, QStringLiteral("Main"));
    }

    // The scaffolder writes design/tokens.json, the generated CMakeLists.txt
    // passes the same path to loom_add_application as DESIGN, and `loom dev`
    // watches whatever this says. All three have to agree.
    void designPathRoundTrips()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath(QStringLiteral("loom.json"));
        const auto source = ProjectManifest::createDefault(
            QStringLiteral("Sample App"), QStringLiteral("com.acme"));
        QCOMPARE(source.designPath(), QStringLiteral("design/tokens.json"));
        QString error;
        QVERIFY2(source.save(path, &error), qPrintable(error));

        ProjectManifest decoded;
        QVERIFY2(ProjectManifest::load(path, decoded, &error), qPrintable(error));
        QCOMPARE(decoded.designPath(), QStringLiteral("design/tokens.json"));
        // Resolved against the manifest's directory, not the working directory:
        // every command that reads this runs from wherever the user happened to
        // be when they typed it.
        QCOMPARE(
            decoded.resolvedDesignPath(path),
            QDir(temporary.path()).filePath(QStringLiteral("design/tokens.json")));
    }

    // A project that declares no design tokens must not gain a "design": ""
    // key, which the schema rejects and which would resolve to the project
    // directory itself.
    void unsetDesignPathIsNotSerialized()
    {
        ProjectManifest manifest = ProjectManifest::createDefault(
            QStringLiteral("Sample App"), QStringLiteral("com.acme"));
        manifest.setDesignPath(QString());
        QVERIFY(!manifest.toJson().contains(QStringLiteral("design")));
        QVERIFY(manifest.resolvedDesignPath(QStringLiteral("/tmp/loom.json")).isEmpty());
    }

    void rejectsUnknownSchema()
    {
        QTemporaryDir temporary;
        const auto path = temporary.filePath(QStringLiteral("loom.json"));
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(R"({"schemaVersion": 99})");
        output.close();

        ProjectManifest decoded;
        QString error;
        QVERIFY(!ProjectManifest::load(path, decoded, &error));
        QVERIFY(error.contains(QStringLiteral("schemaVersion")));
    }

    // Adding a second application used to silently change what `loom dev`
    // ran: every command took constFirst(), which after load is the
    // alphabetically first target because QJsonObject sorts its keys.
    void multipleApplicationsRequireAChoice()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath(QStringLiteral("loom.json"));
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(R"({
            "schemaVersion": 2,
            "project": { "name": "Suite" },
            "qt": { "version": "6.11" },
            "applications": {
                "Viewer": {
                    "name": "Viewer", "target": "Viewer", "id": "com.example.viewer",
                    "uri": "com.example.Viewer", "entry": "Main",
                    "qmlRoots": ["qml"], "assetRoots": [], "platforms": {"desktop": {}}
                },
                "Admin": {
                    "name": "Admin", "target": "Admin", "id": "com.example.admin",
                    "uri": "com.example.Admin", "entry": "Main",
                    "qmlRoots": ["qml"], "assetRoots": [], "platforms": {"desktop": {}}
                }
            }
        })");
        output.close();

        ProjectManifest manifest;
        QString error;
        QVERIFY2(ProjectManifest::load(path, manifest, &error), qPrintable(error));
        QCOMPARE(manifest.applications().size(), 2);

        ApplicationDefinition selected;
        QVERIFY2(
            !manifest.selectApplication(QString(), selected, &error),
            "an ambiguous project silently picked an application");
        QVERIFY(error.contains(QStringLiteral("--app")));

        QVERIFY2(
            manifest.selectApplication(QStringLiteral("Viewer"), selected, &error),
            qPrintable(error));
        QCOMPARE(selected.target, QStringLiteral("Viewer"));

        QVERIFY(!manifest.selectApplication(QStringLiteral("Nope"), selected, &error));
        QVERIFY(error.contains(QStringLiteral("Admin")));
    }

    void defaultApplicationResolvesTheAmbiguityAndRoundTrips()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath(QStringLiteral("loom.json"));
        auto manifest = ProjectManifest::createDefault(
            QStringLiteral("Suite"), QStringLiteral("com.example"));
        manifest.setDefaultApplication(QStringLiteral("Suite"));
        QString error;
        QVERIFY2(manifest.save(path, &error), qPrintable(error));

        ProjectManifest decoded;
        QVERIFY2(ProjectManifest::load(path, decoded, &error), qPrintable(error));
        QCOMPARE(decoded.defaultApplication(), QStringLiteral("Suite"));

        ApplicationDefinition selected;
        QVERIFY2(
            decoded.selectApplication(QString(), selected, &error), qPrintable(error));
        QCOMPARE(selected.target, QStringLiteral("Suite"));
    }

    // The schema forbids unknown keys, so an unset default must be omitted
    // rather than written as an empty string.
    void unsetDefaultApplicationIsNotSerialized()
    {
        const auto manifest = ProjectManifest::createDefault(
            QStringLiteral("Plain"), QStringLiteral("com.example"));
        const auto project =
            manifest.toJson().value(QStringLiteral("project")).toObject();
        QVERIFY(project.contains(QStringLiteral("name")));
        QVERIFY(!project.contains(QStringLiteral("defaultApplication")));
    }

    void platformOptionsAndEmbeddedProfilesRoundTrip()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("loom.json"));
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(R"({
            "schemaVersion": 2,
            "project": {"name": "Devices"},
            "qt": {"version": "6.11"},
            "embeddedProfiles": {
                "panel": {
                    "toolchainFile": "cmake/panel.cmake",
                    "sysroot": "/opt/panel/sysroot",
                    "host": "panel.local",
                    "remoteDir": "/opt/apps/devices"
                }
            },
            "applications": {
                "Devices": {
                    "name": "Devices", "target": "Devices",
                    "id": "com.example.devices", "uri": "com.example.Devices",
                    "entry": "Main", "qmlRoots": ["qml"], "assetRoots": [],
                    "platforms": {
                        "desktop": {},
                        "android": {"abi": "x86_64", "api": 36},
                        "ios": {
                            "hostQtPath": "/opt/Qt/6.11.1/macos",
                            "destination": "simulator"
                        },
                        "embedded": {"profile": "panel"}
                    }
                }
            }
        })");
        output.close();

        ProjectManifest manifest;
        QString error;
        QVERIFY2(ProjectManifest::load(path, manifest, &error), qPrintable(error));
        const auto application = manifest.primaryApplication();
        QCOMPARE(application.platforms.size(), 4);
        QCOMPARE(
            application.platformOptions.value(QStringLiteral("android"))
                .toObject()
                .value(QStringLiteral("api"))
                .toInt(),
            36);
        QCOMPARE(
            application.platformOptions.value(QStringLiteral("ios"))
                .toObject()
                .value(QStringLiteral("hostQtPath"))
                .toString(),
            QStringLiteral("/opt/Qt/6.11.1/macos"));
        QCOMPARE(
            manifest.embeddedProfiles()
                .value(QStringLiteral("panel"))
                .toObject()
                .value(QStringLiteral("host"))
                .toString(),
            QStringLiteral("panel.local"));
    }

    void schemaV2RejectsLegacyPlatformArraysAndUnknownKeys()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto write = [&temporary](const QString &name, const QByteArray &json) {
            const QString path = temporary.filePath(name);
            QFile output(path);
            if (!output.open(QIODevice::WriteOnly))
                return QString();
            output.write(json);
            return path;
        };
        const QByteArray base = R"({
            "schemaVersion": 2,
            "project": {"name": "Strict"},
            "qt": {"version": "6.11"},
            "applications": {
                "Strict": {
                    "name": "Strict", "target": "Strict",
                    "id": "com.example.strict", "uri": "com.example.Strict",
                    "entry": "Main", "qmlRoots": ["qml"], "assetRoots": [],
                    "platforms": ["desktop"]
                }
            }
        })";
        ProjectManifest manifest;
        QString error;
        QVERIFY(!ProjectManifest::load(
            write(QStringLiteral("legacy.json"), base), manifest, &error));
        QVERIFY(error.contains(QStringLiteral("schema v2")));

        QJsonObject object = QJsonDocument::fromJson(base).object();
        QJsonObject applications =
            object.value(QStringLiteral("applications")).toObject();
        QJsonObject application = applications.value(QStringLiteral("Strict")).toObject();
        application.insert(
            QStringLiteral("platforms"),
            QJsonObject{{QStringLiteral("desktop"), QJsonObject{}}});
        applications.insert(QStringLiteral("Strict"), application);
        object.insert(QStringLiteral("applications"), applications);
        QJsonObject project = object.value(QStringLiteral("project")).toObject();
        project.insert(QStringLiteral("typo"), true);
        object.insert(QStringLiteral("project"), project);
        QVERIFY(!ProjectManifest::load(
            write(
                QStringLiteral("unknown.json"),
                QJsonDocument(object).toJson(QJsonDocument::Compact)),
            manifest, &error));
        QVERIFY(error.contains(QStringLiteral("project.typo")));
    }

    void schemaV2RuntimeValidationMatchesPlatformSchema()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto write = [&temporary](const QString &name, const QJsonObject &json) {
            const QString path = temporary.filePath(name);
            QFile output(path);
            if (!output.open(QIODevice::WriteOnly))
                return QString();
            output.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
            return path;
        };
        auto document = ProjectManifest::createDefault(
                            QStringLiteral("Strict"), QStringLiteral("com.example"))
                            .toJson();
        ProjectManifest manifest;
        QString error;

        QJsonObject applications =
            document.value(QStringLiteral("applications")).toObject();
        QJsonObject application = applications.value(QStringLiteral("Strict")).toObject();
        application.insert(QStringLiteral("target"), QStringLiteral("Different"));
        applications.insert(QStringLiteral("Strict"), application);
        document.insert(QStringLiteral("applications"), applications);
        QVERIFY(!ProjectManifest::load(
            write(QStringLiteral("target.json"), document), manifest, &error));
        QVERIFY(error.contains(QStringLiteral("must match")));

        application.insert(QStringLiteral("target"), QStringLiteral("Strict"));
        QJsonObject platforms = application.value(QStringLiteral("platforms")).toObject();
        platforms.insert(
            QStringLiteral("android"),
            QJsonObject{{QStringLiteral("abi"), QStringLiteral("mips")}});
        application.insert(QStringLiteral("platforms"), platforms);
        applications.insert(QStringLiteral("Strict"), application);
        document.insert(QStringLiteral("applications"), applications);
        QVERIFY(!ProjectManifest::load(
            write(QStringLiteral("abi.json"), document), manifest, &error));
        QVERIFY(error.contains(QStringLiteral("Android ABI")));

        platforms.remove(QStringLiteral("android"));
        application.insert(QStringLiteral("platforms"), platforms);
        applications.insert(QStringLiteral("Strict"), application);
        document.insert(QStringLiteral("applications"), applications);
        document.insert(
            QStringLiteral("embeddedProfiles"),
            QJsonObject{
                {QStringLiteral("panel"),
                 QJsonObject{
                     {QStringLiteral("toolchainFile"), QStringLiteral("kit.cmake")},
                     {QStringLiteral("sysroot"), QStringLiteral("/sdk")},
                     {QStringLiteral("host"), QStringLiteral("panel.local")},
                     {QStringLiteral("remoteDir"), QStringLiteral("/opt/app")},
                     {QStringLiteral("port"), 70000},
                 }}});
        QVERIFY(!ProjectManifest::load(
            write(QStringLiteral("profile.json"), document), manifest, &error));
        QVERIFY(error.contains(QStringLiteral("65535")));

        QJsonObject profiles =
            document.value(QStringLiteral("embeddedProfiles")).toObject();
        QJsonObject panel = profiles.value(QStringLiteral("panel")).toObject();
        panel.insert(QStringLiteral("port"), 22);
        panel.insert(
            QStringLiteral("environment"),
            QJsonObject{{QStringLiteral("BAD-NAME"), QStringLiteral("value")}});
        profiles.insert(QStringLiteral("panel"), panel);
        document.insert(QStringLiteral("embeddedProfiles"), profiles);
        QVERIFY(!ProjectManifest::load(
            write(QStringLiteral("environment.json"), document), manifest, &error));
        QVERIFY(error.contains(QStringLiteral("environment variable name")));
    }

    void findsManifestInParent()
    {
        QTemporaryDir temporary;
        const auto nested = temporary.filePath(QStringLiteral("a/b"));
        QVERIFY(QDir().mkpath(nested));
        QFile manifest(temporary.filePath(QStringLiteral("loom.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.close();
        QCOMPARE(findManifest(nested), QFileInfo(manifest).absoluteFilePath());
    }
};

QTEST_APPLESS_MAIN(ManifestTests)
#include "tst_manifest.moc"
