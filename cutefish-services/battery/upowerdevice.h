#ifndef UPOWERDEVICE_H
#define UPOWERDEVICE_H

#include <QObject>
#include <QDBusInterface>

class UPowerDevice : public QObject
{
    Q_OBJECT

public:
    explicit UPowerDevice(const QString &udi, QObject *parent = nullptr);

    QString udi() const;

    QVariant prop(const QString &key) const;
    QMap<QString, QVariant> allProperties() const;

signals:
    void changed();

private slots:
    void onPropertiesChanged(const QString &ifaceName, const QVariantMap &changedProps, const QStringList &invalidatedProps);
    void login1Resuming(bool active);
    void slotChanged();

private:
    void checkCache(const QString &key) const;

private:
    mutable QDBusInterface m_device;
    mutable QVariantMap m_cache;

    QString m_udi;
};

#endif // UPOWERDEVICE_H
