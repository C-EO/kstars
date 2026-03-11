/*
    SPDX-FileCopyrightText: 2016 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QNetworkAccessManager>
#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

class QByteArray;
class QJsonDocument;
class QUrl;

class ProfileInfo;

namespace INDI
{
namespace WebManager
{
bool getWebManagerResponse(QNetworkAccessManager::Operation operation, const QUrl &url, QJsonDocument *reply,
                           QByteArray *data = nullptr);
bool isOnline(const QSharedPointer<ProfileInfo> &pi);
bool checkVersion(const QSharedPointer<ProfileInfo> &pi);
bool syncCustomDrivers(const QSharedPointer<ProfileInfo> &pi);
bool areDriversRunning(const QSharedPointer<ProfileInfo> &pi);
bool startProfile(const QSharedPointer<ProfileInfo> &pi);
bool stopProfile(const QSharedPointer<ProfileInfo> &pi);
bool restartDriver(const QSharedPointer<ProfileInfo> &pi, const QString &label);
bool getProfiles(const QSharedPointer<ProfileInfo> &pi, QJsonArray &profiles);
bool getProfileInfo(const QSharedPointer<ProfileInfo> &pi, const QString &profileName, QJsonObject &info);
bool getProfileDriverLabels(const QSharedPointer<ProfileInfo> &pi, const QString &profileName, QJsonArray &labels);
/**
 * @brief deleteProfile Delete a profile from the INDI Web Manager.
 * @param pi Profile used to reach the Web Manager (provides host and port).
 * @param profileName Name of the profile to delete.
 * @return True on success.
 */
bool deleteProfile(const QSharedPointer<ProfileInfo> &pi, const QString &profileName);
}

namespace AsyncWebManager
{
QFuture<bool> isOnline(const QSharedPointer<ProfileInfo> &pi);
QFuture<bool> isStellarMate(const QSharedPointer<ProfileInfo> &pi);
QFuture<bool> syncCustomDrivers(const QSharedPointer<ProfileInfo> &pi);
QFuture<bool> areDriversRunning(const QSharedPointer<ProfileInfo> &pi);
QFuture<bool> startProfile(const QSharedPointer<ProfileInfo> &pi);
QFuture<bool> stopProfile(const QSharedPointer<ProfileInfo> &pi);
}

}
