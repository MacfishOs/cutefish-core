#ifndef BRIGHTNESSMANAGER_H
#define BRIGHTNESSMANAGER_H

#include "ddcworker.h"

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

class BrightnessManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool brightnessEnabled READ brightnessEnabled)
    Q_PROPERTY(int maxBrightness READ maxBrightness)
    Q_PROPERTY(int brightness READ brightness WRITE setValue)

public:
    BrightnessManager(QObject *parent = nullptr);
    ~BrightnessManager();

    // Legacy single-display API; acts on the internal panel, else the first adjustable display.
    int maxBrightness();
    int brightness();
    bool brightnessEnabled();
    void setValue(int value);

    // Displays are sysfs DRM connector names, e.g. "card0-eDP-1". Values are percentages.
    QStringList displays() const;
    int displayBrightness(const QString &display) const;
    void setDisplayBrightness(const QString &display, int value);

Q_SIGNALS:
    void brightnessChanged();
    void displaysChanged();
    void displayBrightnessChanged(const QString &display, int value);

private:
    enum class Kind {
        Backlight,
        Ddc
    };

    struct Display {
        QString connector;
        Kind kind = Kind::Backlight;
        QString device; // backlight name or /dev/i2c-N
        int maximum = 0;
        int raw = 0;
    };

    void rescan();
    QList<Display> scanBacklights(const QStringList &connected) const;
    QList<DdcDisplay> ddcCandidates(const QStringList &connected,
                                    const QList<Display> &backlights) const;
    void publish(QList<Display> displays);
    void readBacklight(Display &display) const;
    Display *find(const QString &display);
    const Display *find(const QString &display) const;
    int percent(const Display &display) const;
    void notifyIfChanged(const Display &display, int oldPercent);

private slots:
    void scheduleRescan();
    void handleFileChanged(const QString &path);
    void handleDdcProbed(quint64 generation, const QList<DdcDisplay> &displays);

private:
    QFileSystemWatcher *m_fileWatcher;
    QTimer m_rescanTimer;
    QThread m_ddcThread;
    DdcWorker *m_ddcWorker;
    quint64 m_generation = 0;
    QList<Display> m_displays;
};

#endif
