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

#include "pathlisteditor.h"

#include "hostosinfo.h"
#include "stringutils.h"
#include "fileutils.h"

#include <QDebug>
#include <QFileDialog>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSharedPointer>
#include <QTextBlock>
#include <QVBoxLayout>

/*!
    \class Utils::PathListEditor

    \brief The PathListEditor class is a control that lets the user edit a list
    of (directory) paths
    using the platform separator (';',':').

    Typically used for
    path lists controlled by environment variables, such as
    PATH. It is based on a QPlainTextEdit as it should
    allow for convenient editing and non-directory type elements like
    \code
    "etc/mydir1:$SPECIAL_SYNTAX:/etc/mydir2".
    \endcode

    When pasting text into it, the platform separator will be replaced
    by new line characters for convenience.
 */

namespace Utils {

const int PathListEditor::lastInsertButtonIndex = 0;

// ------------ PathListPlainTextEdit:
// Replaces the platform separator ';',':' by '\n'
// when inserting, allowing for pasting in paths
// from the terminal or such.

class PathListPlainTextEdit : public QPlainTextEdit {
public:
    explicit PathListPlainTextEdit(QWidget *parent = nullptr);
protected:
    void insertFromMimeData (const QMimeData *source) override;
};

PathListPlainTextEdit::PathListPlainTextEdit(QWidget *parent) :
    QPlainTextEdit(parent)
{
    // No wrapping, scroll at all events
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

void PathListPlainTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (source->hasText()) {
        // replace separator
        QString text = source->text().trimmed();
        text.replace(HostOsInfo::pathListSeparator(), QLatin1Char('\n'));
        QSharedPointer<QMimeData> fixed(new QMimeData);
        fixed->setText(text);
        QPlainTextEdit::insertFromMimeData(fixed.data());
    } else {
        QPlainTextEdit::insertFromMimeData(source);
    }
}

// ------------ PathListEditorPrivate
struct PathListEditorPrivate {
    PathListEditorPrivate();

    QHBoxLayout *layout;
    QVBoxLayout *buttonLayout;
    QPlainTextEdit *edit;
    QString fileDialogTitle;
};

PathListEditorPrivate::PathListEditorPrivate()   :
        layout(new QHBoxLayout),
        buttonLayout(new QVBoxLayout),
        edit(new PathListPlainTextEdit)
{
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit);
    layout->addLayout(buttonLayout);
    buttonLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Ignored,
                                          QSizePolicy::MinimumExpanding));
}

PathListEditor::PathListEditor(QWidget *parent) :
        QWidget(parent),
        d(new PathListEditorPrivate)
{
    setLayout(d->layout);
    addButton(tr("Insert..."), this, [this] {
        const FilePath dir = FileUtils::getExistingDirectory(this, d->fileDialogTitle);
        if (!dir.isEmpty())
            insertPathAtCursor(dir.toUserOutput());
    });
    addButton(tr("Delete Line"), this, [this] { deletePathAtCursor(); });
    addButton(tr("Clear"), this, [this] { d->edit->clear(); });
}

PathListEditor::~PathListEditor()
{
    delete d;
}

QPushButton *PathListEditor::addButton(const QString &text, QObject *parent,
                                       std::function<void()> slotFunc)
{
    return insertButton(d->buttonLayout->count() - 1, text, parent, slotFunc);
}

QPushButton *PathListEditor::insertButton(int index /* -1 */, const QString &text, QObject *parent,
                                          std::function<void()> slotFunc)
{
    auto rc = new QPushButton(text, this);
    QObject::connect(rc, &QPushButton::pressed, parent, slotFunc);
    d->buttonLayout->insertWidget(index, rc);
    return rc;
}

QString PathListEditor::pathListString() const
{
    return pathList().join(HostOsInfo::pathListSeparator());
}

QStringList PathListEditor::pathList() const
{
    const QString text = d->edit->toPlainText().trimmed();
    if (text.isEmpty())
        return QStringList();
    // trim each line
    QStringList rc = text.split('\n', Qt::SkipEmptyParts);
    const QStringList::iterator end = rc.end();
    for (QStringList::iterator it = rc.begin(); it != end; ++it)
        *it = it->trimmed();
    return rc;
}

void PathListEditor::setPathList(const QStringList &l)
{
    d->edit->setPlainText(l.join(QLatin1Char('\n')));
}

void PathListEditor::setPathList(const QString &pathString)
{
    if (pathString.isEmpty()) {
        clear();
    } else {
        setPathList(pathString.split(HostOsInfo::pathListSeparator(),
                                     Qt::SkipEmptyParts));
    }
}

QString PathListEditor::fileDialogTitle() const
{
    return d->fileDialogTitle;
}

void PathListEditor::setFileDialogTitle(const QString &l)
{
    d->fileDialogTitle = l;
}

void PathListEditor::clear()
{
    d->edit->clear();
}

QString PathListEditor::text() const
{
    return d->edit->toPlainText();
}

void PathListEditor::setText(const QString &t)
{
    d->edit->setPlainText(t);
}

void PathListEditor::insertPathAtCursor(const QString &path)
{
    // If the cursor is at an empty line or at end(),
    // just insert. Else insert line before
    QTextCursor cursor = d->edit->textCursor();
    QTextBlock block = cursor.block();
    const bool needNewLine = !block.text().isEmpty();
    if (needNewLine) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.insertBlock();
        cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::MoveAnchor);
    }
    cursor.insertText(path);
    if (needNewLine) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        d->edit->setTextCursor(cursor);
    }
}

void PathListEditor::deletePathAtCursor()
{
    // Delete current line
    QTextCursor cursor = d->edit->textCursor();
    if (cursor.block().isValid()) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        // Select down or until end of [last] line
        if (!cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor))
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        d->edit->setTextCursor(cursor);
    }
}

} // namespace Utils
