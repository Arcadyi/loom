#pragma once

#include "devserver.h"
#include "projectmanifest.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

class QSocketNotifier;

// Owns the whole `loom dev` loop: the reload server, the application process,
// the rebuild-on-native-change cycle, and shutdown.
//
// Extracted from a pile of lambdas inside Commands::develop, which is why three
// bugs coexisted there: the rebuild lambda captured a raw QProcess* that the
// finished handler had already deleteLater()'d; a restart that could not start
// emitted no finished signal and so hung in exec() forever with no application
// and no nonzero exit; and a signalled loom dev left an orphaned child holding
// a bound port.
class DevSession final : public QObject {
    Q_OBJECT

public:
    struct Configuration {
        QString projectRoot;
        ApplicationDefinition application;
        QString buildDirectory;
        QString buildConfiguration;
        QStringList cmakeArguments;
        QString generator;
        QString executable;
        QStringList applicationArguments;
    };

    explicit DevSession(Configuration configuration, QObject *parent = nullptr);
    ~DevSession() override;

    // Starts everything and runs the event loop. Returns the application's exit
    // code, or a loom exit code if the session could not start.
    int run(QString *error = nullptr);

private:
    void startApplication();
    void stopApplication();
    void handleNativeFilesChanged();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void handleProcessError(QProcess::ProcessError error);
    void finish(int exitCode);
    void installSignalHandlers();
    void handleSignal();

    Configuration m_configuration;
    DevServer m_server;
    // By value. A member the session outlives cannot be deleted out from under
    // a queued signal the way the old heap-allocated one could.
    QProcess m_process;
    QSocketNotifier *m_signalNotifier = nullptr;
    bool m_restarting = false;
    bool m_finishing = false;
    bool m_eventLoopRunning = false;
};
