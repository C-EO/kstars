/*  KStars UI tests
    SPDX-FileCopyrightText: 2018, 2020 Csaba Kertesz <csaba.kertesz@gmail.com>
    SPDX-FileCopyrightText: Jasem Mutlaq <knro@ikarustech.com>
    SPDX-FileCopyrightText: Eric Dejouhanet <eric.dejouhanet@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TEST_EKOS_H
#define TEST_EKOS_H

#include "config-kstars.h"

#if defined(HAVE_INDI)

#include <KActionCollection>

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtTest/QTest>
#else
#include <QTest>
#endif

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QListView>
#include <QMetaObject>
#include <QTreeView>

#include "kstars.h"
#include "ekos/manager.h"
#include "ekos/profileeditor.h"
#include "test_kstars_startup.h"

inline bool kstarsTestRequiresActiveWindow()
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
        return false;
    if (!qEnvironmentVariableIsSet("DISPLAY") && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return false;
    return true;
}

inline void kstarsTestAddProfileDriver(const QString &label)
{
    ProfileEditor *profileEditor = Ekos::Manager::Instance()->findChild<ProfileEditor*>("profileEditorDialog");
    QVERIFY2(profileEditor != nullptr && profileEditor->isVisible(), "Profile Editor is not visible.");

    QTreeView *driversTree = profileEditor->findChild<QTreeView*>("driversTree");
    QVERIFY2(driversTree != nullptr, "driversTree does not exist and cannot be used");
    QAbstractItemModel *driversModel = driversTree->model();
    QVERIFY2(driversModel != nullptr, "driversTree model is missing");

    QModelIndexList matches = driversModel->match(driversModel->index(0, 0), Qt::DisplayRole, label, 1, Qt::MatchRecursive);
    QVERIFY2(!matches.isEmpty(), QString("Driver '%1' not found in drivers tree").arg(label).toStdString().c_str());
    const QModelIndex index = matches.first();

    driversTree->setCurrentIndex(index);
    driversTree->scrollTo(index);
    const bool invoked = QMetaObject::invokeMethod(profileEditor, "addDriver", Qt::DirectConnection,
                         Q_ARG(QModelIndex, index));
    if (!invoked)
    {
        const QRect rect = driversTree->visualRect(index);
        QTest::mouseDClick(driversTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    }

    QListView *profileDriversList = profileEditor->findChild<QListView*>("profileDriversList");
    QVERIFY2(profileDriversList != nullptr, "profileDriversList does not exist and cannot be used");
    QAbstractItemModel *profileModel = profileDriversList->model();
    QVERIFY2(profileModel != nullptr, "profileDriversList model is missing");
    QTRY_VERIFY_WITH_TIMEOUT(!profileModel->match(profileModel->index(0, 0), Qt::DisplayRole, label, 1,
                             Qt::MatchRecursive).isEmpty(),
                             2000);
}

#define KVERIFY_EKOS_IS_HIDDEN() do { \
    if (Ekos::Manager::Instance() != nullptr) { \
        QVERIFY(!Ekos::Manager::Instance()->isVisible()); \
        if (kstarsTestRequiresActiveWindow()) \
            QVERIFY(!Ekos::Manager::Instance()->isActiveWindow()); }} while(false)

#define KVERIFY_EKOS_IS_OPENED() do { \
    QVERIFY(Ekos::Manager::Instance() != nullptr); \
    QVERIFY(Ekos::Manager::Instance()->isVisible()); \
    if (kstarsTestRequiresActiveWindow()) \
        QVERIFY(Ekos::Manager::Instance()->isActiveWindow()); } while(false)

#define KTRY_OPEN_EKOS() do { \
    if (Ekos::Manager::Instance() == nullptr || !Ekos::Manager::Instance()->isVisible()) { \
        KTRY_ACTION("show_ekos"); \
        QTRY_VERIFY_WITH_TIMEOUT(Ekos::Manager::Instance() != nullptr, 200); \
        QTRY_VERIFY_WITH_TIMEOUT(Ekos::Manager::Instance()->isVisible(), 200); \
        if (kstarsTestRequiresActiveWindow()) \
            QTRY_VERIFY_WITH_TIMEOUT(Ekos::Manager::Instance()->isActiveWindow(), 5000); }} while(false)

#define KTRY_CLOSE_EKOS() do { \
    if (Ekos::Manager::Instance() != nullptr && Ekos::Manager::Instance()->isVisible()) { \
        KTRY_ACTION("show_ekos"); \
        if (kstarsTestRequiresActiveWindow()) \
            QTRY_VERIFY_WITH_TIMEOUT(!Ekos::Manager::Instance()->isActiveWindow(), 200); \
        QTRY_VERIFY_WITH_TIMEOUT(!Ekos::Manager::Instance()->isVisible(), 200); }} while(false)

#define KHACK_RESET_EKOS_TIME() do { \
    QWARN("HACK HACK HACK: Reset clock to initial conditions when starting Ekos"); \
    if (KStars::Instance() != nullptr) \
        if (KStars::Instance()->data() != nullptr) \
            KStars::Instance()->data()->clock()->setUTC(KStarsDateTime(TestKStarsStartup::m_InitialConditions.dateTime)); } while(false)

#define KTRY_PROFILEEDITOR_GADGET(klass, name) klass * name = nullptr; \
    do { \
        ProfileEditor* profileEditor = Ekos::Manager::Instance()->findChild<ProfileEditor*>("profileEditorDialog"); \
        QVERIFY2(profileEditor != nullptr && profileEditor->isVisible(), "Profile Editor is not visible."); \
        name = Ekos::Manager::Instance()->findChild<klass*>(#name); \
        QVERIFY2(name != nullptr, QString(#klass "'%1' does not exist and cannot be used").arg(#name).toStdString().c_str()); \
    } while(false)

#define KTRY_PROFILEEDITOR_TREE_COMBOBOX(name, strvalue) \
    do { \
    ProfileEditor* profileEditor = Ekos::Manager::Instance()->findChild<ProfileEditor*>("profileEditorDialog"); \
    QVERIFY2(profileEditor != nullptr && profileEditor->isVisible(), "Profile Editor is not visible."); \
    QString lookup(strvalue); \
    QComboBox * name = profileEditor->findChild<QComboBox*>(#name); \
    if (name != nullptr) { \
        QModelIndexList const list = name->model()->match(name->model()->index(0, 0), Qt::DisplayRole, QVariant::fromValue(lookup), 1, Qt::MatchRecursive); \
        QVERIFY(0 < list.count()); \
        QModelIndex const &item = list.first(); \
        QCOMPARE(list.value(0).data().toString(), lookup); \
        QVERIFY(!item.parent().parent().isValid()); \
        name->setRootModelIndex(item.parent()); \
        name->setCurrentText(lookup); \
        QCOMPARE(name->currentText(), lookup); \
    } else { \
        kstarsTestAddProfileDriver(lookup); \
    }} while(false)

class TestEkos: public QObject
{
        Q_OBJECT
    public:
        explicit TestEkos(QObject *parent = nullptr);

    private Q_SLOTS:
        void initTestCase();
        void cleanupTestCase();

        void init();
        void cleanup();

        void testOpenClose();
        void testSimulatorProfile();
        void testManipulateProfiles();
};

#endif // HAVE_INDI
#endif // TEST_EKOS_H
