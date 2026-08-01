#pragma once

#include <QObject>
#include <QStringList>

class Commands final : public QObject {
    Q_OBJECT

public:
    explicit Commands(QObject *parent = nullptr);
    int execute(const QStringList &arguments);

private:
    int createProject(const QStringList &arguments);
    int initializeProject(const QStringList &arguments);
    int doctor(const QStringList &arguments);
    int setup(const QStringList &arguments);
    int build(const QStringList &arguments);
    int lint(const QStringList &arguments);
    int style(const QStringList &arguments);
    int languageServer(const QStringList &arguments);
    int format(const QStringList &arguments);
    int clean(const QStringList &arguments);
    int test(const QStringList &arguments);
    int develop(const QStringList &arguments);
    int deploy(const QStringList &arguments);
    int migrate(const QStringList &arguments);
    void printHelp() const;
};
