/*
    SPDX-FileCopyrightText: 2001 Heiko Evermann <heiko@evermann.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kstarsdata.h"

#include "ksutils.h"
#include "Options.h"
#include "auxiliary/kspaths.h"
#include "skycomponents/supernovaecomponent.h"
#include "skycomponents/skymapcomposite.h"
#include "ksnotification.h"
#include "skyobjectuserdata.h"
#include <kio/job_base.h>
#include <kio/filecopyjob.h>
#ifndef KSTARS_LITE
#include "fov.h"
#include "imageexporter.h"
#include "kstars.h"
#include "observinglist.h"
#ifdef HAVE_INDI
#include "tools/imagingplanner.h"
#endif
#include "skymap.h"
#include "dialogs/detaildialog.h"
#include "oal/execute.h"
#endif

#ifndef KSTARS_LITE
#include <KMessageBox>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QtConcurrent>

#include "kstars_debug.h"

// Qt version calming
#include <qtskipemptyparts.h>

namespace
{
// Report fatal error during data loading to user
// Calls QApplication::exit
void fatalErrorMessage(QString fname)
{
    qCCritical(KSTARS) << i18n("Critical File not Found: %1", fname);
    KSNotification::sorry(i18n("The file  %1 could not be found. "
                               "KStars cannot run properly without this file. "
                               "KStars searches for this file in following locations:\n\n\t"
                               "%2\n\n"
                               "It appears that your setup is broken.",
                               fname, QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).join("\n\t")),
                          i18n("Critical File Not Found: %1", fname)); // FIXME: Must list locations depending on file type

    qApp->exit(1);
}

// Report non-fatal error during data loading to user and ask
// whether he wants to continue.
//
// No remaining calls so commented out to suppress unused warning
//
// Calls QApplication::exit if he don't
//bool nonFatalErrorMessage(QString fname)
//{
//    qCWarning(KSTARS) << i18n( "Non-Critical File Not Found: %1", fname );
//#ifdef KSTARS_LITE
//    Q_UNUSED(fname);
//    return true;
//#else
//    int res = KMessageBox::warningContinueCancel(nullptr,
//              i18n("The file %1 could not be found. "
//                   "KStars can still run without this file. "
//                   "KStars search for this file in following locations:\n\n\t"
//                   "%2\n\n"
//                   "It appears that you setup is broken. Press Continue to run KStars without this file ",
//                   fname, QStandardPaths::standardLocations( QStandardPaths::AppLocalDataLocation ).join("\n\t") ),
//              i18n( "Non-Critical File Not Found: %1", fname ));  // FIXME: Must list locations depending on file type
//    if( res != KMessageBox::Continue )
//        qApp->exit(1);
//    return res == KMessageBox::Continue;
//#endif
//}
}

KStarsData *KStarsData::pinstance = nullptr;

KStarsData *KStarsData::Create()
{
    // This method should never be called twice within a run, since a
    // lot of the code assumes that KStarsData, once created, is never
    // destroyed. They maintain local copies of KStarsData::Instance()
    // for efficiency (maybe this should change, but it is not
    // required to delete and reinstantiate KStarsData). Thus, when we
    // call this method, pinstance MUST be zero, i.e. this must be the
    // first (and last) time we are calling it. -- asimha
    Q_ASSERT(!pinstance);

    delete pinstance;
    pinstance = new KStarsData();
    return pinstance;
}

KStarsData::KStarsData()
    : m_Geo(dms(0), dms(0)), m_ksuserdb(),
      temporaryTrail(false),
      //locale( new KLocale( "kstars" ) ),
      m_preUpdateID(0), m_updateID(0), m_preUpdateNumID(0), m_updateNumID(0), m_preUpdateNum(J2000), m_updateNum(J2000)
{
#ifndef KSTARS_LITE
    m_LogObject.reset(new OAL::Log);
#endif
    // at startup times run forward
    setTimeDirection(0.0);
}

KStarsData::~KStarsData()
{
    Q_ASSERT(pinstance);

    //delete locale;
    qDeleteAll(geoList);
    geoList.clear();
    qDeleteAll(ADVtreeList);
    ADVtreeList.clear();

    pinstance = nullptr;
}

bool KStarsData::initialize()
{
    //Load Time Zone Rules//
    emit progressText(i18n("Reading time zone rules"));
    if (!readTimeZoneRulebook())
    {
        fatalErrorMessage("TZrules.dat");
        return false;
    }

    emit progressText(
        i18n("Upgrade existing user city db to support geographic elevation."));

    QString dbfile = QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("mycitydb.sqlite");

    /// This code to add Height column to table city in mycitydb.sqlite is a transitional measure to support a meaningful
    /// geographic elevation.
    if (QFile::exists(dbfile))
    {
        QSqlDatabase fixcitydb = QSqlDatabase::addDatabase("QSQLITE", "fixcitydb");

        fixcitydb.setDatabaseName(dbfile);
        fixcitydb.open();

        if (fixcitydb.tables().contains("city", Qt::CaseInsensitive))
        {
            QSqlRecord r = fixcitydb.record("city");
            if (!r.contains("Elevation"))
            {
                emit progressText(i18n("Adding \"Elevation\" column to city table."));

                QSqlQuery query(fixcitydb);
                if (query.exec(
                            "alter table city add column Elevation real default -10;") ==
                        false)
                {
                    emit progressText(QString("failed to add Elevation column to city "
                                              "table in mycitydb.sqlite: &1")
                                      .arg(query.lastError().text()));
                }
            }
            else
            {
                emit progressText(i18n("City table already contains \"Elevation\"."));
            }
        }
        else
        {
            emit progressText(i18n("City table missing from database."));
        }
        fixcitydb.close();
    }

    //Load Cities//
    emit progressText(i18n("Loading city data"));
    if (!readCityData())
    {
        fatalErrorMessage("citydb.sqlite");
        return false;
    }

    //Initialize User Database//
    emit progressText(i18n("Loading User Information"));
    m_ksuserdb.Initialize();

    //Initialize SkyMapComposite//
    emit progressText(i18n("Loading sky objects"));
    m_SkyComposite.reset(new SkyMapComposite());
    //Load Image URLs//
    //#ifndef Q_OS_ANDROID
    //On Android these 2 calls produce segfault. WARNING
    emit progressText(i18n("Loading Image URLs"));

    // if (!readURLData("image_url.dat", SkyObjectUserdata::Type::image) &&
    //     !nonFatalErrorMessage("image_url.dat"))
    //     return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QFuture<bool> future = QtConcurrent::run(&KStarsData::readURLData, this, QString("image_url.dat"),
                           SkyObjectUserdata::Type::image);
#else
    QtConcurrent::run(this, &KStarsData::readURLData, QString("image_url.dat"),
                      SkyObjectUserdata::Type::image);
#endif

    //Load Information URLs//
    //emit progressText(i18n("Loading Information URLs"));
    // if (!readURLData("info_url.dat", SkyObjectUserdata::Type::website) &&
    //     !nonFatalErrorMessage("info_url.dat"))
    //     return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    future = QtConcurrent::run(&KStarsData::readURLData, this, QString("info_url.dat"),
                               SkyObjectUserdata::Type::website);
#else
    QtConcurrent::run(this, &KStarsData::readURLData, QString("info_url.dat"),
                      SkyObjectUserdata::Type::website);
#endif

    //#endif
    //emit progressText( i18n("Loading Variable Stars" ) );

#ifndef KSTARS_LITE
    //Initialize Observing List and imaging planner
    m_ObservingList = new ObservingList();
#ifdef HAVE_INDI
    m_ImagingPlanner.reset(new ImagingPlanner());
#endif
#endif

    readUserLog();

#ifndef KSTARS_LITE
    readADVTreeData();
#endif
    return true;
}

void KStarsData::updateTime(GeoLocation *geo, const bool automaticDSTchange)
{
    // sync LTime with the simulation clock
    LTime = geo->UTtoLT(ut());
    syncLST();

    //Only check DST if (1) TZrule is not the empty rule, and (2) if we have crossed
    //the DST change date/time.
    if (!geo->tzrule()->isEmptyRule())
    {
        if (TimeRunsForward)
        {
            // timedirection is forward
            // DST change happens if current date is bigger than next calculated dst change
            if (ut() > NextDSTChange)
                resetToNewDST(geo, automaticDSTchange);
        }
        else
        {
            // timedirection is backward
            // DST change happens if current date is smaller than next calculated dst change
            if (ut() < NextDSTChange)
                resetToNewDST(geo, automaticDSTchange);
        }
    }

    KSNumbers num(ut().djd());

    if (std::abs(ut().djd() - LastNumUpdate.djd()) > 1.0)
    {
        LastNumUpdate = KStarsDateTime(ut().djd());
        m_preUpdateNumID++;
        m_preUpdateNum = KSNumbers(num);
        skyComposite()->update(&num);
    }

    if (std::abs(ut().djd() - LastPlanetUpdate.djd()) > 0.01)
    {
        LastPlanetUpdate = KStarsDateTime(ut().djd());
        skyComposite()->updateSolarSystemBodies(&num);
    }

    // Moon moves ~30 arcmin/hr, so update its position every minute.
    if (std::abs(ut().djd() - LastMoonUpdate.djd()) > 0.00069444)
    {
        LastMoonUpdate = ut();
        skyComposite()->updateMoons(&num);
    }

    //Update Alt/Az coordinates.  Timescale varies with zoom level
    //If Clock is in Manual Mode, always update. (?)
    if (std::abs(ut().djd() - LastSkyUpdate.djd()) > 0.1 / Options::zoomFactor() || clock()->isManualMode())
    {
        LastSkyUpdate = ut();
        m_preUpdateID++;
        //omit KSNumbers arg == just update Alt/Az coords // <-- Eh? -- asimha. Looks like this behavior / ideology has changed drastically.
        skyComposite()->update(&num);

        emit skyUpdate(clock()->isManualMode());
    }
}

void KStarsData::syncUpdateIDs()
{
    m_updateID = m_preUpdateID;
    if (m_updateNumID == m_preUpdateNumID)
        return;
    m_updateNumID = m_preUpdateNumID;
    m_updateNum   = KSNumbers(m_preUpdateNum);
}

unsigned int KStarsData::incUpdateID()
{
    m_preUpdateID++;
    m_preUpdateNumID++;
    syncUpdateIDs();
    return m_updateID;
}

void KStarsData::setFullTimeUpdate()
{
    //Set the update markers to invalid dates to trigger updates in each category
    LastSkyUpdate    = KStarsDateTime(QDateTime());
    LastPlanetUpdate = KStarsDateTime(QDateTime());
    LastMoonUpdate   = KStarsDateTime(QDateTime());
    LastNumUpdate    = KStarsDateTime(QDateTime());
}

void KStarsData::syncLST()
{
    LST = geo()->GSTtoLST(ut().gst());
}

void KStarsData::changeDateTime(const KStarsDateTime &newDate)
{
    //Turn off animated slews for the next time step.
    setSnapNextFocus();

    clock()->setUTC(newDate);

    LTime = geo()->UTtoLT(ut());
    //set local sideral time
    syncLST();

    //Make sure Numbers, Moon, planets, and sky objects are updated immediately
    setFullTimeUpdate();

    // reset tzrules data with new local time and time direction (forward or backward)
    geo()->tzrule()->reset_with_ltime(LTime, geo()->TZ0(), isTimeRunningForward());

    // reset next dst change time
    setNextDSTChange(geo()->tzrule()->nextDSTChange());
}

void KStarsData::resetToNewDST(GeoLocation *geo, const bool automaticDSTchange)
{
    // reset tzrules data with local time, timezone offset and time direction (forward or backward)
    // force a DST change with option true for 3. parameter
    geo->tzrule()->reset_with_ltime(LTime, geo->TZ0(), TimeRunsForward, automaticDSTchange);
    // reset next DST change time
    setNextDSTChange(geo->tzrule()->nextDSTChange());
    //reset LTime, because TZoffset has changed
    LTime = geo->UTtoLT(ut());
}

void KStarsData::setTimeDirection(float scale)
{
    TimeRunsForward = scale >= 0;
}

GeoLocation *KStarsData::locationNamed(const QString &city, const QString &province, const QString &country)
{
    foreach (GeoLocation *loc, geoList)
    {
        if (loc->translatedName() == city && (province.isEmpty() || loc->translatedProvince() == province) &&
                (country.isEmpty() || loc->translatedCountry() == country))
        {
            return loc;
        }
    }
    return nullptr;
}

GeoLocation *KStarsData::nearestLocation(double longitude, double latitude)
{
    GeoLocation *nearest = nullptr;
    double distance = 1e6;

    dms lng(longitude), lat(latitude);
    for (auto oneCity : geoList)
    {
        double newDistance = oneCity->distanceTo(lng, lat);
        if (newDistance < distance)
        {
            distance = newDistance;
            nearest = oneCity;
        }
    }

    return nearest;
}

void KStarsData::setLocationFromOptions()
{
    setLocation(GeoLocation(dms(Options::longitude()), dms(Options::latitude()), Options::cityName(),
                            Options::provinceName(), Options::countryName(), Options::timeZone(),
                            &(Rulebook[Options::dST()]), Options::elevation(), false, 4));
}

void KStarsData::setLocation(const GeoLocation &l)
{
    m_Geo = GeoLocation(l);
    if (m_Geo.lat()->Degrees() >= 90.0)
        m_Geo.setLat(dms(89.99));
    if (m_Geo.lat()->Degrees() <= -90.0)
        m_Geo.setLat(dms(-89.99));

    //store data in the Options objects
    Options::setCityName(m_Geo.name());
    Options::setProvinceName(m_Geo.province());
    Options::setCountryName(m_Geo.country());
    Options::setTimeZone(m_Geo.TZ0());
    Options::setElevation(m_Geo.elevation());
    Options::setLongitude(m_Geo.lng()->Degrees());
    Options::setLatitude(m_Geo.lat()->Degrees());
    // set the rule from rulebook
    foreach (const QString &key, Rulebook.keys())
    {
        if (!key.isEmpty() && m_Geo.tzrule()->equals(&Rulebook[key]))
            Options::setDST(key);
    }

    emit geoChanged();
}

SkyObject *KStarsData::objectNamed(const QString &name)
{
    if ((name == "star") || (name == "nothing") || name.isEmpty())
        return nullptr;
    return skyComposite()->findByName(name, true); // objectNamed has to do an exact match
}

bool KStarsData::readCityData()
{
    QSqlDatabase citydb = QSqlDatabase::addDatabase("QSQLITE", "citydb");
    QString dbfile      = KSPaths::locate(QStandardPaths::AppLocalDataLocation, "citydb.sqlite");
    citydb.setDatabaseName(dbfile);
    if (citydb.open() == false)
    {
        qCCritical(KSTARS) << "Unable to open city database file " << dbfile << citydb.lastError().text();
        return false;
    }

    QSqlQuery get_query(citydb);

    //get_query.prepare("SELECT * FROM city");
    if (!get_query.exec("SELECT * FROM city"))
    {
        qCCritical(KSTARS) << get_query.lastError();
        return false;
    }

    bool citiesFound = false;
    // get_query.size() always returns -1 so we set citiesFound if at least one city is found
    while (get_query.next())
    {
        citiesFound          = true;
        QString name         = get_query.value(1).toString();
        QString province     = get_query.value(2).toString();
        QString country      = get_query.value(3).toString();
        dms lat              = dms(get_query.value(4).toString());
        dms lng              = dms(get_query.value(5).toString());
        double TZ            = get_query.value(6).toDouble();
        TimeZoneRule *TZrule = &(Rulebook[get_query.value(7).toString()]);
        double elevation     = get_query.value(8).toDouble();

        // appends city names to list
        geoList.append(new GeoLocation(lng, lat, name, province, country, TZ, TZrule, elevation, true, 4));
    }
    citydb.close();

    // Reading local database
    QSqlDatabase mycitydb = QSqlDatabase::addDatabase("QSQLITE", "mycitydb");
    dbfile = QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("mycitydb.sqlite");

    if (QFile::exists(dbfile))
    {
        mycitydb.setDatabaseName(dbfile);
        if (mycitydb.open())
        {
            QSqlQuery get_query(mycitydb);

            if (!get_query.exec("SELECT * FROM city"))
            {
                qDebug() << Q_FUNC_INFO << get_query.lastError();
                return false;
            }
            while (get_query.next())
            {
                QString name         = get_query.value(1).toString();
                QString province     = get_query.value(2).toString();
                QString country      = get_query.value(3).toString();
                dms lat              = dms(get_query.value(4).toString());
                dms lng              = dms(get_query.value(5).toString());
                double TZ            = get_query.value(6).toDouble();
                TimeZoneRule *TZrule = &(Rulebook[get_query.value(7).toString()]);
                double elevation     = get_query.value(8).toDouble();

                // appends city names to list
                geoList.append(new GeoLocation(lng, lat, name, province, country, TZ, TZrule, elevation, false, 4));
            }
            mycitydb.close();
        }
    }

    return citiesFound;
}

bool KStarsData::readTimeZoneRulebook()
{
    QFile file;

    if (KSUtils::openDataFile(file, "TZrules.dat"))
    {
        QTextStream stream(&file);

        while (!stream.atEnd())
        {
            QString line = stream.readLine().trimmed();
            if (line.length() && !line.startsWith('#')) //ignore commented and blank lines
            {
                QStringList fields = line.split(' ', Qt::SkipEmptyParts);
                QString id         = fields[0];
                QTime stime        = QTime(fields[3].left(fields[3].indexOf(':')).toInt(),
                                           fields[3].mid(fields[3].indexOf(':') + 1, fields[3].length()).toInt());
                QTime rtime        = QTime(fields[6].left(fields[6].indexOf(':')).toInt(),
                                           fields[6].mid(fields[6].indexOf(':') + 1, fields[6].length()).toInt());

                Rulebook[id] = TimeZoneRule(fields[1], fields[2], stime, fields[4], fields[5], rtime);
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool KStarsData::openUrlFile(const QString &urlfile, QFile &file)
{
    //QFile file;
    QString localFile;
    bool fileFound = false;
    QFile localeFile;

    //if ( locale->language() != "en_US" )
    if (QLocale().language() != QLocale::English)
        //localFile = locale->language() + '/' + urlfile;
        localFile = QLocale().languageToString(QLocale().language()) + '/' + urlfile;

    if (!localFile.isEmpty() && KSUtils::openDataFile(file, localFile))
    {
        fileFound = true;
    }
    else
    {
        // Try to load locale file, if not successful, load regular urlfile and then copy it to locale.
        file.setFileName(QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(urlfile));
        if (file.open(QIODevice::ReadOnly))
        {
            //local file found.  Now, if global file has newer timestamp, then merge the two files.
            //First load local file into QStringList
            bool newDataFound(false);
            QStringList urlData;
            QTextStream lStream(&file);
            while (!lStream.atEnd())
                urlData.append(lStream.readLine());

            //Find global file(s) in findAllResources() list.
            QFileInfo fi_local(file.fileName());

            QStringList flist = KSPaths::locateAll(QStandardPaths::AppDataLocation, urlfile);

            for (int i = 0; i < flist.size(); i++)
            {
                if (flist[i] != file.fileName())
                {
                    QFileInfo fi_global(flist[i]);

                    //Is this global file newer than the local file?
                    if (fi_global.lastModified() > fi_local.lastModified())
                    {
                        //Global file has newer timestamp than local.  Add lines in global file that don't already exist in local file.
                        //be smart about this; in some cases the URL is updated but the image is probably the same if its
                        //label string is the same.  So only check strings up to last ":"
                        QFile globalFile(flist[i]);
                        if (globalFile.open(QIODevice::ReadOnly))
                        {
                            QTextStream gStream(&globalFile);
                            while (!gStream.atEnd())
                            {
                                QString line = gStream.readLine();

                                //If global-file line begins with "XXX:" then this line should be removed from the local file.
                                if (line.startsWith(QLatin1String("XXX:")) && urlData.contains(line.mid(4)))
                                {
                                    urlData.removeAt(urlData.indexOf(line.mid(4)));
                                }
                                else
                                {
                                    //does local file contain the current global file line, up to second ':' ?

                                    bool linefound(false);
                                    for (int j = 0; j < urlData.size(); ++j)
                                    {
                                        if (urlData[j].contains(line.left(line.indexOf(':', line.indexOf(':') + 1))))
                                        {
                                            //replace line in urlData with its equivalent in the newer global file.
                                            urlData.replace(j, line);
                                            if (!newDataFound)
                                                newDataFound = true;
                                            linefound = true;
                                            break;
                                        }
                                    }
                                    if (!linefound)
                                    {
                                        urlData.append(line);
                                        if (!newDataFound)
                                            newDataFound = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            file.close();

            //(possibly) write appended local file
            if (newDataFound)
            {
                if (file.open(QIODevice::WriteOnly))
                {
                    QTextStream outStream(&file);
                    for (int i = 0; i < urlData.size(); i++)
                    {
                        outStream << urlData[i] << '\n';
                    }
                    file.close();
                }
            }

            if (file.open(QIODevice::ReadOnly))
                fileFound = true;
        }
        else
        {
            if (KSUtils::openDataFile(file, urlfile))
            {
                if (QLocale().language() != QLocale::English)
                    qDebug() << Q_FUNC_INFO << "No localized URL file; using default English file.";
                // we found urlfile, we need to copy it to locale
                localeFile.setFileName(QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(urlfile));
                if (localeFile.open(QIODevice::WriteOnly))
                {
                    QTextStream readStream(&file);
                    QTextStream writeStream(&localeFile);
                    while (!readStream.atEnd())
                    {
                        QString line = readStream.readLine();
                        if (!line.startsWith(QLatin1String("XXX:"))) //do not write "deleted" lines
                            writeStream << line << '\n';
                    }

                    localeFile.close();
                    file.reset();
                }
                else
                {
                    qDebug() << Q_FUNC_INFO << "Failed to copy default URL file to locale folder, modifying default object links is "
                             "not possible";
                }
                fileFound = true;
            }
        }
    }
    return fileFound;
}

bool KStarsData::readURLData(const QString &urlfile, SkyObjectUserdata::Type type)
{
#ifndef KSTARS_LITE
    if (KStars::Closing)
        return true;
#endif

    QFile file;
    if (!openUrlFile(urlfile, file))
        return false;

    QTextStream stream(&file);
    QMutexLocker _{ &m_user_data_mutex };

    while (!stream.atEnd())
    {
        QString line = stream.readLine();

        //ignore comment lines
        if (!line.startsWith('#'))
        {
#ifndef KSTARS_LITE
            if (KStars::Closing)
            {
                file.close();
                return true;
            }
#endif

            int idx      = line.indexOf(':');
            QString name = line.left(idx);
            if (name == "XXX")
                continue;
            QString sub   = line.mid(idx + 1);
            idx           = sub.indexOf(':');
            QString title = sub.left(idx);
            QString url   = sub.mid(idx + 1);
            // Dirty hack to fix things up for planets

            //            if (name == "Mercury" || name == "Venus" || name == "Mars" || name == "Jupiter" || name == "Saturn" ||
            //                    name == "Uranus" || name == "Neptune" /* || name == "Pluto" */)
            //                o = skyComposite()->findByName(i18n(name.toLocal8Bit().data()));
            //            else

            auto &data_element = m_user_data[name];
            data_element.addLink(title, QUrl{ url }, type);
        }
    }
    file.close();
    return true;
}

// FIXME: Improve the user log system

// Note: It might be very nice to keep the log in plaintext files, for
// portability, human-readability, and greppability. However, it takes
// a lot of time to parse and look up, is very messy from the
// reliability and programming point of view, needs to be parsed at
// start, can become corrupt easily because of a missing bracket...

// An SQLite database is a good compromise. A user can easily view it
// using an SQLite browser. There is no need to read at start-up, one
// can read the log when required. Easy to edit logs / update logs
// etc. Will not become corrupt. Needn't be parsed.

// However, IMHO, it is best to put these kinds of things in separate
// databases, instead of unifying them as a table under the user
// database. This ensures portability and a certain robustness that if
// a user opens it, they cannot incorrectly edit a part of the DB they
// did not intend to edit.

// --asimha 2016 Aug 17

// FIXME: This is a significant contributor to KStars startup time.
bool KStarsData::readUserLog()
{
    QFile file;
    QString fullContents;

    if (!KSUtils::openDataFile(file, "userlog.dat"))
        return false;

    QTextStream stream(&file);

    if (!stream.atEnd())
        fullContents = stream.readAll();

    QMutexLocker _{ &m_user_data_mutex };

    QString buffer(fullContents);
    const QLatin1String logStart("[KSLABEL:"), logEnd("[KSLogEnd]");
    std::size_t currentEntryIndex = 0;
    while (!buffer.isEmpty())
    {
        int startIndex, endIndex;
        QString sub, name, data;

        startIndex = buffer.indexOf(logStart) + logStart.size();
        if (startIndex < 0)
            break;
        currentEntryIndex += startIndex;
        endIndex   = buffer.indexOf(logEnd, startIndex);

        auto malformatError = [&]()
        {
            int res = KMessageBox::warningContinueCancel(
                          nullptr,
                          i18n("The user notes log file %1 is malformatted in the opening of the entry starting at %2. "
                               "KStars can still run without fully reading this file. "
                               "Press Continue to run KStars with whatever partial reading was successful. "
                               "The file may get truncated if KStars writes to the file later. Press Cancel to instead abort now and manually fix the problem. ",
                               file.fileName(), QString::number(currentEntryIndex)),
                          i18n( "Malformed file %1", file.fileName() )
                      );
            if( res != KMessageBox::Continue )
                qApp->exit(1); // FIXME: Why does this not work?
        };

        if (endIndex < 0)
        {
            malformatError();
            break;
        }

        // Read name after KSLABEL identifier
        // Because some object names have [] within them, we have to be careful
        // [...] names are used by SIMBAD and NED to specify paper authors
        // Unbalanced [,] are not allowed in the object name, but are allowed in the notes
        int nameEndIndex = startIndex, openBracketCount = 1;
        while (openBracketCount > 0 && nameEndIndex < endIndex)
        {
            if (buffer[nameEndIndex] == ']')
                --openBracketCount;
            else if (buffer[nameEndIndex] == '[')
                ++openBracketCount;
            ++nameEndIndex;
        }
        if (openBracketCount > 0)
        {
            malformatError();
            break;
        }
        name = buffer.mid(startIndex, (nameEndIndex - 1) - startIndex);

        // Read data and skip new line
        if (buffer[nameEndIndex] == '\n')
            ++nameEndIndex;
        data   = buffer.mid(nameEndIndex, endIndex - nameEndIndex);
        buffer = buffer.mid(endIndex + logEnd.size() + 1);
        currentEntryIndex += (endIndex + logEnd.size() + 1 - startIndex);

        auto &data_element   = m_user_data[name];
        data_element.userLog = data;

    } // end while
    file.close();
    return true;
}

bool KStarsData::readADVTreeData()
{
    QFile file;
    QString Interface;
    QString Name, Link, subName;

    if (!KSUtils::openDataFile(file, "advinterface.dat"))
        return false;

    QTextStream stream(&file);
    QString Line;

    while (!stream.atEnd())
    {
        int Type, interfaceIndex;

        Line = stream.readLine();

        if (Line.startsWith(QLatin1String("[KSLABEL]")))
        {
            Name = Line.mid(9);
            Type = 0;
        }
        else if (Line.startsWith(QLatin1String("[END]")))
            Type = 1;
        else if (Line.startsWith(QLatin1String("[KSINTERFACE]")))
        {
            Interface = Line.mid(13);
            continue;
        }

        else
        {
            int idx = Line.indexOf(':');
            Name    = Line.left(idx);
            Link    = Line.mid(idx + 1);

            // Link is empty, using Interface instead
            if (Link.isEmpty())
            {
                Link           = Interface;
                subName        = Name;
                interfaceIndex = Link.indexOf(QLatin1String("KSINTERFACE"));
                Link.remove(interfaceIndex, 11);
                Link = Link.insert(interfaceIndex, subName.replace(' ', '+'));
            }

            Type = 2;
        }

        ADVTreeData *ADVData = new ADVTreeData;

        ADVData->Name = Name;
        ADVData->Link = Link;
        ADVData->Type = Type;

        ADVtreeList.append(ADVData);
    }

    return true;
}

//There's a lot of code duplication here, but it's not avoidable because
//this function is only called from main.cpp when the user is using
//"dump" mode to produce an image from the command line.  In this mode,
//there is no KStars object, so none of the DBus functions can be called
//directly.
bool KStarsData::executeScript(const QString &scriptname, SkyMap *map)
{
#ifndef KSTARS_LITE
    int cmdCount(0);

    QFile f(scriptname);
    if (!f.open(QIODevice::ReadOnly))
    {
        qDebug() << Q_FUNC_INFO << "Could not open file " << f.fileName();
        return false;
    }

    QTextStream istream(&f);
    while (!istream.atEnd())
    {
        QString line = istream.readLine();
        line.remove("string:");
        line.remove("int32:");
        line.remove("double:");
        line.remove("bool:");

        //find a dbus line and extract the function name and its arguments
        //The function name starts after the last occurrence of "org.kde.kstars."
        //or perhaps "org.kde.kstars.SimClock.".
        if (line.startsWith(QString("dbus-send")))
        {
            QString funcprefix = "org.kde.kstars.SimClock.";
            int i              = line.lastIndexOf(funcprefix);
            if (i >= 0)
            {
                i += funcprefix.length();
            }
            else
            {
                funcprefix = "org.kde.kstars.";
                i          = line.lastIndexOf(funcprefix);
                if (i >= 0)
                {
                    i += funcprefix.length();
                }
            }
            if (i < 0)
            {
                qWarning() << "Could not parse line: " << line;
                return false;
            }

            QStringList fn = line.mid(i).split(' ');

            //DEBUG
            //qDebug() << Q_FUNC_INFO << fn;

            if (fn[0] == "lookTowards" && fn.size() >= 2)
            {
                double az(-1.0);
                QString arg = fn[1].toLower();
                if (arg == "n" || arg == "north")
                    az = 0.0;
                if (arg == "ne" || arg == "northeast")
                    az = 45.0;
                if (arg == "e" || arg == "east")
                    az = 90.0;
                if (arg == "se" || arg == "southeast")
                    az = 135.0;
                if (arg == "s" || arg == "south")
                    az = 180.0;
                if (arg == "sw" || arg == "southwest")
                    az = 225.0;
                if (arg == "w" || arg == "west")
                    az = 270.0;
                if (arg == "nw" || arg == "northwest")
                    az = 335.0;
                if (az >= 0.0)
                {
                    // N.B. unrefract() doesn't matter at 90 degrees
                    map->setFocusAltAz(dms(90.0), map->focus()->az());
                    map->focus()->HorizontalToEquatorial(&LST, geo()->lat());
                    map->setDestination(*map->focus());
                    cmdCount++;
                }

                if (arg == "z" || arg == "zenith")
                {
                    // N.B. unrefract() doesn't matter at 90 degrees
                    map->setFocusAltAz(dms(90.0), map->focus()->az());
                    map->focus()->HorizontalToEquatorial(&LST, geo()->lat());
                    map->setDestination(*map->focus());
                    cmdCount++;
                }

                //try a named object.  The name is everything after fn[0],
                //concatenated with spaces.
                fn.removeAll(fn.first());
                QString objname   = fn.join(" ");
                SkyObject *target = objectNamed(objname);
                if (target)
                {
                    map->setFocus(target);
                    map->focus()->EquatorialToHorizontal(&LST, geo()->lat());
                    map->setDestination(*map->focus());
                    cmdCount++;
                }
            }
            else if (fn[0] == "setRaDec" && fn.size() == 3)
            {
                bool ok(false);
                dms r(0.0), d(0.0);

                ok = r.setFromString(fn[1], false); //assume angle in hours
                if (ok)
                    ok = d.setFromString(fn[2], true); //assume angle in degrees
                if (ok)
                {
                    map->setFocus(r, d);
                    map->focus()->EquatorialToHorizontal(&LST, geo()->lat());
                    cmdCount++;
                }
            }
            else if (fn[0] == "setAltAz" && fn.size() == 3)
            {
                bool ok(false);
                dms az(0.0), alt(0.0);

                ok = alt.setFromString(fn[1]);
                if (ok)
                    ok = az.setFromString(fn[2]);
                if (ok)
                {
                    map->setFocusAltAz(alt, az);
                    map->focus()->HorizontalToEquatorial(&LST, geo()->lat());
                    cmdCount++;
                }
            }
            else if (fn[0] == "loadColorScheme")
            {
                fn.removeAll(fn.first());
                QString csName = fn.join(" ").remove('\"');
                qCDebug(KSTARS) << "Loading Color scheme: " << csName;

                QString filename = csName.toLower().trimmed();
                bool ok(false);

                //Parse default names which don't follow the regular file-naming scheme
                if (csName == i18nc("use default color scheme", "Default Colors"))
                    filename = "classic.colors";
                if (csName == i18nc("use 'star chart' color scheme", "Star Chart"))
                    filename = "chart.colors";
                if (csName == i18nc("use 'night vision' color scheme", "Night Vision"))
                    filename = "night.colors";

                //Try the filename if it ends with ".colors"
                if (filename.endsWith(QLatin1String(".colors")))
                    ok = colorScheme()->load(filename);

                //If that didn't work, try assuming that 'name' is the color scheme name
                //convert it to a filename exactly as ColorScheme::save() does
                if (!ok)
                {
                    if (!filename.isEmpty())
                    {
                        for (int i = 0; i < filename.length(); ++i)
                            if (filename.at(i) == ' ')
                                filename.replace(i, 1, "-");

                        filename = filename.append(".colors");
                        ok       = colorScheme()->load(filename);
                    }

                    if (!ok)
                        qDebug() << Q_FUNC_INFO << QString("Unable to load color scheme named %1. Also tried %2.")
                                 .arg(csName, filename);
                }
            }
            else if (fn[0] == "zoom" && fn.size() == 2)
            {
                bool ok(false);
                double z = fn[1].toDouble(&ok);
                if (ok)
                {
                    if (z > MAXZOOM)
                        z = MAXZOOM;
                    if (z < MINZOOM)
                        z = MINZOOM;
                    Options::setZoomFactor(z);
                    cmdCount++;
                }
            }
            else if (fn[0] == "zoomIn")
            {
                if (Options::zoomFactor() < MAXZOOM)
                {
                    Options::setZoomFactor(Options::zoomFactor() * DZOOM);
                    cmdCount++;
                }
            }
            else if (fn[0] == "zoomOut")
            {
                if (Options::zoomFactor() > MINZOOM)
                {
                    Options::setZoomFactor(Options::zoomFactor() / DZOOM);
                    cmdCount++;
                }
            }
            else if (fn[0] == "defaultZoom")
            {
                Options::setZoomFactor(DEFAULTZOOM);
                cmdCount++;
            }
            else if (fn[0] == "setLocalTime" && fn.size() == 7)
            {
                bool ok(false);
                // min is a macro - use mnt
                int yr(0), mth(0), day(0), hr(0), mnt(0), sec(0);
                yr = fn[1].toInt(&ok);
                if (ok)
                    mth = fn[2].toInt(&ok);
                if (ok)
                    day = fn[3].toInt(&ok);
                if (ok)
                    hr = fn[4].toInt(&ok);
                if (ok)
                    mnt = fn[5].toInt(&ok);
                if (ok)
                    sec = fn[6].toInt(&ok);
                if (ok)
                {
                    changeDateTime(geo()->LTtoUT(KStarsDateTime(QDate(yr, mth, day), QTime(hr, mnt, sec))));
                    cmdCount++;
                }
                else
                {
                    qWarning() << ki18n("Could not set time: %1 / %2 / %3 ; %4:%5:%6")
                               .subs(day)
                               .subs(mth)
                               .subs(yr)
                               .subs(hr)
                               .subs(mnt)
                               .subs(sec)
                               .toString();
                }
            }
            else if (fn[0] == "changeViewOption" && fn.size() == 3)
            {
                bool bOk(false), dOk(false);

                //parse bool value
                bool bVal(false);
                if (fn[2].toLower() == "true")
                {
                    bOk  = true;
                    bVal = true;
                }
                if (fn[2].toLower() == "false")
                {
                    bOk  = true;
                    bVal = false;
                }
                if (fn[2] == "1")
                {
                    bOk  = true;
                    bVal = true;
                }
                if (fn[2] == "0")
                {
                    bOk  = true;
                    bVal = false;
                }

                //parse double value
                double dVal = fn[2].toDouble(&dOk);

                // FIXME: REGRESSION
                //                if ( fn[1] == "FOVName"                ) { Options::setFOVName(       fn[2] ); cmdCount++; }
                //                if ( fn[1] == "FOVSizeX"         && dOk ) { Options::setFOVSizeX( (float)dVal ); cmdCount++; }
                //                if ( fn[1] == "FOVSizeY"         && dOk ) { Options::setFOVSizeY( (float)dVal ); cmdCount++; }
                //                if ( fn[1] == "FOVShape"        && nOk ) { Options::setFOVShape(       nVal ); cmdCount++; }
                //                if ( fn[1] == "FOVColor"               ) { Options::setFOVColor(      fn[2] ); cmdCount++; }
                if (fn[1] == "ShowStars" && bOk)
                {
                    Options::setShowStars(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowCLines" && bOk)
                {
                    Options::setShowCLines(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowCNames" && bOk)
                {
                    Options::setShowCNames(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowMilkyWay" && bOk)
                {
                    Options::setShowMilkyWay(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowEquatorialGrid" && bOk)
                {
                    Options::setShowEquatorialGrid(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowHorizontalGrid" && bOk)
                {
                    Options::setShowHorizontalGrid(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowEquator" && bOk)
                {
                    Options::setShowEquator(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowEcliptic" && bOk)
                {
                    Options::setShowEcliptic(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowHorizon" && bOk)
                {
                    Options::setShowHorizon(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowGround" && bOk)
                {
                    Options::setShowGround(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowSun" && bOk)
                {
                    Options::setShowSun(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowMoon" && bOk)
                {
                    Options::setShowMoon(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowMercury" && bOk)
                {
                    Options::setShowMercury(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowVenus" && bOk)
                {
                    Options::setShowVenus(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowMars" && bOk)
                {
                    Options::setShowMars(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowJupiter" && bOk)
                {
                    Options::setShowJupiter(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowSaturn" && bOk)
                {
                    Options::setShowSaturn(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowUranus" && bOk)
                {
                    Options::setShowUranus(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowNeptune" && bOk)
                {
                    Options::setShowNeptune(bVal);
                    cmdCount++;
                }
                //if ( fn[1] == "ShowPluto"       && bOk ) { Options::setShowPluto(    bVal ); cmdCount++; }
                if (fn[1] == "ShowAsteroids" && bOk)
                {
                    Options::setShowAsteroids(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowComets" && bOk)
                {
                    Options::setShowComets(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowSolarSystem" && bOk)
                {
                    Options::setShowSolarSystem(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowDeepSky" && bOk)
                {
                    Options::setShowDeepSky(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowSupernovae" && bOk)
                {
                    Options::setShowSupernovae(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowStarNames" && bOk)
                {
                    Options::setShowStarNames(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowStarMagnitudes" && bOk)
                {
                    Options::setShowStarMagnitudes(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowAsteroidNames" && bOk)
                {
                    Options::setShowAsteroidNames(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowCometNames" && bOk)
                {
                    Options::setShowCometNames(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowPlanetNames" && bOk)
                {
                    Options::setShowPlanetNames(bVal);
                    cmdCount++;
                }
                if (fn[1] == "ShowPlanetImages" && bOk)
                {
                    Options::setShowPlanetImages(bVal);
                    cmdCount++;
                }

                if (fn[1] == "UseAltAz" && bOk)
                {
                    Options::setUseAltAz(bVal);
                    cmdCount++;
                }
                if (fn[1] == "UseRefraction" && bOk)
                {
                    Options::setUseRefraction(bVal);
                    cmdCount++;
                }
                if (fn[1] == "UseAutoLabel" && bOk)
                {
                    Options::setUseAutoLabel(bVal);
                    cmdCount++;
                }
                if (fn[1] == "UseAutoTrail" && bOk)
                {
                    Options::setUseAutoTrail(bVal);
                    cmdCount++;
                }
                if (fn[1] == "UseAnimatedSlewing" && bOk)
                {
                    Options::setUseAnimatedSlewing(bVal);
                    cmdCount++;
                }
                if (fn[1] == "FadePlanetTrails" && bOk)
                {
                    Options::setFadePlanetTrails(bVal);
                    cmdCount++;
                }
                if (fn[1] == "SlewTimeScale" && dOk)
                {
                    Options::setSlewTimeScale(dVal);
                    cmdCount++;
                }
                if (fn[1] == "ZoomFactor" && dOk)
                {
                    Options::setZoomFactor(dVal);
                    cmdCount++;
                }
                //                if ( fn[1] == "MagLimitDrawStar"     && dOk ) { Options::setMagLimitDrawStar( dVal ); cmdCount++; }
                if (fn[1] == "StarDensity" && dOk)
                {
                    Options::setStarDensity(dVal);
                    cmdCount++;
                }
                //                if ( fn[1] == "MagLimitDrawStarZoomOut" && dOk ) { Options::setMagLimitDrawStarZoomOut( dVal ); cmdCount++; }
                if (fn[1] == "MagLimitDrawDeepSky" && dOk)
                {
                    Options::setMagLimitDrawDeepSky(dVal);
                    cmdCount++;
                }
                if (fn[1] == "MagLimitDrawDeepSkyZoomOut" && dOk)
                {
                    Options::setMagLimitDrawDeepSkyZoomOut(dVal);
                    cmdCount++;
                }
                if (fn[1] == "StarLabelDensity" && dOk)
                {
                    Options::setStarLabelDensity(dVal);
                    cmdCount++;
                }
                if (fn[1] == "MagLimitHideStar" && dOk)
                {
                    Options::setMagLimitHideStar(dVal);
                    cmdCount++;
                }
                if (fn[1] == "MagLimitAsteroid" && dOk)
                {
                    Options::setMagLimitAsteroid(dVal);
                    cmdCount++;
                }
                if (fn[1] == "AsteroidLabelDensity" && dOk)
                {
                    Options::setAsteroidLabelDensity(dVal);
                    cmdCount++;
                }
                if (fn[1] == "MaxRadCometName" && dOk)
                {
                    Options::setMaxRadCometName(dVal);
                    cmdCount++;
                }

                //these three are a "radio group"
                if (fn[1] == "UseLatinConstellationNames" && bOk)
                {
                    Options::setUseLatinConstellNames(true);
                    Options::setUseLocalConstellNames(false);
                    Options::setUseAbbrevConstellNames(false);
                    cmdCount++;
                }
                if (fn[1] == "UseLocalConstellationNames" && bOk)
                {
                    Options::setUseLatinConstellNames(false);
                    Options::setUseLocalConstellNames(true);
                    Options::setUseAbbrevConstellNames(false);
                    cmdCount++;
                }
                if (fn[1] == "UseAbbrevConstellationNames" && bOk)
                {
                    Options::setUseLatinConstellNames(false);
                    Options::setUseLocalConstellNames(false);
                    Options::setUseAbbrevConstellNames(true);
                    cmdCount++;
                }
            }
            else if (fn[0] == "setGeoLocation" && (fn.size() == 3 || fn.size() == 4))
            {
                QString city(fn[1]), province, country(fn[2]);
                province.clear();
                if (fn.size() == 4)
                {
                    province = fn[2];
                    country  = fn[3];
                }

                bool cityFound(false);
                foreach (GeoLocation *loc, geoList)
                {
                    if (loc->translatedName() == city &&
                            (province.isEmpty() || loc->translatedProvince() == province) &&
                            loc->translatedCountry() == country)
                    {
                        cityFound = true;
                        setLocation(*loc);
                        cmdCount++;
                        break;
                    }
                }

                if (!cityFound)
                    qWarning() << i18n("Could not set location named %1, %2, %3", city, province, country);
            }
        }
    } //end while

    if (cmdCount)
        return true;
#else
    Q_UNUSED(map)
    Q_UNUSED(scriptname)
#endif
    return false;
}

#ifndef KSTARS_LITE
void KStarsData::syncFOV()
{
    visibleFOVs.clear();
    // Add visible FOVs
    foreach (FOV *fov, availFOVs)
    {
        if (Options::fOVNames().contains(fov->name()))
            visibleFOVs.append(fov);
    }
    // Remove unavailable FOVs
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    QSet<QString> names = QSet<QString>::fromList(Options::fOVNames());
#else
    const QStringList m_fOVNames = Options::fOVNames();
    QSet<QString> names (m_fOVNames.begin(), m_fOVNames.end());
#endif
    QSet<QString> all;
    foreach (FOV *fov, visibleFOVs)
    {
        all.insert(fov->name());
    }
    Options::setFOVNames(all.intersect(names).values());
}

// FIXME: Why does KStarsData store the Execute instance??? -- asimha
Execute *KStarsData::executeSession()
{
    if (!m_Execute.get())
        m_Execute.reset(new Execute());

    return m_Execute.get();
}

// FIXME: Why does KStarsData store the ImageExporer instance??? KStarsData is supposed to work with no reference to KStars -- asimha
ImageExporter *KStarsData::imageExporter()
{
    if (!m_ImageExporter.get())
        m_ImageExporter.reset(new ImageExporter(KStars::Instance()));

    return m_ImageExporter.get();
}
#endif

std::pair<bool, QString>
KStarsData::addToUserData(const QString &name, const SkyObjectUserdata::LinkData &data)
{
    QMutexLocker _{ &m_user_data_mutex };

    findUserData(name).links[data.type].push_back(data);

    QString entry;
    QFile file;
    const auto isImage = data.type == SkyObjectUserdata::Type::image;

    //Also, update the user's custom image links database
    //check for user's image-links database.  If it doesn't exist, create it.
    file.setFileName(
        KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        (isImage ?
         "image_url.dat" :
         "info_url.dat")); //determine filename in local user KDE directory tree.

    if (!file.open(QIODevice::ReadWrite | QIODevice::Append))
        return { false,
                 isImage ?
                 i18n("Custom image-links file could not be opened.\nLink cannot "
                      "be recorded for future sessions.") :
                 i18n("Custom information-links file could not be opened.\nLink "
                      "cannot be recorded for future sessions.") };
    else
    {
        entry = name + ':' + data.title + ':' + data.url.toString();
        QTextStream stream(&file);
        stream << entry << '\n';
        file.close();
    }

    return { true, {} };
}

std::pair<bool, QString> updateLocalDatabase(SkyObjectUserdata::Type type,
        const QString &search_line,
        const QString &replace_line)
{
    QString TempFileName, file_line;
    QFile URLFile;
    QTemporaryFile TempFile;
    TempFile.setAutoRemove(false);
    TempFile.open();

    bool replace = !replace_line.isEmpty();

    if (search_line.isEmpty())
        return { false, "Invalid update request." };

    TempFileName = TempFile.fileName();

    switch (type)
    {
        // Info Links
        case SkyObjectUserdata::Type::website:
            // Get name for our local info_url file
            URLFile.setFileName(
                KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                "info_url.dat");
            break;

        // Image Links
        case SkyObjectUserdata::Type::image:
            // Get name for our local info_url file
            URLFile.setFileName(
                KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                "image_url.dat");
            break;
    }

    // Copy URL file to temp file
    KIO::file_copy(QUrl::fromLocalFile(URLFile.fileName()),
                   QUrl::fromLocalFile(TempFileName), -1,
                   KIO::Overwrite | KIO::HideProgressInfo);

    if (!URLFile.open(QIODevice::WriteOnly))
    {
        return { false, "Failed to open " + URLFile.fileName() +
                 "KStars cannot save to user database" };
    }

    // Get streams;
    QTextStream temp_stream(&TempFile);
    QTextStream out_stream(&URLFile);

    bool found = false;
    while (!temp_stream.atEnd())
    {
        file_line = temp_stream.readLine();
        // If we find a match, either replace, or remove (by skipping).
        if (file_line == search_line)
        {
            found = true;
            if (replace)
                (out_stream) << replace_line << '\n';
            else
                continue;
        }
        else
            (out_stream) << file_line << '\n';
    }

    // just append it if we haven't found it.
    if (!found && replace)
    {
        out_stream << replace_line << '\n';
    }

    URLFile.close();

    return { true, {} };
}

std::pair<bool, QString> KStarsData::editUserData(const QString &name,
        const unsigned int index,
        const SkyObjectUserdata::LinkData &data)
{
    QMutexLocker _{ &m_user_data_mutex };

    auto &entry = findUserData(name);
    if (index >= entry.links[data.type].size())
        return { false, i18n("Userdata at index %1 does not exist.", index) };

    entry.links[data.type][index] = data;

    QString search_line = name;
    search_line += ':';
    search_line += data.title;
    search_line += ':';
    search_line += data.url.toString();

    QString replace_line = name + ':' + data.title + ':' + data.url.toString();
    return updateLocalDatabase(data.type, search_line, replace_line);
}

std::pair<bool, QString> KStarsData::deleteUserData(const QString &name,
        const unsigned int index,
        SkyObjectUserdata::Type type)
{
    QMutexLocker _{ &m_user_data_mutex };

    auto &linkList = findUserData(name).links[type];
    if (index >= linkList.size())
        return { false, i18n("Userdata at index %1 does not exist.", index) };

    const auto data = linkList[index];
    linkList.erase(linkList.begin() + index);

    QString search_line = name;
    search_line += ':';
    search_line += data.title;
    search_line += ':';
    search_line += data.url.toString();

    QString replace_line = name + ':' + data.title + ':' + data.url.toString();
    return updateLocalDatabase(data.type, search_line, "");
}

std::pair<bool, QString> KStarsData::updateUserLog(const QString &name,
        const QString &newLog)
{
    QMutexLocker _{ &m_user_data_mutex };

    QFile file;
    QString logs; //existing logs

    //Do nothing if:
    //+ new log is the "default" message
    //+ new log is empty
    if (newLog == (i18n("Record here observation logs and/or data on %1.", name)) ||
            newLog.isEmpty())
        return { true, {} };

    // header label
    QString KSLabel = "[KSLABEL:" + name + ']';

    file.setFileName(
        QDir(KSPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(
            "userlog.dat")); //determine filename in local user KDE directory tree.

    if (file.open(QIODevice::ReadOnly))
    {
        QTextStream instream(&file);
        // read all data into memory
        logs = instream.readAll();
        file.close();
    }

    const auto &userLog = m_user_data[name].userLog;

    // Remove old log entry from the logs text
    if (!userLog.isEmpty())
    {
        int startIndex, endIndex;
        QString sub;

        startIndex = logs.indexOf(KSLabel);
        sub        = logs.mid(startIndex);
        endIndex   = sub.indexOf("[KSLogEnd]");

        logs.remove(startIndex, endIndex + 11);
    }

    //append the new log entry to the end of the logs text
    logs.append(KSLabel + '\n' + newLog.trimmed() + "\n[KSLogEnd]\n");

    //Open file for writing
    if (!file.open(QIODevice::WriteOnly))
    {
        return { false, "Cannot write to user log file" };
    }

    //Write new logs text
    QTextStream outstream(&file);
    outstream << logs;

    file.close();

    findUserData(name).userLog = newLog;
    return { true, {} };
};

const SkyObjectUserdata::Data &KStarsData::getUserData(const QString &name)
{
    QMutexLocker _{ &m_user_data_mutex };

    return findUserData(name); // we're consting it
}

SkyObjectUserdata::Data &KStarsData::findUserData(const QString &name)
{
    auto element = m_user_data.find(name);
    if (element != m_user_data.end())
    {
        return element->second;
    }

    // fallback: we did not find it directly, therefore we may try to
    // find a matching object
    const auto *object = m_SkyComposite->findByName(name);
    if (object != nullptr)
    {
        return m_user_data[object->name()];
    }

    return m_user_data[name];
};
