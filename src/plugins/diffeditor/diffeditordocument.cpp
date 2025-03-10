/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "diffeditordocument.h"
#include "diffeditorconstants.h"
#include "diffeditorcontroller.h"
#include "diffutils.h"

#include <utils/fileutils.h>
#include <utils/qtcassert.h>

#include <coreplugin/dialogs/codecselector.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QMenu>
#include <QTextCodec>
#include <QUuid>

using namespace Utils;

namespace DiffEditor {
namespace Internal {

DiffEditorDocument::DiffEditorDocument() :
    Core::BaseTextDocument()
{
    setId(Constants::DIFF_EDITOR_ID);
    setMimeType(Constants::DIFF_EDITOR_MIMETYPE);
    setTemporary(true);
}

/**
 * @brief Set a controller for a document
 * @param controller The controller to set.
 *
 * This method takes ownership of the controller and will delete it after it is done with it.
 */
void DiffEditorDocument::setController(DiffEditorController *controller)
{
    if (m_controller == controller)
        return;

    QTC_ASSERT(isTemporary(), return);

    if (m_controller)
        m_controller->deleteLater();
    m_controller = controller;
}

DiffEditorController *DiffEditorDocument::controller() const
{
    return m_controller;
}

static void appendRow(ChunkData *chunk, const RowData &row)
{
    const bool isSeparator = row.leftLine.textLineType == TextLineData::Separator
            && row.rightLine.textLineType == TextLineData::Separator;
    if (!isSeparator)
        chunk->rows.append(row);
}

ChunkData DiffEditorDocument::filterChunk(const ChunkData &data,
                                          const ChunkSelection &selection, bool revert)
{
    if (selection.isNull())
        return data;

    ChunkData chunk(data);
    chunk.rows.clear();
    for (int i = 0; i < data.rows.count(); ++i) {
        RowData row = data.rows[i];
        const bool isLeftSelected = selection.leftSelection.contains(i);
        const bool isRightSelected = selection.rightSelection.contains(i);

        if (isLeftSelected || isRightSelected) {
            if (row.equal || (isLeftSelected && isRightSelected)) {
                appendRow(&chunk, row);
            } else if (isLeftSelected) {
                RowData newRow = row;

                row.rightLine = TextLineData(TextLineData::Separator);
                appendRow(&chunk, row);

                if (revert) {
                    newRow.leftLine = newRow.rightLine;
                    newRow.equal = true;
                    appendRow(&chunk, newRow);
                }
            } else { // isRightSelected
                if (!revert) {
                    RowData newRow = row;
                    newRow.rightLine = newRow.leftLine;
                    newRow.equal = true;
                    appendRow(&chunk, newRow);
                }

                row.leftLine = TextLineData(TextLineData::Separator);
                appendRow(&chunk, row);
            }
        } else {
            if (revert)
                row.leftLine = row.rightLine;
            else
                row.rightLine = row.leftLine;
            row.equal = true;
            appendRow(&chunk, row);
        }
    }

    return chunk;
}

QString DiffEditorDocument::makePatch(int fileIndex, int chunkIndex,
                                      const ChunkSelection &selection,
                                      bool revert, bool addPrefix,
                                      const QString &overriddenFileName) const
{
    if (fileIndex < 0 || chunkIndex < 0)
        return QString();

    if (fileIndex >= m_diffFiles.count())
        return QString();

    const FileData &fileData = m_diffFiles.at(fileIndex);
    if (chunkIndex >= fileData.chunks.count())
        return QString();

    const ChunkData chunkData = filterChunk(fileData.chunks.at(chunkIndex), selection, revert);
    const bool lastChunk = (chunkIndex == fileData.chunks.count() - 1);

    const QString fileName = !overriddenFileName.isEmpty()
            ? overriddenFileName : revert
              ? fileData.rightFileInfo.fileName
              : fileData.leftFileInfo.fileName;

    QString leftPrefix, rightPrefix;
    if (addPrefix) {
        leftPrefix = "a/";
        rightPrefix = "b/";
    }
    return DiffUtils::makePatch(chunkData,
                                leftPrefix + fileName,
                                rightPrefix + fileName,
                                lastChunk && fileData.lastChunkAtTheEndOfFile);
}

void DiffEditorDocument::setDiffFiles(const QList<FileData> &data, const FilePath &directory,
                                      const QString &startupFile)
{
    m_diffFiles = data;
    if (!directory.isEmpty())
        m_baseDirectory = directory;
    m_startupFile = startupFile;
    emit documentChanged();
}

QList<FileData> DiffEditorDocument::diffFiles() const
{
    return m_diffFiles;
}

FilePath DiffEditorDocument::baseDirectory() const
{
    return m_baseDirectory;
}

void DiffEditorDocument::setBaseDirectory(const FilePath &directory)
{
    m_baseDirectory = directory;
}

QString DiffEditorDocument::startupFile() const
{
    return m_startupFile;
}

void DiffEditorDocument::setDescription(const QString &description)
{
    if (m_description == description)
        return;

    m_description = description;
    emit descriptionChanged();
}

QString DiffEditorDocument::description() const
{
    return m_description;
}

void DiffEditorDocument::setContextLineCount(int lines)
{
    QTC_ASSERT(!m_isContextLineCountForced, return);
    m_contextLineCount = lines;
}

int DiffEditorDocument::contextLineCount() const
{
    return m_contextLineCount;
}

void DiffEditorDocument::forceContextLineCount(int lines)
{
    m_contextLineCount = lines;
    m_isContextLineCountForced = true;
}

bool DiffEditorDocument::isContextLineCountForced() const
{
    return m_isContextLineCountForced;
}

void DiffEditorDocument::setIgnoreWhitespace(bool ignore)
{
    m_ignoreWhitespace = ignore;
}

bool DiffEditorDocument::ignoreWhitespace() const
{
    return m_ignoreWhitespace;
}

bool DiffEditorDocument::setContents(const QByteArray &contents)
{
    Q_UNUSED(contents)
    return true;
}

QString DiffEditorDocument::fallbackSaveAsPath() const
{
    if (!m_baseDirectory.isEmpty())
        return m_baseDirectory.toString();
    return QDir::homePath();
}

bool DiffEditorDocument::isSaveAsAllowed() const
{
    return state() == LoadOK;
}

bool DiffEditorDocument::save(QString *errorString, const FilePath &filePath, bool autoSave)
{
    Q_UNUSED(errorString)
    Q_UNUSED(autoSave)

    if (state() != LoadOK)
        return false;

    const bool ok = write(filePath, format(), plainText(), errorString);

    if (!ok)
        return false;

    setController(nullptr);
    setDescription(QString());
    Core::EditorManager::clearUniqueId(this);

    setTemporary(false);
    setFilePath(filePath.absoluteFilePath());
    setPreferredDisplayName(QString());
    emit temporaryStateChanged();

    return true;
}

void DiffEditorDocument::reload()
{
    if (m_controller) {
        m_controller->requestReload();
    } else {
        QString errorMessage;
        reload(&errorMessage, Core::IDocument::FlagReload, Core::IDocument::TypeContents);
    }
}

bool DiffEditorDocument::reload(QString *errorString, ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return true;
    return open(errorString, filePath(), filePath()) == OpenResult::Success;
}

Core::IDocument::OpenResult DiffEditorDocument::open(QString *errorString, const Utils::FilePath &filePath,
                              const Utils::FilePath &realFilePath)
{
    QTC_CHECK(filePath == realFilePath); // does not support autosave
    beginReload();
    QString patch;
    ReadResult readResult = read(filePath, &patch, errorString);
    if (readResult == TextFileFormat::ReadIOError
        || readResult == TextFileFormat::ReadMemoryAllocationError) {
        return OpenResult::ReadError;
    }

    bool ok = false;
    QList<FileData> fileDataList = DiffUtils::readPatch(patch, &ok);
    if (!ok) {
        *errorString = tr("Could not parse patch file \"%1\". "
                          "The content is not of unified diff format.")
                .arg(filePath.toUserOutput());
    } else {
        setTemporary(false);
        emit temporaryStateChanged();
        setFilePath(filePath.absoluteFilePath());
        setDiffFiles(fileDataList, filePath.absoluteFilePath());
    }
    endReload(ok);
    if (!ok && readResult == TextFileFormat::ReadEncodingError)
        ok = selectEncoding();
    return ok ? OpenResult::Success : OpenResult::CannotHandle;
}

bool DiffEditorDocument::selectEncoding()
{
    Core::CodecSelector codecSelector(Core::ICore::dialogParent(), this);
    switch (codecSelector.exec()) {
    case Core::CodecSelector::Reload: {
        setCodec(codecSelector.selectedCodec());
        QString errorMessage;
        return reload(&errorMessage, Core::IDocument::FlagReload, Core::IDocument::TypeContents);
    }
    case Core::CodecSelector::Save:
        setCodec(codecSelector.selectedCodec());
        return Core::EditorManager::saveDocument(this);
    case Core::CodecSelector::Cancel:
        break;
    }
    return false;
}

QString DiffEditorDocument::fallbackSaveAsFileName() const
{
    const int maxSubjectLength = 50;

    const QString desc = description();
    if (!desc.isEmpty()) {
        QString name = QString::fromLatin1("0001-%1").arg(desc.left(desc.indexOf('\n')));
        name = FileUtils::fileSystemFriendlyName(name);
        name.truncate(maxSubjectLength);
        name.append(".patch");
        return name;
    }
    return QStringLiteral("0001.patch");
}

// ### fixme: git-specific handling should be done in the git plugin:
// Remove unexpanded branches and follows-tag, clear indentation
// and create E-mail
static void formatGitDescription(QString *description)
{
    QString result;
    result.reserve(description->size());
    const auto descriptionList = description->split('\n');
    for (QString line : descriptionList) {
        if (line.startsWith("commit ") || line.startsWith("Branches: <Expand>"))
            continue;

        if (line.startsWith("Author: "))
            line.replace(0, 8, "From: ");
        else if (line.startsWith("    "))
            line.remove(0, 4);
        result.append(line);
        result.append('\n');
    }
    *description = result;
}

QString DiffEditorDocument::plainText() const
{
    QString result = description();
    const int formattingOptions = DiffUtils::GitFormat;
    if (formattingOptions & DiffUtils::GitFormat)
        formatGitDescription(&result);

    const QString diff = DiffUtils::makePatch(diffFiles(), formattingOptions);
    if (!diff.isEmpty()) {
        if (!result.isEmpty())
            result += '\n';
        result += diff;
    }
    return result;
}

void DiffEditorDocument::beginReload()
{
    emit aboutToReload();
    m_state = Reloading;
    emit changed();
    QSignalBlocker blocker(this);
    setDiffFiles(QList<FileData>(), {});
    setDescription(QString());
}

void DiffEditorDocument::endReload(bool success)
{
    m_state = success ? LoadOK : LoadFailed;
    emit changed();
    emit reloadFinished(success);
}

} // namespace Internal
} // namespace DiffEditor
