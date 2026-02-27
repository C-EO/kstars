/*
    SPDX-FileCopyrightText: 2026 John Evans <john.e.evans.email@googlemail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "fitsstackwebcast.h"
#include "qrcodegen.hpp"
#include <KLocalizedString>
#include <fits_debug.h>
#include "kspaths.h"
#include <Options.h>

#include <QBuffer>
#include <QPixmap>
#include <QIcon>
#include <QNetworkInterface>
#include <QPainter>
#include <QDateTime>
#include <QFont>
#include <QDir>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>
#include <QFileDialog>
#include <QTimer>
#include <QCloseEvent>

LiveStackWebcast::LiveStackWebcast(QWidget *parent) : QWidget(parent), ui(new Ui::FitsStackWebcast)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);
    setWindowTitle(i18n("LiveStack Webcast Controller"));

    // Initial Data population
    ui->titleEntry->setText(getSessionTitle());
    ui->logoLPathDisplay->setText(getLogoLPath());
    ui->logoRPathDisplay->setText(getLogoRPath());
    ui->port->setValue(getPort());
    ui->throttle->setValue(getThrottle() / 1000);
    ui->subscribersTable->setVisible(false);


    // Connections (Capture 'dial' to manage window lifetime)
    connect(ui->logoLBtn, &QPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, i18n("Select LHS Logo"), QDir::homePath(), "Images (*.png *.jpg)");
        if (!path.isEmpty())
        {
            setLogoLPath(path);
            ui->logoLPathDisplay->setText(path);
        }
    });

    connect(ui->logoRBtn, &QPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, i18n("Select RHS Logo"), QDir::homePath(), "Images (*.png *.jpg)");
        if (!path.isEmpty())
        {
            setLogoRPath(path);
            ui->logoRPathDisplay->setText(path);
        }
    });

    connect(ui->throttle, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int throttleS)
    {
        setThrottle(throttleS * 1000);
        qCDebug(KSTARS_FITS) << "Live Stack Webcast throttle updated to" << throttleS << "seconds.";
    });

    connect(ui->startBtn, &QPushButton::clicked, this, [this]()
    {
        setSessionTitle(ui->titleEntry->text());
        setLogoLPath(ui->logoLPathDisplay->text());
        setLogoRPath(ui->logoRPathDisplay->text());
        setPort(ui->port->value());

        if (!listen(ui->port->value()))
        {
            ui->urlLabel->setText(i18n("Failed to start: Port %1 may be in use.", ui->port->value()));
            resetSession();
        }
        updateUI();
    });

    connect(ui->stopBtn, &QPushButton::clicked, this, [this]()
    {
        stop();
        updateUI();
    });

    connect(ui->resetBtn, &QPushButton::clicked, this, [this]()
    {
        resetSession();
        updateUI();
    });

    connect(ui->subscribersBtn, &QPushButton::clicked, this, [this]()
    {
        const bool isVisible = ui->subscribersTable->isVisible();
        ui->subscribersTable->setVisible(!isVisible);
    });

    connect(ui->buttonBox->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &LiveStackWebcast::hide);

    // Setup the Refresh Timer
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &LiveStackWebcast::updateUI);
    m_refreshTimer->start(3000);

    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &LiveStackWebcast::handleNewConnection);

    updateUI();
}

LiveStackWebcast::~LiveStackWebcast()
{
    stop();
    delete ui;
}

bool LiveStackWebcast::listen(uint16_t port)
{
    // If we're already listening on this port, do nothing
    if (m_tcpServer->isListening() && m_port == port)
        return true;

    // Otherwise, stop and restart on the new port
    stop();

    if (m_tcpServer->listen(QHostAddress::Any, port))
    {
        m_port = port;
        qCDebug(KSTARS_FITS) << "LiveStack Webcast listening on port" << port;
        return true;
    }

    qCDebug(KSTARS_FITS) << "LiveStack Webcast failed to start on port" << port;
    return false;
}

void LiveStackWebcast::stop()
{
    if (m_tcpServer && m_tcpServer->isListening())
    {
        m_tcpServer->close();

        // Clean up active subscribers
        for (auto socket : m_subscribedClients.keys())
            if (socket) socket->disconnectFromHost();
    }

    m_galleryCache.clear();
    m_subscribedClients.clear();
    m_slugToPath.clear();
    m_lastUpdateTimes.clear();
    m_metaCache.clear();
    m_lobbySubscribers.clear();
}

bool LiveStackWebcast::isListening() const
{
    return m_tcpServer && m_tcpServer->isListening();
}

void LiveStackWebcast::setStackingActive(const bool active, const QString &newPath)
{
    m_isStacking = active;

    if (active)
        m_activePath = newPath;

    broadcastStatus();
}

void LiveStackWebcast::processIncomingStack(const QString &sourcePath, const LiveStackMetadata &meta,
        const QImage &image)
{
    SigPipeBlocker blocker;

    // Throttle check
    if (m_lastBroadcastTimer.isValid() && m_lastBroadcastTimer.elapsed() < m_throttleMs)
        return;

    m_lastBroadcastTimer.restart();

    QString cleanPath = QDir::cleanPath(sourcePath);
    m_activePath = cleanPath;

    // Prepare JSON metadata for SSE broadcast
    m_metaCache[cleanPath] = meta;
    QJsonObject root = getHUDData(cleanPath);
    root["type"] = "metadata";
    root["slug"] = getSlugForPath(cleanPath);
    root["active"] = true;
    QJsonDocument doc(root);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

    // Broadcast MJPEG Frame
    broadcastImage(cleanPath, image);

    // Cache JPG for Lobby/Refresh
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (image.save(&buffer, "JPEG", 85))
        m_galleryCache[cleanPath] = bytes;

    // Push Metadata via SSE (HUD)
    for (int i = m_lobbySubscribers.size() - 1; i >= 0; --i)
    {
        QPointer<QTcpSocket> socket = m_lobbySubscribers.at(i);

        if (!socket || !socket->isOpen() || socket->state() != QAbstractSocket::ConnectedState)
        {
            m_lobbySubscribers.removeAt(i);
            continue;
        }

        // Prepare the payload
        QByteArray payload = "data: " + jsonBytes + "\n\n";

        if (socket->isWritable())
        {
            if (socket->write(payload) == -1)
            {
                socket->abort();
                m_lobbySubscribers.removeAt(i);
            }
        }
    }
}

void LiveStackWebcast::resetSession()
{
    QMutableMapIterator<QTcpSocket *, QString> it(m_subscribedClients);
    while (it.hasNext())
    {
        it.next();
        it.key()->disconnectFromHost();
    }
    m_galleryCache.clear();
    m_subscribedClients.clear();
    m_slugToPath.clear();
    m_lastUpdateTimes.clear();
    m_metaCache.clear();
    m_lobbySubscribers.clear();
}

int LiveStackWebcast::getPort()
{
    if (m_port < 0)
        m_port = Options::fitsLSWebcastPort();
    return m_port;
}

void LiveStackWebcast::setPort(int port)
{
    m_port = port;
    Options::setFitsLSWebcastThrottle(port);
}

int LiveStackWebcast::getThrottle()
{
    if (m_throttleMs < 0)
        m_throttleMs = Options::fitsLSWebcastThrottle();
    return m_throttleMs;
}

void LiveStackWebcast::setThrottle(const int ms)
{
    m_throttleMs = ms;
    Options::setFitsLSWebcastThrottle(ms);
}

QString LiveStackWebcast::getSessionTitle()
{
    if (m_sessionTitle.isEmpty())
        m_sessionTitle = Options::fitsLSWebcastTitle();
    return m_sessionTitle;
}

void LiveStackWebcast::setSessionTitle(const QString &title)
{
    m_sessionTitle = title;
    Options::setFitsLSWebcastTitle(title);
}

QString LiveStackWebcast::getLogoLPath()
{
    if (m_logoLPath.isEmpty())
        m_logoLPath = Options::fitsLSWebcastLogoL();
    return m_logoLPath;
}

void LiveStackWebcast::setLogoLPath(const QString &path)
{
    m_logoLPath = path;
    Options::setFitsLSWebcastLogoL(path);
}

void LiveStackWebcast::setLogoRPath(const QString &path)
{
    m_logoRPath = path;
    Options::setFitsLSWebcastLogoR(path);
}

QString LiveStackWebcast::getLogoRPath()
{
    if (m_logoRPath.isEmpty())
        m_logoRPath = Options::fitsLSWebcastLogoR();
    return m_logoRPath;
}

QString LiveStackWebcast::getLocalURL() const
{
    // Try all network interfaces
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces())
    {
        for (const QNetworkAddressEntry &entry : iface.addressEntries())
        {
            QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            {
                // Return first valid LAN IPv4
                return QString("http://%1:%2").arg(addr.toString()).arg(m_port);
            }
        }
    }

    // Fall back to localhost if really nothing found
    return QString("http://127.0.0.1:%1").arg(m_port);
}

QPixmap LiveStackWebcast::generateQRCodePixmap()
{
    QString url = getLocalURL();
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.toUtf8().constData(), qrcodegen::QrCode::Ecc::LOW);

    int size = qr.getSize();
    int scale = 10;
    QPixmap pixmap((size + 2) * scale, (size + 2) * scale);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setBrush(Qt::black);
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            if (qr.getModule(x, y)) painter.drawRect((x + 1) * scale, (y + 1) * scale, scale, scale);
        }
    }
    return pixmap;
}

QList<ConnectedViewer> LiveStackWebcast::getActiveViewers()
{
    QMap<QString, QString> users; // IP -> Activity

    // Check Lobby Viewers
    for (const QPointer<QTcpSocket> &s : m_lobbySubscribers)
    {
        if (s && s->state() == QAbstractSocket::ConnectedState)
        {
            QString ip = s->peerAddress().toString().remove("::ffff:");
            if (!users.contains(ip))
                users[ip] = i18n("Connected");
        }
    }

    QList<ConnectedViewer> result;
    // 3. Prepare the result, handle name resolution, and include device type
    for (auto it = users.begin(); it != users.end(); ++it)
    {
        QString ip = it.key();

        // Handle Reverse DNS lookup
        if (!m_resolvedNames.contains(ip))
        {
            m_resolvedNames[ip] = i18n("Resolving...");
            resolveName(ip);
        }

        QString device = m_deviceTypeCache.value(ip, i18n("Unknown Device"));

        // Struct order: {ip, name, deviceType, activity}
        result.append({ip, m_resolvedNames[ip], device, it.value()});
    }
    return result;
}

void LiveStackWebcast::closeEvent(QCloseEvent *event)
{
    // Ignore the close event so the object isn't destroyed
    event->ignore();

    // Hide the window instead
    this->hide();
}

void LiveStackWebcast::updateUI()
{
    if (!ui)
        return;

    const bool running = isListening();

    ui->startBtn->setEnabled(!running);
    ui->stopBtn->setEnabled(running);
    ui->titleEntry->setEnabled(!running);
    ui->logoLPathDisplay->setEnabled(!running);
    ui->logoLBtn->setEnabled(!running);
    ui->logoRPathDisplay->setEnabled(!running);
    ui->logoRBtn->setEnabled(!running);
    ui->port->setEnabled(!running);

    if (running)
    {
        // Populate the viewer table
        if (ui->subscribersTable->isVisible())
        {
            auto viewers = getActiveViewers();
            ui->subscribersTable->setRowCount(viewers.size());
            for (int i = 0; i < viewers.size(); ++i)
            {
                ui->subscribersTable->setItem(i, 0, new QTableWidgetItem(viewers[i].name));
                ui->subscribersTable->setItem(i, 1, new QTableWidgetItem(viewers[i].ip));
                ui->subscribersTable->setItem(i, 2, new QTableWidgetItem(viewers[i].deviceType));
                ui->subscribersTable->setItem(i, 3, new QTableWidgetItem(viewers[i].activity));
            }
        }

        ui->qrLabel->setPixmap(generateQRCodePixmap().scaled(250, 250, Qt::KeepAspectRatio));
        ui->urlLabel->setText(QString("<a href='%1' style='color: #f22;'>%1</a>").arg(getLocalURL()));
    }
    else
    {
        ui->qrLabel->setText(QString("<h3>%1</h3>").arg(i18n("Webcast Stopped")));
        ui->urlLabel->clear();
    }
};

void LiveStackWebcast::handleNewConnection()
{
    while (m_tcpServer->hasPendingConnections())
    {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        if (!socket)
            continue;

        // Ensure the socket is cleaned up when the client leaves
        connect(socket, &QTcpSocket::disconnected, this, &LiveStackWebcast::clientDisconnected);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

        // Process incoming HTTP requests
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]()
        {
            processRequest(socket);
        });
    }
}

void LiveStackWebcast::clientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket)
    {
        // Remove from general list
        m_subscribedClients.remove(socket);

        // Remove from Lobby list
        // Note: QSet<QPointer<QTcpSocket>> or QList handles this well
        m_lobbySubscribers.removeOne(socket);
    }
}

void LiveStackWebcast::processRequest(QTcpSocket *socket)
{
    SigPipeBlocker blocker;

    if (!socket || !socket->canReadLine())
        return;

    QByteArray requestLine = socket->readLine();
    QString header = QString::fromUtf8(requestLine).trimmed();

    qCDebug(KSTARS_FITS) << "HTTP REQUEST:" << header;

    // Read remaining headers to find User-Agent
    QString ip = socket->peerAddress().toString().remove("::ffff:");
    while (socket->canReadLine())
    {
        QByteArray lineData = socket->readLine();
        QString line = QString::fromUtf8(lineData).trimmed();

        // Empty line means end of headers
        if (line.isEmpty())
            break;

        if (line.startsWith("User-Agent:", Qt::CaseInsensitive))
        {
            QString ua = line.mid(11).trimmed();
            QString os = i18n("Unknown OS");
            QString browser = i18n("Unknown Browser");

            // OS Detection
            if (ua.contains("iPhone"))
            {
                os = "iPhone";
            }
            else if (ua.contains("Android"))
            {
                os = "Android";
            }
            else if (ua.contains("iPad"))
            {
                os = "iPad";
            }
            else if (ua.contains("Macintosh"))
            {
                os = "macOS";
            }
            else if (ua.contains("Windows"))
            {
                os = "Windows";
            }
            else if (ua.contains("Linux"))
            {
                os = "Linux";
            }

            // Browser Detection
            if (ua.contains("OPR/") || ua.contains("Opera"))
            {
                browser = "Opera";
            }
            else if (ua.contains("Edg/"))
            {
                browser = "Edge";
            }
            else if (ua.contains("Chrome/"))
            {
                browser = "Chrome";
            }
            else if (ua.contains("Firefox/"))
            {
                browser = "Firefox";
            }
            else if (ua.contains("Safari/") && !ua.contains("Chrome"))
            {
                browser = "Safari";
            }

            m_deviceTypeCache[ip] = QString("%1 (%2)").arg(os, browser);
        }
    }

    const bool isStream = header.startsWith("GET /stream") || header.startsWith("GET /lobby-push");

    // Prevent double-processing of standard requests
    if (!isStream && socket->property("handled").toBool())
        return;

    if (!isStream)
        socket->setProperty("handled", true);

    // ---- DASHBOARD ----
    if (header.startsWith("GET /dashboard"))
    {
        sendDashboard(socket, header);
        return;
    }

    // ---- MJPEG STREAM ----
    if (header.startsWith("GET /stream"))
    {
        QString urlLine = header.section(' ', 1, 1);
        QString slug = urlLine.section("id=", 1, 1).section('&', 0, 0);

        QString path = m_slugToPath.value(slug);
        if (path.isEmpty() || slug == "active")
            path = m_activePath;

        QImage firstImg;
        if (!path.isEmpty() && m_galleryCache.contains(path))
            firstImg.loadFromData(m_galleryCache.value(path));
        else
        {
            firstImg = QImage(640, 480, QImage::Format_RGB32);
            firstImg.fill(Qt::black);
        }

        QByteArray imgData;
        {
            QBuffer buffer(&imgData);
            buffer.open(QIODevice::WriteOnly);
            firstImg.save(&buffer, "JPEG", 80);
        }

        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary=kstars_frame\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: keep-alive\r\n\r\n");

        socket->write("--kstars_frame\r\n");
        socket->write("Content-Type: image/jpeg\r\n");
        socket->write("Content-Length: " + QByteArray::number(imgData.size()) + "\r\n\r\n");
        socket->write(imgData);
        socket->write("\r\n");
        socket->disconnectFromHost();
        m_subscribedClients[socket] = path;
        return;
    }

    // ---- THUMBNAILS ----
    if (header.startsWith("GET /thumb/"))
    {
        QString urlPath = header.section(' ', 1, 1);
        QString slug = urlPath.section("/thumb/", 1, 1).section('?', 0, 0);
        QString path = m_slugToPath.value(slug);

        if (path.isEmpty() && slug == "active")
            path = m_activePath;

        sendGalleryImage(socket, path);
        return;
    }

    // ---- LOBBY PUSH (SSE) ----
    if (header.startsWith("GET /lobby-push"))
    {
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: keep-alive\r\n\r\n");

        m_lobbySubscribers.append(QPointer<QTcpSocket>(socket));

        // Send initial state immediately so the UI is correct on load
        QJsonObject obj = getHUDData(m_activePath);
        obj["type"] = "stacking";
        obj["active"] = m_isStacking;
        obj["slug"] = getSlugForPath(m_activePath);

        QByteArray payload = "data: " + QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n\n";
        socket->write(payload);
        return;
    }

    // ---- LOGO IMAGE ----
    if (header.startsWith("GET /logoL"))
    {
        sendImageFile(socket, m_logoLPath);
        return;
    }

    if (header.startsWith("GET /logoR"))
    {
        sendImageFile(socket, m_logoRPath);
        return;
    }

    // ---- KSTARS ICON ----
    if (header.startsWith("GET /icon"))
    {
        QIcon kstarsIcon = QIcon::fromTheme("kstars");
        if (kstarsIcon.isNull())
            sendImageFile(socket, "/usr/share/icons/hicolor/128x128/apps/kstars.png");
        else
        {
            QPixmap pix = kstarsIcon.pixmap(128, 128);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            pix.save(&buffer, "PNG");

            socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n\r\n");
            socket->write(bytes);
            socket->disconnectFromHost();
        }
        return;
    }

    // ---- FALLBACK: LOBBY PAGE ----
    sendLobby(socket);
}

void LiveStackWebcast::sendGalleryImage(QTcpSocket *socket, const QString &path)
{
    SigPipeBlocker blocker;
    QImage img;

    // Try cache first (this is the NORMAL path)
    if (m_galleryCache.contains(path))
        img.loadFromData(m_galleryCache.value(path));

    // Send something
    if (img.isNull())
    {
        img = QImage(640, 360, QImage::Format_RGB32);
        img.fill(Qt::black);

        QPainter p(&img);
        p.setPen(Qt::red);
        p.setFont(QFont("Sans", 24, QFont::Bold));
        p.drawText(img.rect(), Qt::AlignCenter, "Awaiting Image");
    }

    // Encode as JPEG
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 80);

    // Send HTTP response
    socket->write("HTTP/1.1 200 OK\r\n"
                  "Content-Type: image/jpeg\r\n"
                  "Cache-Control: no-cache\r\n"
                  "Content-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n");

    socket->write(jpeg);
    socket->disconnectFromHost();
}

void LiveStackWebcast::sendDashboard(QTcpSocket *socket, const QString &header)
{
    SigPipeBlocker blocker;
    QString slug;
    const QString query = header.section('?', 1).section(' ', 0, 0);
    const QStringList parts = query.split('&');
    for (const QString &p : parts)
        if (p.startsWith("id="))
            slug = p.mid(3);

    if (slug.isEmpty())
        slug = getSlugForPath(m_activePath);

    const QString title = getSessionTitle();
    QString path = m_slugToPath.value(slug);
    QJsonObject hudData = getHUDData(path);

    // Safety: Escape backslashes then quotes
    QString hudJson = QJsonDocument(hudData).toJson(QJsonDocument::Compact);
    hudJson.replace("\\", "\\\\").replace("\"", "\\\"");

    QString logoLImg = m_logoLPath.isEmpty() ? "" :
                       QString("<img src='/logoL' style='height:50px; width:auto; display:block;'>");
    QString logoRImg = m_logoRPath.isEmpty() ? "" :
                       QString("<img src='/logoR' style='height:50px; width:auto; display:block;'>");

    QString html = QString(R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <meta name="apple-mobile-web-app-title" content="KStars LiveStacker">
    <link rel="apple-touch-icon" href="/icon">

    <style>
        :root { --accent:#f22; --bg:#000; --panel:#111; --text:#eee; --btn-text:#fff; --footer-text:#666; --brand-color:#bbb; --hud-bg:rgba(15,15,15,0.4); --hud-text:#eee; }
        body.night-vision { --text:#f44; --accent:#b00; --panel:#050000; --btn-text:#f00; --footer-text:#700; --brand-color:#800; --hud-bg:rgba(20,0,0,0.5); --hud-text:#f00; }

        body {
            margin:0; background:#000; font-family:sans-serif; color:var(--text);
            display:flex; flex-direction:column; height:100vh; height:100svh;
            overflow:hidden; box-sizing: border-box;
            padding-left: env(safe-area-inset-left); padding-right: env(safe-area-inset-right);
        }

        header {
            display: flex; align-items: flex-end; padding-bottom: 10px;
            flex-shrink: 0; min-height: 70px; background: #111; border-bottom: 1px solid #333;
            display: flex; align-items: center; justify-content: space-between;
            padding: 5px 15px; padding-top: env(safe-area-inset-top); box-sizing: border-box;
        }

        .header-left, .header-right { display: flex; align-items: center; flex-shrink: 0; }
        .session-title { font-weight: bold; color: var(--accent); font-size: 1.2em; text-align: center; flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding: 0 10px; }

        button { background:#222; color:var(--btn-text); border:1px solid #444; padding:8px 16px; cursor:pointer; border-radius:4px; font-weight:bold; height:40px; margin-left:10px; }
        body.night-vision button { border-color:#500; background:#100; }

        main { flex:1; position:relative; display:flex; justify-content:center; align-items:center; background:#000; overflow:hidden; }
        #main-image { max-width:100%; max-height:100%; object-fit:contain; }

        /* HUD STYLING */
        .hud-overlay-box {
            position:absolute;
            top: calc(20px + env(safe-area-inset-top));
            left: calc(20px + env(safe-area-inset-left));
            z-index:100;
            background:var(--hud-bg);
            padding:20px;
            border-left:4px solid var(--accent);
            border-radius:6px;
            pointer-events:none;
            backdrop-filter: blur(4px);
        }
        .hud-table td { padding:4px 12px; color:var(--hud-text); font-family:'Consolas',monospace; font-size:1.1em; }
        .hud-label { color:var(--accent); font-weight:bold; text-transform:uppercase; font-size:0.8em; }

        /* FOOTER STYLING */
        footer { background:var(--panel); padding:15px; border-top:1px solid #222; text-align:center; display:flex; align-items:center; justify-content:center; gap:10px; flex-shrink:0; padding-bottom: calc(15px + env(safe-area-inset-bottom)); }
        .powered-by { text-transform:uppercase; font-size:0.75em; letter-spacing:1px; color:var(--footer-text); font-weight:bold; }
        .footer-icon { height:22px; width:auto; opacity:0.8; }
        .footer-brand { color:var(--brand-color); font-size:0.9em; }

        /* MOBILE SCALING FIXES */
        @media (max-width: 600px) {
            header { padding: 5px; min-height: 60px; }
            .session-title { font-size: 1.0em; }
            button { padding: 6px 10px; height: 34px; font-size: 0.75em; margin-left: 4px; }
            .header-left img, .header-right img { height: 35px !important; }

            /* Scale the HUD down for phones without removing elements */
            .hud-overlay-box { top: calc(70px + env(safe-area-inset-top)); left: 10px; }
            .hud-table td { padding: 2px 6px; font-size: 0.8em; }
            .hud-label { font-size: 0.6em; }
        }
    </style>
</head>
<body>
    <header>
        <div class="header-left">%1</div>
        <div class="session-title">%2</div>
        <div class="header-right">
            %3
            <button id="hud-btn">HUD</button>
            <button id="red-btn">NIGHT MODE</button>
            <button onclick="location.href='/'">LOBBY</button>
        </div>
    </header>

    <main>
        <img id="main-image" src="/stream?id=%4">
        <div class="hud-overlay-box" id="hud-box">
            <table class="hud-table">
                <tr><td class="hud-label">Object</td><td id="d-name">--</td></tr>
                <tr><td class="hud-label">ID</td><td id="d-id">--</td></tr>
                <tr><td class="hud-label">Type</td><td id="d-class">--</td></tr>
                <tr><td class="hud-label">Magnitude</td><td id="d-mag">--</td></tr>
                <tr><td class="hud-label">Constellation</td><td id="d-const">--</td></tr>
                <tr><td class="hud-label">Stack</td><td id="d-stack">--</td></tr>
            </table>
        </div>
    </main>

    <footer>
        <span class="powered-by">Powered by</span>
        <img src="/icon" class="footer-icon">
        <span class="footer-brand">KStars <strong>LiveStacker</strong></span>
    </footer>

    <script>
        const hb = document.getElementById('hud-box');
        const updateHUD = (d) => {
            if (!d || d.slug !== "%4") return;
            document.getElementById('d-name').textContent = d.longName || d.target;
            document.getElementById('d-id').textContent = d.target + (d.name2 ? " / " + d.name2 : "");
            document.getElementById('d-class').textContent = d.typeName || "N/A";
            document.getElementById('d-mag').textContent = d.magnitude || "N/A";
            document.getElementById('d-const').textContent = d.constellation || "N/A";
            const mins = (parseFloat(d.totalIntegration || 0) / 60).toFixed(1);
            document.getElementById('d-stack').textContent = `${d.subCount || 0} x ${d.exposureTime || 0}s (${mins}m)`;
        };

        try { updateHUD(JSON.parse("%5")); } catch(e) {}

        /* HUD LOGIC: DEFAULT TO ON */
        let hudPref = localStorage.getItem('hv');
        if (hudPref === null) hudPref = 'true'; // Force ON for new visitors
        hb.style.display = (hudPref === 'true') ? 'block' : 'none';

        document.getElementById('hud-btn').onclick = () => {
            const isNowVisible = hb.style.display === 'none';
            hb.style.display = isNowVisible ? 'block' : 'none';
            localStorage.setItem('hv', isNowVisible ? 'true' : 'false');
        };

        /* NIGHT MODE LOGIC */
        const rb = document.getElementById('red-btn');
        if (localStorage.getItem('nv') === 'true') document.body.classList.add('night-vision');
        rb.onclick = () => {
            const isNight = document.body.classList.toggle('night-vision');
            localStorage.setItem('nv', isNight ? 'true' : 'false');
        };

        const source = new EventSource("/lobby-push");
        source.onmessage = (e) => { try { updateHUD(JSON.parse(e.data)); } catch(err) {} };
    </script>
</body>
</html>)html")
                   .arg(logoLImg)
                   .arg(title.isEmpty() ? "Live Session" : title)
                   .arg(logoRImg)
                   .arg(slug)
                   .arg(hudJson);

    QByteArray data = html.toUtf8();
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + QByteArray::number(
                      data.size()) + "\r\nConnection: close\r\n\r\n" + data);
}

void LiveStackWebcast::sendImageFile(QTcpSocket *socket, const QString &filePath)
{
    SigPipeBlocker blocker;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        // Send a tiny transparent 1x1 pixel if the file is missing
        // to prevent the "broken image" icon from ruining the UI
        socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    QByteArray data = file.readAll();
    QString contentType = "image/png";
    if (filePath.endsWith(".jpg", Qt::CaseInsensitive) || filePath.endsWith(".jpeg", Qt::CaseInsensitive))
        contentType = "image/jpeg";

    socket->write("HTTP/1.1 200 OK\r\n");
    socket->write("Content-Type: " + contentType.toUtf8() + "\r\n");
    socket->write("Content-Length: " + QByteArray::number(data.size()) + "\r\n");
    socket->write("Cache-Control: public, max-age=3600\r\n");
    socket->write("Connection: close\r\n\r\n");
    socket->write(data);
    socket->disconnectFromHost();
}

void LiveStackWebcast::sendLobby(QTcpSocket *socket)
{
    SigPipeBlocker blocker;
    const QString title = getSessionTitle();
    QString activeSlug = getSlugForPath(m_activePath);

    QString logoLImg = m_logoLPath.isEmpty() ? "" :
                       QString("<img src='/logoL' style='height:50px; width:auto; display:block;'>");
    QString logoRImg = m_logoRPath.isEmpty() ? "" :
                       QString("<img src='/logoR' style='height:50px; width:auto; display:block; margin-right:15px;'>");

    QString galleryItems;
    QStringList slugs = m_slugToPath.keys();
    for (int i = slugs.size() - 1; i >= 0; --i)
    {
        QString s = slugs.at(i);
        QString name = m_metaCache.value(m_slugToPath.value(s)).targetName;
        if (name.isEmpty())
            name = "Target";

        galleryItems += QString(R"html(
            <a href="/dashboard?id=%1" class="gallery-card" id="card-%1">
                <div class="gallery-thumb-container"><img src="/thumb/%1" id="img-%1" loading="lazy"></div>
                <div class="gallery-name">%2</div>
            </a>)html").arg(s, name);
    }

    QString html = QString(R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">

    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <meta name="apple-mobile-web-app-title" content="KStars LiveStacker">
    <link rel="apple-touch-icon" href="/icon">

    <style>
        :root { --accent:#f22; --bg:#000; --panel:#111; --text:#eee; --btn-text:#fff; --footer-text:#666; --brand-color:#bbb; }
        body.night-vision { --text:#f44; --accent:#b00; --panel:#050000; --btn-text:#f00; --footer-text:#700; --brand-color:#800; }

        body {
            margin:0;
            background:var(--bg);
            font-family:sans-serif;
            color:var(--text);
            display:flex;
            flex-direction:column;
            /* Fix for iOS toolbars */
            height: 100vh;
            height: 100svh;
            /* Safe area for Notch and Home Indicator */
            padding: env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);
            overflow:hidden;
            box-sizing: border-box;
        }

        header
        {
            flex-shrink: 0;
            min-height: 80px;
            background: #111;
            border-bottom: 1px solid #333;
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 20px;
            box-sizing: border-box;
            flex-wrap: wrap;
        }

        .session-title
        {
            font-weight: bold;
            color: var(--accent);
            font-size: 1.4em;
            text-align: center;
            flex: 1;
            padding: 0 10px;
        }

        @media (max-width: 480px)
        {
            .session-title { font-size: 1.1em; }
            header { padding: 5px 10px; }
        }

        main { flex:1; overflow-y:auto; padding:30px; -webkit-overflow-scrolling: touch; }

        button { background:#222; color:var(--btn-text); border:1px solid #444; padding:8px 16px; cursor:pointer; border-radius:4px; font-weight:bold; height:40px; }
        body.night-vision button { border-color: #500; background: #100; }

        #live-hero { margin-bottom:50px; position:relative; }
        .hero-link { display:block; border:2px solid var(--accent); border-radius:12px; overflow:hidden; max-width:900px; margin:0 auto; background:#000; position:relative; text-decoration:none; }
        .hero-thumb-wrapper { width:100%; aspect-ratio:16/9; display:flex; align-items:center; justify-content:center; }
        #hero-img { max-width:100%; max-height:100%; object-fit:contain; }

        .live-badge { position:absolute; top:15px; left:15px; z-index:10; padding:6px 12px; border-radius:4px; font-family:'Consolas',monospace; font-size:0.9em; font-weight:bold; background:var(--accent); color:#fff; animation:pulse 2s infinite; }
        @keyframes pulse { 0%, 100% { opacity:1; } 50% { opacity:0.4; } }

        #gallery-grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(200px, 1fr)); gap:25px; }
        .gallery-card { background:#050505; border:1px solid #333; border-radius:8px; overflow:hidden; text-decoration:none; transition:transform 0.2s, border-color 0.2s; }
        .gallery-card:hover { transform:translateY(-5px); border-color:var(--accent); }
        .gallery-thumb-container { width:100%; height:180px; display:flex; align-items:center; justify-content:center; background:#000; }
        .gallery-card img { max-width:100%; max-height:100%; object-fit:contain; }
        .gallery-name { padding:12px; text-align:center; font-size:0.85em; font-weight:bold; color:var(--accent); background:#111; border-top:1px solid #222; }

        footer { background:var(--panel); padding:15px; border-top:1px solid #222; text-align:center; display:flex; align-items:center; justify-content:center; gap:10px; flex-shrink:0; }
        .powered-by { text-transform:uppercase; font-size:0.75em; letter-spacing:1px; color:var(--footer-text); font-weight:bold; }
        .footer-icon { height:22px; width:auto; opacity:0.8; }
        .footer-brand { color:var(--brand-color); font-size:0.9em; }

        body.night-vision .footer-brand strong { color: var(--accent); }
        body.night-vision .live-badge { background:rgba(0,0,0,0.8); color:#f00; border:1px solid #f00; }
    </style>
</head>
<body>
    <header><div>%1</div><div class="session-title">%2</div><div style="display:flex; align-items:center;">%3 <button id="red-btn">NIGHT MODE</button></div></header>
    <main>
        <section id="live-hero" style="display:%4;">
            <a href="/dashboard?id=%5" class="hero-link" id="hero-link">
                <div class="live-badge" id="live-badge" style="display:%6;">LIVE NOW</div>
                <div class="hero-thumb-wrapper"><img id="hero-img" src="/thumb/%5"></div>
            </a>
        </section>
        <div id="gallery-grid">%7</div>
    </main>
    <footer><span class="powered-by">Powered by</span><img src="/icon" class="footer-icon"><span class="footer-brand">KStars <strong>LiveStacker</strong></span></footer>
    <script>
        const rb = document.getElementById('red-btn');
        rb.onclick = () => { try { localStorage.setItem('nv', document.body.classList.toggle('night-vision')); } catch(e){} };
        if (localStorage.getItem('nv') === 'true') document.body.classList.add('night-vision');

        const source = new EventSource("/lobby-push");
        source.onmessage = (e) => {
            const d = JSON.parse(e.data);
            if (d.type === "stacking" || d.type === "metadata")
            {
                const hero = document.getElementById('live-hero');
                const badge = document.getElementById('live-badge');
                if (hero) hero.style.display = (d.active || d.type === "metadata") ? 'block' : 'none';
                if (badge && d.type === "stacking") badge.style.display = d.active ? 'block' : 'none';

                if (d.slug)
                {
                    const cacheBust = "?t=" + Date.now();
                    document.getElementById('hero-link').href = `/dashboard?id=${d.slug}`;
                    document.getElementById('hero-img').src = `/thumb/${d.slug}${cacheBust}`;

                    let gImg = document.getElementById(`img-${d.slug}`);
                    if (!gImg)
                    {
                        const grid = document.getElementById('gallery-grid');
                        const card = document.createElement('a');
                        card.id = `card-${d.slug}`; card.className = 'gallery-card'; card.href = `/dashboard?id=${d.slug}`;
                        card.innerHTML = `<div class="gallery-thumb-container"><img id="img-${d.slug}" src="/thumb/${d.slug}${cacheBust}"></div><div class="gallery-name">${d.target || 'Target'}</div>`;
                        grid.prepend(card);
                    }
                    else
                    {
                        gImg.src = `/thumb/${d.slug}${cacheBust}`;
                        if (d.target)
                            document.querySelector(`#card-${d.slug} .gallery-name`).textContent = d.target;
                    }
                }
            }
        };
    </script>
</body>
</html>)html")
                   .arg(logoLImg)
                   .arg(title.isEmpty() ? "Live Session" : title)
                   .arg(logoRImg)
                   .arg(activeSlug.isEmpty() ? "none" : "block")
                   .arg(activeSlug)
                   .arg(m_isStacking ? "block" : "none")
                   .arg(galleryItems);

    QByteArray data = html.toUtf8();
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + QByteArray::number(
                      data.size()) + "\r\nConnection: close\r\n\r\n" + data);
}

QString LiveStackWebcast::getSlugForPath(const QString &path)
{
    if (path.isEmpty())
        return QString();

    // Check for existing mapping
    QMapIterator<QString, QString> it(m_slugToPath);
    while (it.hasNext())
    {
        it.next();
        if (it.value() == path)
            return it.key();
    }

    // Hierarchy: Target Name is now the King
    QString baseName;
    if (m_metaCache.contains(path))
    {
        const auto &meta = m_metaCache[path];
        if (!meta.targetName.isEmpty())
            baseName = meta.targetName; // "M 43"
        else if (!meta.name.isEmpty())
            baseName = meta.name;
        else if (!meta.longName.isEmpty())
            baseName = meta.longName;
    }

    if (baseName.isEmpty())
        baseName = QDir(path).dirName();

    // Sanitization: "M 43" -> "m43"
    QString slug = baseName.toLower();
    slug.remove(QRegularExpression("[^a-z0-9]"));

    if (slug.isEmpty())
        slug = "target";

    // Collision Handling
    QString uniqueSlug = slug;
    int counter = 1;
    while (m_slugToPath.contains(uniqueSlug))
        uniqueSlug = slug + QString::number(counter++);

    m_slugToPath[uniqueSlug] = path;
    return uniqueSlug;
}

void LiveStackWebcast::broadcastImage(const QString &sourcePath, const QImage &image)
{
    SigPipeBlocker blocker;
    if (m_subscribedClients.isEmpty() && m_lobbySubscribers.isEmpty())
        return;

    // --- Encode QImage to JPEG ---
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 80)) // quality 80
    {
        qCDebug(KSTARS_FITS) << "Failed to encode image for MJPEG broadcast!";
        return;
    }

    // --- Build MJPEG frame ---
    QByteArray frame;
    frame.append("--kstars_frame\r\n");
    frame.append("Content-Type: image/jpeg\r\n");
    frame.append("Content-Length: " + QByteArray::number(imageData.size()) + "\r\n\r\n");
    frame.append(imageData);
    frame.append("\r\n");

    // --- Send MJPEG frame to subscribed clients ---
    QMutableMapIterator<QTcpSocket*, QString> it(m_subscribedClients);
    while (it.hasNext())
    {
        it.next();
        QTcpSocket *socket = it.key();

        if (socket && socket->state() == QAbstractSocket::ConnectedState && socket->isWritable())
        {
            if (it.value() == sourcePath || it.value() == "__placeholder__")
                socket->write(frame);
        }
        else
            it.remove();
    }

    // --- Update HUD via SSE (lobby subscribers) ---
    QJsonObject obj;
    obj["slug"]        = getSlugForPath(sourcePath);
    obj["longName"]    = QFileInfo(sourcePath).fileName();
    obj["frameNumber"] = static_cast<double>(m_frameCounter++); // QJsonValue needs double

    QJsonDocument doc(obj);
    QByteArray payload = "data: " + doc.toJson(QJsonDocument::Compact) + "\n\n";

    for (int i = 0; i < m_lobbySubscribers.size(); ++i)
    {
        QPointer<QTcpSocket> ptr = m_lobbySubscribers.at(i);
        if (ptr && ptr->state() == QAbstractSocket::ConnectedState && ptr->isWritable())
            ptr->write(payload);
        else
        {
            m_lobbySubscribers.removeAt(i);
            --i;
        }
    }

    qCDebug(KSTARS_FITS) << "Broadcasted frame for" << sourcePath
                         << "- MJPEG clients:" << m_subscribedClients.size()
                         << "SSE clients:" << m_lobbySubscribers.size();
}

void LiveStackWebcast::broadcastStatus()
{
    QString currentSlug = getSlugForPath(m_activePath);

    QJsonObject json;
    json["type"] = "stacking";        // Matches the JS listener
    json["active"] = m_isStacking;    // true = Show Badge, false = Hide
    json["slug"] = currentSlug;       // Update the href link
    if (m_metaCache.contains(m_activePath))
        json["target"] = m_metaCache[m_activePath].targetName;

    broadcastSSE(json);
}

void LiveStackWebcast::broadcastSSE(const QJsonObject &json)
{
    SigPipeBlocker blocker; // CRITICAL: This must be here.
    QByteArray payload = "data: " + QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n\n";

    for (int i = m_lobbySubscribers.size() - 1; i >= 0; --i)
    {
        QPointer<QTcpSocket> s = m_lobbySubscribers.at(i);
        if (s && s->state() == QAbstractSocket::ConnectedState)
            s->write(payload);
        else
            m_lobbySubscribers.removeAt(i);
    }
}

QJsonObject LiveStackWebcast::getHUDData(const QString &cleanPath)
{
    LiveStackMetadata meta = m_metaCache.value(cleanPath);

    QJsonObject root;
    root["type"]             = "metadata";
    root["slug"]             = getSlugForPath(cleanPath);
    root["target"]           = meta.targetName;
    root["name"]             = meta.name;
    root["name2"]            = meta.name2;
    root["longName"]         = meta.longName;
    root["typeName"]         = meta.typeName; // Updated key name
    root["magnitude"]        = QString::number(meta.magnitude, 'f', 1);
    root["constellation"]    = meta.constellation;
    root["subCount"]         = meta.subCount;
    root["exposureTime"]     = meta.exposureTime;
    root["totalIntegration"] = meta.totalIntegration;

    return root;
}

void LiveStackWebcast::resolveName(const QString &ip)
{
    QHostInfo::lookupHost(ip, this, [this, ip](const QHostInfo & host)
    {
        if (host.error() == QHostInfo::NoError && !host.hostName().isEmpty())
            m_resolvedNames[ip] = host.hostName();
        else
            m_resolvedNames[ip] = i18n("Unknown Device");
    });
}

