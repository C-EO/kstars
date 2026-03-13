#ifndef TESTHELPERS_H
#define TESTHELPERS_H

#include "config-kstars.h"

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtTest/QTest>
#else
#include <QTest>
#endif

#include <QObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStandardPaths>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>

#include "kstars.h"
#include "kspaths.h"

namespace KTest
{
namespace Internal
{
inline bool isLoopbackHost(const QString &hostName)
{
    if (hostName.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        return true;

    QHostAddress address;
    return address.setAddress(hostName) && address.isLoopback();
}

class NoNetworkProxyFactory final : public QNetworkProxyFactory
{
    public:
        QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
        {
            const QString hostName = !query.peerHostName().isEmpty() ? query.peerHostName() : query.url().host();
            if (Internal::isLoopbackHost(hostName))
                return {QNetworkProxy(QNetworkProxy::NoProxy)};

            return {QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 9)};
        }
};
}

inline bool copyRecursively(const QString &sourcePath, const QString &targetPath)
{
    QFileInfo sourceInfo(sourcePath);
    if (sourceInfo.isDir())
    {
        QDir targetDir(targetPath);
        if (!targetDir.exists() && !targetDir.mkpath("."))
            return false;
        QDir sourceDir(sourcePath);
        const QFileInfoList entries = sourceDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
        {
            const QString target = targetDir.filePath(entry.fileName());
            if (!copyRecursively(entry.filePath(), target))
                return false;
        }
        return true;
    }

    if (QFileInfo::exists(targetPath))
        return true;
    return QFile::copy(sourcePath, targetPath);
}

inline QString rootDir()
{
    static const QString root = []()
    {
        QString base = QDir::tempPath();
        const QString path = QDir(base).filePath(QStringLiteral("kstars-tests-%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(path);
        return path;
    }
    ();

    return root;
}

inline QString homePath()
{
    const QString path = QDir(rootDir()).filePath(QStringLiteral("home"));
    QDir().mkpath(path);
    return path;
}

inline QString tempPath()
{
    const QString path = QDir(rootDir()).filePath(QStringLiteral("tmp"));
    QDir().mkpath(path);
    return path;
}

inline QString tempDirPattern(const QString &prefix = QStringLiteral("test"))
{
    return QDir(tempPath()).filePath(prefix + QStringLiteral("-XXXXXX"));
}

inline QString devShareDir()
{
    const QString path = QDir(rootDir()).filePath(QStringLiteral("devshare"));
    QDir().mkpath(path);
    return path;
}

inline QString kstarsDataDir()
{
#ifdef KSTARS_TEST_SOURCEDIR
    return QDir(QString::fromUtf8(KSTARS_TEST_SOURCEDIR)).filePath(QStringLiteral("kstars/data"));
#else
    const QByteArray envDataDir = qgetenv("KSTARS_TEST_DATADIR");
    if (!envDataDir.isEmpty())
        return QString::fromUtf8(envDataDir);

    const QByteArray envSourceDir = qgetenv("KSTARS_TEST_SOURCEDIR");
    if (!envSourceDir.isEmpty())
        return QDir(QString::fromUtf8(envSourceDir)).filePath(QStringLiteral("kstars/data"));

    return {};
#endif
}

inline void setupTestEnvironment()
{
    // Only disable networking when a test explicitly requests it.
    // `KSTARS_TEST_ENABLE_NETWORK=1` wins over a disabled-network request.
    const bool enableNetwork = qEnvironmentVariableIsSet("KSTARS_TEST_ENABLE_NETWORK") &&
                               qgetenv("KSTARS_TEST_ENABLE_NETWORK") != QByteArray("0");
    const bool disableNetwork = !enableNetwork &&
                                qEnvironmentVariableIsSet("KSTARS_TEST_NO_NETWORK") &&
                                qgetenv("KSTARS_TEST_NO_NETWORK") != QByteArray("0");
    if (disableNetwork)
    {
        qputenv("KSTARS_TEST_NO_NETWORK", "1");
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxyFactory::setApplicationProxyFactory(new Internal::NoNetworkProxyFactory());
    }
    else
    {
        qunsetenv("KSTARS_TEST_NO_NETWORK");
    }

    const QString home = homePath();
    qputenv("HOME", home.toUtf8());
    qputenv("USERPROFILE", home.toUtf8());
    qputenv("HOMEPATH", home.toUtf8());

    const QString tmp = tempPath();
    qputenv("TMPDIR", tmp.toUtf8());
    qputenv("TMP", tmp.toUtf8());
    qputenv("TEMP", tmp.toUtf8());

    const QString runtime = QDir(rootDir()).filePath(QStringLiteral("run"));
    QDir().mkpath(runtime);
    QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("XDG_RUNTIME_DIR", runtime.toUtf8());

    // Tests override XDG_RUNTIME_DIR for isolation, which breaks inherited Wayland sessions.
    // Ensure Qt does not try to use a non-existent Wayland socket under the test runtime dir.
    const QByteArray display = qgetenv("DISPLAY");
    qunsetenv("WAYLAND_DISPLAY");
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && display.isEmpty())
        qputenv("QT_QPA_PLATFORM", "offscreen");

    const QString dataHome = QDir(rootDir()).filePath(QStringLiteral("data"));
    const QString configHome = QDir(rootDir()).filePath(QStringLiteral("config"));
    const QString cacheHome = QDir(rootDir()).filePath(QStringLiteral("cache"));
    QDir().mkpath(dataHome);
    QDir().mkpath(configHome);
    QDir().mkpath(cacheHome);
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
    qputenv("XDG_CACHE_HOME", cacheHome.toUtf8());

    const QString datadir = kstarsDataDir();
    if (!datadir.isEmpty())
        qputenv("KSTARS_TEST_DATADIR", datadir.toUtf8());

    const QString devshare = devShareDir();
    const QByteArray existingDataDirs = qgetenv("XDG_DATA_DIRS");
    if (existingDataDirs.isEmpty())
        qputenv("XDG_DATA_DIRS", (devshare + QStringLiteral(":/usr/local/share:/usr/share")).toUtf8());
    else
        qputenv("XDG_DATA_DIRS", (devshare + QStringLiteral(":") + QString::fromUtf8(existingDataDirs)).toUtf8());
}

inline void installTestData()
{
    const QString source = kstarsDataDir();
    if (source.isEmpty())
        return;

    const QString dest = KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dest.isEmpty())
        return;

    QDir destDir(dest);
    if (!destDir.exists())
        destDir.mkpath(".");

    QDir sourceDir(source);
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries)
    {
        const QString target = destDir.filePath(entry.fileName());
        if (QFileInfo::exists(target))
            continue;
        if (!QFile::link(entry.filePath(), target))
            copyRecursively(entry.filePath(), target);
    }

    const QString vsopSource = QDir(source).filePath(QStringLiteral("vsop87"));
    if (QDir(vsopSource).exists())
    {
        const QFileInfoList vsopEntries = QDir(vsopSource).entryInfoList(QStringList() << QStringLiteral("*.vsop"), QDir::Files);
        for (const QFileInfo &entry : vsopEntries)
        {
            const QString target = destDir.filePath(entry.fileName());
            if (QFileInfo::exists(target))
                continue;
            if (!QFile::link(entry.filePath(), target))
                QFile::copy(entry.filePath(), target);
        }
    }

    const QString shareRoot = devShareDir();
    const QString shareKstars = QDir(shareRoot).filePath(QStringLiteral("kstars"));
    if (!QFileInfo::exists(shareKstars))
    {
        if (!QFile::link(source, shareKstars))
            copyRecursively(source, shareKstars);
    }
    else if (!QFileInfo(shareKstars).isSymLink())
    {
        const QString shareVsop = QDir(shareKstars).filePath(QStringLiteral("vsop87"));
        if (QDir(shareVsop).exists())
        {
            const QFileInfoList vsopEntries = QDir(shareVsop).entryInfoList(QStringList() << QStringLiteral("*.vsop"), QDir::Files);
            for (const QFileInfo &entry : vsopEntries)
            {
                const QString target = QDir(shareKstars).filePath(entry.fileName());
                if (QFileInfo::exists(target))
                    continue;
                if (!QFile::link(entry.filePath(), target))
                    QFile::copy(entry.filePath(), target);
            }
        }
    }
}

struct EarlyEnvironment
{
    EarlyEnvironment()
    {
        setupTestEnvironment();
    }
};

static const EarlyEnvironment kEarlyEnvironment;
}

/** @brief Helper to clean application user folders when in test mode.
 *
 * It verifies that App(Data|Config|Cache)Location folders can be removed, that KSPaths::writableLocation
 * will not recreate them by itself and that those folders can be recreated manually.
 *
 * @param recreate is a boolean that requires the helper to recreate the application user folders after removal.
 */
#define KTEST_CLEAN_TEST(recreate) do { \
    if (!QStandardPaths::isTestModeEnabled()) \
        qFatal("Helper KTEST_CLEAN_TEST only works in test mode."); \
    QList<QStandardPaths::StandardLocation> const locs = { \
        QStandardPaths::AppLocalDataLocation, \
        QStandardPaths::AppConfigLocation, \
        QStandardPaths::CacheLocation }; \
    for (auto loc: locs) { \
        QString const path = KSPaths::writableLocation(loc); \
        if (!QDir(path).removeRecursively()) \
            qFatal("Local application location '%s' must be removable.", qPrintable(path)); \
        if (QDir(KSPaths::writableLocation(loc)).exists()) \
            qFatal("Local application location '%s' must not exist after having been removed.", qPrintable(path)); \
        if (recreate) \
            if (!QDir(path).mkpath(".")) \
                qFatal("Local application location '%s' must be recreatable.", qPrintable(path)); \
        }} while(false)

#define KTEST_CLEAN_RCFILE() do { \
    if (!QStandardPaths::isTestModeEnabled()) \
        qFatal("Helper KTEST_CLEAN_RCFILE only works in test mode."); \
    const QString rcfilepath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(qAppName() + "rc"); \
    if (QFileInfo(rcfilepath).exists()) { \
        if (!QFile(rcfilepath).remove()) \
            qFatal("Local application location RC file must be removable."); \
    }} while(false)

/** @brief Helper to begin a test.
 *
 * For now, this puts the application paths in test mode, and removes and recreates
 * the three application user folders.
 */
#define KTEST_BEGIN() do { \
    QCoreApplication::setApplicationName(QStringLiteral("kstars")); \
    QCoreApplication::setOrganizationDomain(QStringLiteral("kde.org")); \
    QCoreApplication::setOrganizationName(QStringLiteral("KDE")); \
    KTest::setupTestEnvironment(); \
    QStandardPaths::setTestModeEnabled(true); \
    KTEST_CLEAN_TEST(true); \
    KTest::installTestData(); \
    KTEST_CLEAN_RCFILE(); \
} while(false)

/** @brief Helper to end a test.
 *
 * For now, this removes the three application user folders.
 */
#define KTEST_END() do { \
    KTEST_CLEAN_RCFILE(); \
    KTEST_CLEAN_TEST(false); } while(false)

#endif // TESTHELPERS_H
