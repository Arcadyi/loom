#pragma once

#include <QList>
#include <QString>

struct ToolCheck {
    QString name;
    bool available = false;
    QString detail;
    QString remediation;
    // False where the check only proves the tool is on PATH. Saying so is the
    // difference between a report and PATH theater: the android/ios/embedded
    // checks cannot validate versions or SDK contents, and used to be presented
    // exactly like the ones that can.
    bool validated = true;
};

class ToolchainDoctor {
public:
    static QList<ToolCheck> inspect(const QString &target);
    static int printReport(const QString &target, bool asJson = false);
    static int printSetupPlan(const QString &target);
};
