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
            "schemaVersion": 1,
            "project": { "name": "Suite" },
            "qt": { "version": "6.11" },
            "applications": {
                "Viewer": {
                    "name": "Viewer", "target": "Viewer", "id": "com.example.viewer",
                    "uri": "com.example.Viewer", "entry": "Main",
                    "qmlRoots": ["qml"], "assetRoots": [], "platforms": ["desktop"]
                },
                "Admin": {
                    "name": "Admin", "target": "Admin", "id": "com.example.admin",
                    "uri": "com.example.Admin", "entry": "Main",
                    "qmlRoots": ["qml"], "assetRoots": [], "platforms": ["desktop"]
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
