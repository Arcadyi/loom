#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QScopedPointer>
#include <loom/loom.h>

class ReactivityTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void semanticTokenFollowsTheme();
    void darkVariant();
    void unmanagedPropertiesSurviveThemeSwitch();
};

void ReactivityTests::cleanup()
{
    loom::setTheme(QStringLiteral("light"));
}

void ReactivityTests::semanticTokenFollowsTheme()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-surface\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x0f, 0x17, 0x2a));

    loom::setTheme(QStringLiteral("light"));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void ReactivityTests::darkVariant()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white dark:bg-black rounded dark:rounded-xl\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    // The initial color is already white, so radius is the write to wait on.
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);
    QCOMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));
    QCOMPARE(item->property("radius").toReal(), 12.0);
}

void ReactivityTests::unmanagedPropertiesSurviveThemeSwitch()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 123\n"
        "    border.width: 7\n"
        "    Lo.style: \"bg-surface\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x0f, 0x17, 0x2a));
    QCOMPARE(item->width(), 123.0);
    QCOMPARE(QQmlProperty(item.data(), "border.width").read().toReal(), 7.0);
}

QTEST_MAIN(ReactivityTests)
#include "tst_reactivity.moc"
