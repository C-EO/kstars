/*
    SPDX-FileCopyrightText: 2026 John Evans <john.e.evans.email@googlemail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "fitscommon.h"
#include "ui_livestackwebcast.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QImage>
#include <QElapsedTimer>
#include <QMap>
#include <QPointer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostInfo>

#include <csignal>

/**
 * @class LiveStackWebcast
 * @brief A lightweight HTTP and WebSocket-like server for broadcasting live astronomy stacking results.
 * * This class provides a web interface (Lobby and Dashboard) that allows remote users to view
 * real-time astronomical image stacking. It handles:
 * - MJPEG streaming of active stacks.
 * - Server-Sent Events (SSE) for metadata and UI synchronization.
 * - Dynamic gallery management for multiple targets.
 * - Night-vision mode UI styling.
 * *
 */
class LiveStackWebcast : public QWidget
{
        Q_OBJECT

    public:
        explicit LiveStackWebcast(QWidget *parent = nullptr);
        ~LiveStackWebcast();

        /**
         * @brief Starts listening for incoming HTTP connections.
         * @param port The TCP port to bind to.
         * @return true if the server started successfully, false otherwise.
         */
        bool listen(uint16_t port);

        /**
         * @brief Stops the TCP server and closes all active client sockets.
         */
        void stop();

        /**
         * @brief Checks if the webcast server is currently active.
         * @return true if the server is listening for connections.
         */
        bool isListening() const;

        /**
         * @brief Sets whether an active stacking process is currently running.
         * @param active True if stacking is in progress.
         * @param path The filesystem path associated with the current stack.
         */
        void setStackingActive(const bool active, const QString &path);

        /**
         * @brief Processes a complete stack update with metadata.
         * @param sourcePath Path to the source data.
         * @param meta Metadata including exposure time and count.
         * @param image The final stacked image.
         */
        void processIncomingStack(const QString &sourcePath, const LiveStackMetadata &meta, const QImage &image);

        /**
         * @brief Resets the session, clearing gallery caches and target history.
         */
        void resetSession();

        /** @return The current TCP port. */
        int getPort();

        /** @brief Sets the TCP port. */
        void setPort(const int port);

        /** @return The broadcast throttle interval in milliseconds. */
        int getThrottle();

        /** @brief Sets the minimum time between image broadcasts to save bandwidth. */
        void setThrottle(const int ms);

        /** @return The current session title. */
        QString getSessionTitle();

        /** @brief Sets the title displayed in the web header. */
        void setSessionTitle(const QString &title);

        /** @return Path to the left logo. */
        QString getLogoLPath();

        /** @brief Sets the local file path for the left-side header logo. */
        void setLogoLPath(const QString &path);

        /** @return Path to the right logo. */
        QString getLogoRPath();

        /** @brief Sets the local file path for the right-side header logo. */
        void setLogoRPath(const QString &path);

        /**
         * @brief Generates the local URL for users to access the webcast.
         * @return A string formatted as http://[local_ip]:[port]
         */
        QString getLocalURL() const;

        /**
         * @brief Generates a QR code for easy mobile access to the URL.
         * @return A QPixmap containing the QR code image.
         */
        QPixmap generateQRCodePixmap();

        /**
         * @brief Return a list of connected viewer devices
         * @return List of viewers.
         */
        QList<ConnectedViewer> getActiveViewers();

    protected:
        /** Intercept the window manager's close signal */
        void closeEvent(QCloseEvent *event) override;

    private slots:
        /** @brief Update UI widgets */
        void updateUI();

        /** @brief Accepts incoming TCP connections from QTcpServer. */
        void handleNewConnection();

        /** @brief Parses the HTTP request and routes to the appropriate handler. */
        void processRequest(QTcpSocket *socket);

    private:
        /** @brief Cleans up client registration when a socket closes. */
        void clientDisconnected();

        /** @brief Sends an image update to all clients currently viewing the dashboard. */
        void broadcastImage(const QString &sourcePath, const QImage &image);

        /** @brief Broadcasts stacking status (active/inactive) to the Lobby via SSE. */
        void broadcastStatus();

        /** @brief Internal helper to write JSON to all SSE subscribers. */
        void broadcastSSE(const QJsonObject &json);

        /** @brief Handles specific dashboard logic and parameter parsing. */
        void sendDashboard(QTcpSocket *socket, const QString &path);

        /** @brief Serves a cached thumbnail for the gallery. */
        void sendGalleryImage(QTcpSocket *socket, const QString &path);

        /** @brief Serves a generic image file (e.g., logos, icons) from disk. */
        void sendImageFile(QTcpSocket *socket, const QString &filePath);

        /** @brief Renders the main Lobby HTML with the gallery grid. */
        void sendLobby(QTcpSocket *socket);

        /** @brief Generates a URL-safe unique identifier for a target path. */
        QString getSlugForPath(const QString &path);

        /** @brief Aggregates astronomical metadata into a JSON object for the HUD. */
        QJsonObject getHUDData(const QString &path);

        /** @brief Get a hostname from an ip address */
        void resolveName(const QString &ip);

        Ui::FitsStackWebcast *ui;
        QTimer *m_refreshTimer;                          // Timer to refresh subscribers
        QTcpServer *m_tcpServer { nullptr };             // Internal TCP server.
        int m_port { -1 };                               // Target port.
        int m_throttleMs { -1 };                         // Milliseconds between broadcasts.

        QElapsedTimer m_lastBroadcastTimer;              // Timer for throttle logic.
        const QByteArray m_boundary { "kstars_frame" };  // MJPEG stream boundary.

        QString m_sessionTitle;                          // Title shown on web pages.
        QString m_logoLPath;                             // Path to left logo image.
        QString m_logoRPath;                             // Path to right logo image.
        QString m_activePath;                            // Path of the currently stacking target.
        bool m_isStacking { false };                     // Stacking status flag.
        quint64 m_frameCounter { 0 };                    // Counter for image frames.

        QMap<QString, QByteArray> m_galleryCache;        // JPG-encoded thumbnail cache.
        QMap<QTcpSocket *, QString> m_subscribedClients; // MJPEG stream subscribers.
        QMap<QString, QString> m_slugToPath;             // Map of URL slugs to filesystem paths.
        QMap<QString, QDateTime> m_lastUpdateTimes;      // Last update timestamp per target.
        QMap<QString, LiveStackMetadata> m_metaCache;    // Cached stacking metadata.
        QList<QPointer<QTcpSocket>> m_lobbySubscribers;  // SSE subscribers list.
        QMap<QString, QString> m_resolvedNames;          // Cache: IP -> Hostname
        QMap<QString, QString> m_deviceTypeCache;        // Cache: IP -> Device Type
};

/**
 * @class SigPipeBlocker
 * @brief RAII helper to temporarily ignore SIGPIPE signals.
 * * In Unix-like systems, writing to a closed socket triggers a SIGPIPE which
 * crashes the process by default. This class suppresses that behavior
 * within its scope.
 */
class SigPipeBlocker
{
    public:
        /** @brief Swaps the SIGPIPE handler to SIG_IGN. */
        SigPipeBlocker()
        {
#ifndef Q_OS_WIN
            oldHandler = std::signal(SIGPIPE, SIG_IGN);
#endif
        }

        /** @brief Restores the previous SIGPIPE handler. */
        ~SigPipeBlocker()
        {
#ifndef Q_OS_WIN
            std::signal(SIGPIPE, oldHandler);
#endif
        }
    private:
#ifndef Q_OS_WIN
        void (*oldHandler)(int); ///< Stores the previous signal handler.
#endif
};
