#include "wallpapermanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRunnable>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

static QStringList systemWallpaperDirectories()
{
    const QString userDataPath = QDir::cleanPath(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));

    QStringList directories;
    const QStringList dataPaths =
            QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataPath : dataPaths) {
        if (QDir::cleanPath(dataPath) == userDataPath)
            continue;

        directories.append(QDir(dataPath).absoluteFilePath(
                QStringLiteral("backgrounds/cutefishos")));
    }

    return directories;
}

static bool isSystemWallpaper(const QString &path)
{
    const QString wallpaperPath = QFileInfo(path).absoluteFilePath();
    const QStringList directories = systemWallpaperDirectories();
    for (const QString &directory : directories) {
        if (wallpaperPath.startsWith(directory + QDir::separator()))
            return true;
    }

    return false;
}

static QString userWallpaperDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .absoluteFilePath(QStringLiteral("backgrounds/cutefish/user"));
}

static bool isManagedUserWallpaper(const QString &path)
{
    return QFileInfo(path).absoluteFilePath().startsWith(
            userWallpaperDirectory() + QDir::separator());
}

// A folder the user added is mirrored here, one directory per folder: the
// originals may be moved or deleted, and the wallpapers have to survive that.
static QString wallpaperFolderRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .absoluteFilePath(QStringLiteral("backgrounds/cutefish/folders"));
}

static QString wallpaperFolderStore(const QString &folder)
{
    const QByteArray folderHash =
            QCryptographicHash::hash(QDir(folder).absolutePath().toUtf8(),
                                     QCryptographicHash::Sha256)
                    .toHex()
                    .left(16);
    return QDir(wallpaperFolderRoot()).absoluteFilePath(QString::fromLatin1(folderHash));
}

static bool isManagedFolderWallpaper(const QString &path)
{
    return QFileInfo(path).absoluteFilePath().startsWith(wallpaperFolderRoot()
                                                         + QDir::separator());
}

static QStringList imagesIn(const QString &directory)
{
    static const QStringList nameFilters = {
        QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
        QStringLiteral("*.webp"), QStringLiteral("*.bmp"),  QStringLiteral("*.tif"),
        QStringLiteral("*.tiff"), QStringLiteral("*.avif"),
    };

    QStringList result;
    QDirIterator iterator(directory, nameFilters, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        result.append(iterator.next());

    return result;
}

// The copy's name carries a hash of the source path, so the same source always
// maps to the same copy and a renamed one to a new copy.
static QString mirroredFileName(const QFileInfo &sourceInfo)
{
    const QByteArray pathHash =
            QCryptographicHash::hash(sourceInfo.absoluteFilePath().toUtf8(),
                                     QCryptographicHash::Sha256)
                    .toHex()
                    .left(12);
    QString fileName = sourceInfo.completeBaseName() + QLatin1Char('-')
            + QString::fromLatin1(pathHash);
    if (!sourceInfo.suffix().isEmpty())
        fileName += QLatin1Char('.') + sourceInfo.suffix().toLower();
    return fileName;
}

static QString copyWallpaperInto(const QString &path, const QString &destinationDirectory)
{
    const QFileInfo sourceInfo(path);
    if (!sourceInfo.isFile())
        return path;

    const QString sourcePath = sourceInfo.absoluteFilePath();
    if (sourcePath.startsWith(destinationDirectory + QDir::separator()))
        return sourcePath;

    if (!QDir().mkpath(destinationDirectory))
        return path;

    const QString fileName = mirroredFileName(sourceInfo);

    const QString destinationPath = QDir(destinationDirectory).absoluteFilePath(fileName);
    if (QFileInfo::exists(destinationPath))
        return destinationPath;

    QFile source(sourcePath);
    QSaveFile destination(destinationPath);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly))
        return path;

    while (!source.atEnd()) {
        const QByteArray data = source.read(1024 * 1024);
        if (data.isEmpty() && source.error() != QFileDevice::NoError)
            return path;
        if (destination.write(data) != data.size())
            return path;
    }

    return destination.commit() ? destinationPath : path;
}

// A wallpaper that ships with the system is already where it belongs.
static QString importUserWallpaper(const QString &path)
{
    if (isSystemWallpaper(path))
        return path;

    return copyWallpaperInto(path, userWallpaperDirectory());
}

WallpaperManager::WallpaperManager(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_backgroundVisible = true;
    m_wallpaperPath =
            m_settings->value("Wallpaper", "/usr/share/backgrounds/cutefishos/default.jpg")
                    .toString();
    // A folder stays in the list even when its source is gone: what the list
    // shows is the copy taken when the folder was added.
    const QStringList savedWallpaperFolders =
            m_settings->value("WallpaperFolders").toStringList();
    for (const QString &folder : savedWallpaperFolders) {
        const QString folderPath = QDir(folder).absolutePath();
        if (!folderPath.isEmpty() && !m_wallpaperFolders.contains(folderPath))
            m_wallpaperFolders.append(folderPath);
    }
    m_settings->setValue("WallpaperFolders", m_wallpaperFolders);

    const QStringList savedUserWallpapers = m_settings->value("UserWallpapers").toStringList();
    for (const QString &path : savedUserWallpapers) {
        if (!QFileInfo::exists(path))
            continue;

        const QString importedPath = importUserWallpaper(path);
        if (isManagedUserWallpaper(importedPath))
            m_userWallpapers.append(importedPath);
    }
    if (QFileInfo::exists(m_wallpaperPath) && !isSystemWallpaper(m_wallpaperPath)
        && !isManagedFolderWallpaper(m_wallpaperPath)) {
        m_wallpaperPath = importUserWallpaper(m_wallpaperPath);
        m_settings->setValue("Wallpaper", m_wallpaperPath);
        if (isManagedUserWallpaper(m_wallpaperPath)
            && !m_userWallpapers.contains(m_wallpaperPath)) {
            m_userWallpapers.prepend(m_wallpaperPath);
        }
    }
    m_userWallpapers.removeDuplicates();
    m_settings->setValue("UserWallpapers", m_userWallpapers);
    m_backgroundType = m_settings->value("BackgroundType", 0).toInt();
    m_backgroundColor = m_settings->value("BackgroundColor", "#2B8ADA").toString();
}

bool WallpaperManager::backgroundVisible() const
{
    return m_backgroundVisible;
}

void WallpaperManager::setBackgroundVisible(bool value)
{
    if (m_backgroundVisible == value)
        return;

    m_backgroundVisible = value;
    emit backgroundVisibleChanged();
}

QString WallpaperManager::wallpaper() const
{
    return m_wallpaperPath;
}

void WallpaperManager::setWallpaper(const QString &path)
{
    const QString wallpaperPath = isManagedFolderWallpaper(path)
            ? QFileInfo(path).absoluteFilePath()
            : importUserWallpaper(path);
    if (QFileInfo::exists(wallpaperPath) && isManagedUserWallpaper(wallpaperPath)
        && !m_userWallpapers.contains(wallpaperPath)) {
        m_userWallpapers.prepend(wallpaperPath);
        m_settings->setValue("UserWallpapers", m_userWallpapers);
        emit userWallpapersChanged();
    }

    if (m_wallpaperPath != wallpaperPath) {
        m_wallpaperPath = wallpaperPath;
        m_settings->setValue("Wallpaper", wallpaperPath);
        emit wallpaperChanged(wallpaperPath);
    }
}

QStringList WallpaperManager::userWallpapers() const
{
    return m_userWallpapers;
}

void WallpaperManager::removeUserWallpaper(const QString &path)
{
    if (!isManagedUserWallpaper(path) || !m_userWallpapers.contains(path)) {
        return;
    }

    if (QFileInfo::exists(path) && !QFile::remove(path))
        return;

    m_userWallpapers.removeAll(path);
    m_settings->setValue("UserWallpapers", m_userWallpapers);

    if (path != m_wallpaperPath) {
        emit userWallpapersChanged();
        return;
    }

    QString replacement;
    for (const QString &candidate : m_userWallpapers) {
        if (QFileInfo::exists(candidate)) {
            replacement = candidate;
            break;
        }
    }

    if (replacement.isEmpty()) {
        const QStringList directories = systemWallpaperDirectories();
        for (const QString &directory : directories) {
            const QStringList wallpapers = imagesIn(directory);
            if (!wallpapers.isEmpty()) {
                replacement = wallpapers.first();
                break;
            }
        }
    }

    m_wallpaperPath = replacement;
    m_settings->setValue("Wallpaper", replacement);
    emit wallpaperChanged(replacement);

    if (replacement.isEmpty())
        setBackgroundType(1);

    emit userWallpapersChanged();
}

QStringList WallpaperManager::wallpaperFolders() const
{
    return m_wallpaperFolders;
}

void WallpaperManager::addWallpaperFolder(const QString &path)
{
    const QString folderPath = QDir(path).absolutePath();
    if (!QFileInfo(folderPath).isDir() || m_wallpaperFolders.contains(folderPath))
        return;

    m_wallpaperFolders.append(folderPath);
    m_settings->setValue("WallpaperFolders", m_wallpaperFolders);
    syncWallpaperFolder(folderPath);
    emit wallpaperFoldersChanged();
}

void WallpaperManager::removeWallpaperFolder(const QString &path)
{
    const QString folderPath = QDir(path).absolutePath();
    if (!m_wallpaperFolders.contains(folderPath))
        return;

    // The folder's copies go with it, so a wallpaper still in use is kept by
    // moving it over to the user's own wallpapers first.
    const QString store = wallpaperFolderStore(folderPath);
    if (m_wallpaperPath.startsWith(store + QDir::separator())) {
        const QString importedPath = importUserWallpaper(m_wallpaperPath);
        if (isManagedUserWallpaper(importedPath)) {
            m_wallpaperPath = importedPath;
            m_settings->setValue("Wallpaper", importedPath);
            if (!m_userWallpapers.contains(importedPath)) {
                m_userWallpapers.prepend(importedPath);
                m_settings->setValue("UserWallpapers", m_userWallpapers);
                emit userWallpapersChanged();
            }
            emit wallpaperChanged(importedPath);
        }
    }

    QDir(store).removeRecursively();
    m_wallpaperFolders.removeAll(folderPath);
    m_settings->setValue("WallpaperFolders", m_wallpaperFolders);
    emit wallpaperFoldersChanged();
}

// The copies are what the folder shows; the source is read again on every
// query so that images added to it later turn up here too. Copying a folder
// takes long enough to be worth a thread: the caller is told to ask again
// once the copies are on disk.
void WallpaperManager::syncWallpaperFolder(const QString &folder)
{
    if (m_syncingFolders.contains(folder))
        return;

    m_syncingFolders.insert(folder);
    const QString wallpaperInUse = m_wallpaperPath;
    QThreadPool::globalInstance()->start(QRunnable::create([this, folder, wallpaperInUse] {
        const QString store = wallpaperFolderStore(folder);
        QStringList before = imagesIn(store);
        before.sort();
        const QStringList sources = imagesIn(folder);
        QSet<QString> expected;
        for (const QString &source : sources) {
            copyWallpaperInto(source, store);
            expected.insert(mirroredFileName(QFileInfo(source)));
        }
        // A copy whose source was renamed, replaced or removed has no use, unless it is the
        // wallpaper in use; a source folder that is gone as a whole keeps its copies.
        if (QFileInfo(folder).isDir()) {
            for (const QString &copy : imagesIn(store)) {
                if (copy != wallpaperInUse && !expected.contains(QFileInfo(copy).fileName()))
                    QFile::remove(copy);
            }
        }
        QStringList after = imagesIn(store);
        after.sort();
        const bool copied = after != before;

        QMetaObject::invokeMethod(
                this, [this, folder, copied] { onWallpaperFolderSynced(folder, copied); },
                Qt::QueuedConnection);
    }));
}

void WallpaperManager::onWallpaperFolderSynced(const QString &folder, bool copied)
{
    m_syncingFolders.remove(folder);

    // The folder was removed while its copies were still being made.
    if (!m_wallpaperFolders.contains(folder)) {
        QDir(wallpaperFolderStore(folder)).removeRecursively();
        return;
    }

    if (copied)
        emit wallpaperFolderImagesChanged(folder);
}

QStringList WallpaperManager::wallpaperFolderImages(const QString &path)
{
    const QString folderPath = QDir(path).absolutePath();
    if (!m_wallpaperFolders.contains(folderPath))
        return {};

    syncWallpaperFolder(folderPath);
    return imagesIn(wallpaperFolderStore(folderPath));
}

int WallpaperManager::backgroundType() const
{
    return m_backgroundType;
}

void WallpaperManager::setBackgroundType(int type)
{
    if (m_backgroundType != type) {
        m_backgroundType = type;
        m_settings->setValue("BackgroundType", m_backgroundType);
        emit backgroundTypeChanged();
    }
}

QString WallpaperManager::backgroundColor() const
{
    return m_backgroundColor;
}

void WallpaperManager::setBackgroundColor(const QString &color)
{
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        m_settings->setValue("BackgroundColor", m_backgroundColor);
        emit backgroundColorChanged();
    }
}
