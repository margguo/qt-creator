/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "itemlibraryassetsmodel.h"
#include "itemlibraryassetsdirsmodel.h"
#include "itemlibraryassetsfilesmodel.h"

#include <synchronousimagecache.h>
#include <theme.h>
#include <hdrimage.h>
#include <designersettings.h>

#include <coreplugin/icore.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFont>
#include <QImageReader>
#include <QMetaProperty>
#include <QPainter>
#include <QRawFont>
#include <QMessageBox>
#include <QCheckBox>
#include <utils/stylehelper.h>
#include <utils/filesystemwatcher.h>

namespace QmlDesigner {

void ItemLibraryAssetsModel::saveExpandedState(bool expanded, const QString &assetPath)
{
    m_expandedStateHash.insert(assetPath, expanded);
}

bool ItemLibraryAssetsModel::loadExpandedState(const QString &assetPath)
{
    return m_expandedStateHash.value(assetPath, true);
}

ItemLibraryAssetsModel::DirExpandState ItemLibraryAssetsModel::getAllExpandedState() const
{
    const auto keys = m_expandedStateHash.keys();
    bool allExpanded = true;
    bool allCollapsed = true;
    for (const QString &assetPath : keys) {
        bool expanded = m_expandedStateHash.value(assetPath);

        if (expanded)
            allCollapsed = false;
        if (!expanded)
            allExpanded = false;

        if (!allCollapsed && !allExpanded)
            break;
    }

    return allExpanded ? DirExpandState::AllExpanded : allCollapsed ? DirExpandState::AllCollapsed
           : DirExpandState::SomeExpanded;
}

void ItemLibraryAssetsModel::toggleExpandAll(bool expand)
{
    std::function<void(ItemLibraryAssetsDir *)> expandDirRecursive;
    expandDirRecursive = [&](ItemLibraryAssetsDir *currAssetsDir) {
        if (currAssetsDir->dirDepth() > 0) {
            currAssetsDir->setDirExpanded(expand);
            saveExpandedState(expand, currAssetsDir->dirPath());
        }

        const QList<ItemLibraryAssetsDir *> childDirs = currAssetsDir->childAssetsDirs();
        for (const auto childDir : childDirs)
            expandDirRecursive(childDir);
    };

    beginResetModel();
    expandDirRecursive(m_assetsDir);
    endResetModel();
}

void ItemLibraryAssetsModel::removeFile(const QString &filePath)
{
    bool askBeforeDelete = DesignerSettings::getValue(
                DesignerSettingsKey::ASK_BEFORE_DELETING_ASSET).toBool();
    bool assetDelete = true;

    if (askBeforeDelete) {
        QMessageBox msg(QMessageBox::Question, tr("Confirm Delete File"),
                        tr("\"%1\" might be in use. Delete anyway?").arg(filePath),
                        QMessageBox::No | QMessageBox::Yes);
        QCheckBox cb;
        cb.setText(tr("Do not ask this again"));
        msg.setCheckBox(&cb);
        int ret = msg.exec();

        if (ret == QMessageBox::No)
            assetDelete = false;

        if (cb.isChecked())
            DesignerSettings::setValue(DesignerSettingsKey::ASK_BEFORE_DELETING_ASSET, false);
    }

    if (assetDelete) {
        if (!QFile::exists(filePath)) {
            QMessageBox::warning(Core::ICore::dialogParent(),
                                 tr("Failed to Locate File"),
                                 tr("Could not find \"%1\".").arg(filePath));
        } else if (!QFile::remove(filePath)) {
            QMessageBox::warning(Core::ICore::dialogParent(),
                                 tr("Failed to Delete File"),
                                 tr("Could not delete \"%1\".").arg(filePath));
        }
    }
}

const QStringList &ItemLibraryAssetsModel::supportedImageSuffixes()
{
    static QStringList retList;
    if (retList.isEmpty()) {
        const QList<QByteArray> suffixes = QImageReader::supportedImageFormats();
        for (const QByteArray &suffix : suffixes)
            retList.append("*." + QString::fromUtf8(suffix));
    }
    return retList;
}

const QStringList &ItemLibraryAssetsModel::supportedFragmentShaderSuffixes()
{
    static const QStringList retList {"*.frag", "*.glsl", "*.glslf", "*.fsh"};
    return retList;
}

const QStringList &ItemLibraryAssetsModel::supportedShaderSuffixes()
{
    static const QStringList retList {"*.frag", "*.vert",
                                      "*.glsl", "*.glslv", "*.glslf",
                                      "*.vsh", "*.fsh"};
    return retList;
}

const QStringList &ItemLibraryAssetsModel::supportedFontSuffixes()
{
    static const QStringList retList {"*.ttf", "*.otf"};
    return retList;
}

const QStringList &ItemLibraryAssetsModel::supportedAudioSuffixes()
{
    static const QStringList retList {"*.wav"};
    return retList;
}

const QStringList &ItemLibraryAssetsModel::supportedTexture3DSuffixes()
{
    // These are file types only supported by 3D textures
    static QStringList retList {"*.hdr"};
    return retList;
}

ItemLibraryAssetsModel::ItemLibraryAssetsModel(SynchronousImageCache &fontImageCache,
                                               Utils::FileSystemWatcher *fileSystemWatcher,
                                               QObject *parent)
    : QAbstractListModel(parent)
    , m_fontImageCache(fontImageCache)
    , m_fileSystemWatcher(fileSystemWatcher)
{
    // add role names
    int role = 0;
    const QMetaObject meta = ItemLibraryAssetsDir::staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i)
        m_roleNames.insert(role++, meta.property(i).name());
}

QVariant ItemLibraryAssetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        qWarning() << Q_FUNC_INFO << "Invalid index requested: " << QString::number(index.row());
        return {};
    }

    if (m_roleNames.contains(role))
        return m_assetsDir ? m_assetsDir->property(m_roleNames.value(role)) : QVariant();

    qWarning() << Q_FUNC_INFO << "Invalid role requested: " << QString::number(role);
    return {};
}

int ItemLibraryAssetsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)

    return 1;
}

QHash<int, QByteArray> ItemLibraryAssetsModel::roleNames() const
{
    return m_roleNames;
}

// called when a directory is changed to refresh the model for this directory
void ItemLibraryAssetsModel::refresh()
{
    setRootPath(m_assetsDir->dirPath());
}

void ItemLibraryAssetsModel::setRootPath(const QString &path)
{
    static const QStringList ignoredTopLevelDirs {"imports", "asset_imports"};

    m_fileSystemWatcher->removeDirectories(m_fileSystemWatcher->directories());
    m_fileSystemWatcher->removeFiles(m_fileSystemWatcher->files());

    std::function<bool(ItemLibraryAssetsDir *, int)> parseDirRecursive;
    parseDirRecursive = [this, &parseDirRecursive](ItemLibraryAssetsDir *currAssetsDir, int currDepth) {
        m_fileSystemWatcher->addDirectory(currAssetsDir->dirPath(), Utils::FileSystemWatcher::WatchAllChanges);

        QDir dir(currAssetsDir->dirPath());
        dir.setNameFilters(supportedSuffixes().values());
        dir.setFilter(QDir::Files);
        QDirIterator itFiles(dir);
        bool isEmpty = true;
        while (itFiles.hasNext()) {
            QString filePath = itFiles.next();
            QString fileName = filePath.split('/').last();
            if (m_searchText.isEmpty() || fileName.contains(m_searchText, Qt::CaseInsensitive)) {
                currAssetsDir->addFile(filePath);
                m_fileSystemWatcher->addFile(filePath, Utils::FileSystemWatcher::WatchAllChanges);
                isEmpty = false;
            }
        }

        dir.setNameFilters({});
        dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
        QDirIterator itDirs(dir);

        while (itDirs.hasNext()) {
            QDir subDir = itDirs.next();
            if (currDepth == 1 && ignoredTopLevelDirs.contains(subDir.dirName()))
                continue;

            ItemLibraryAssetsDir *assetsDir = new ItemLibraryAssetsDir(subDir.path(), currDepth, loadExpandedState(subDir.path()), currAssetsDir);
            currAssetsDir->addDir(assetsDir);
            saveExpandedState(loadExpandedState(assetsDir->dirPath()), assetsDir->dirPath());
            isEmpty &= parseDirRecursive(assetsDir, currDepth + 1);
        }

        if (isEmpty)
            currAssetsDir->setDirVisible(false);

        return isEmpty;
    };

    if (m_assetsDir)
        delete m_assetsDir;

    beginResetModel();
    m_assetsDir = new ItemLibraryAssetsDir(path, 0, true, this);
    parseDirRecursive(m_assetsDir, 1);
    endResetModel();
}

void ItemLibraryAssetsModel::setSearchText(const QString &searchText)
{
    if (m_searchText != searchText) {
        m_searchText = searchText;
        refresh();
    }
}

const QSet<QString> &ItemLibraryAssetsModel::supportedSuffixes() const
{
    static QSet<QString> allSuffixes;
    if (allSuffixes.isEmpty()) {
        auto insertSuffixes = [](const QStringList &suffixes) {
            for (const auto &suffix : suffixes)
                allSuffixes.insert(suffix);
        };
        insertSuffixes(supportedImageSuffixes());
        insertSuffixes(supportedShaderSuffixes());
        insertSuffixes(supportedFontSuffixes());
        insertSuffixes(supportedAudioSuffixes());
        insertSuffixes(supportedTexture3DSuffixes());
    }
    return allSuffixes;
}

const QSet<QString> &ItemLibraryAssetsModel::previewableSuffixes() const
{
    static QSet<QString> previewableSuffixes;
    if (previewableSuffixes.isEmpty()) {
        auto insertSuffixes = [](const QStringList &suffixes) {
            for (const auto &suffix : suffixes)
                previewableSuffixes.insert(suffix);
        };
        insertSuffixes(supportedFontSuffixes());
    }
    return previewableSuffixes;
}

} // namespace QmlDesigner
