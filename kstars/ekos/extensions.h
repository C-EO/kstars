#ifndef EXTENSIONS_H
#define EXTENSIONS_H

#include "ekos.h"

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QIcon>

class extensions : public QObject
{
        Q_OBJECT
    public:
        explicit extensions(QObject *parent = nullptr);
        bool discover();
        void run(const QString &extension = "");
        void stop();
        void kill();

        struct extDetails
        {
            QString tooltip;
            QIcon icon;
            bool detached;
        };
        QMap<QString, extDetails>* found;

    public Q_SLOTS:
        QIcon getIcon(const QString &name);
        QString getTooltip(const QString &name);

    Q_SIGNALS:
        void extensionStateChanged(Ekos::ExtensionState);
        void extensionOutput(const QString);

    private:
        QString m_Directory = "";
        bool confValid(const QString &filePath);
        QProcess* extensionProcess;

};

#endif // EXTENSIONS_H
