/****************************************************************************
**
** Copyright (C) 2016 Przemyslaw Gorszkowski <pgorszkowski@gmail.com>
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

#include "cppincludehierarchy.h"

#include "cppeditor.h"
#include "cppeditorwidget.h"
#include "cppeditorconstants.h"
#include "cppeditordocument.h"
#include "cppeditorplugin.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/fileiconprovider.h>
#include <coreplugin/find/itemviewfind.h>

#include <cpptools/baseeditordocumentprocessor.h>
#include <cpptools/cppelementevaluator.h>
#include <cpptools/cppmodelmanager.h>
#include <cpptools/cpptoolsbridge.h>
#include <cpptools/editordocumenthandle.h>

#include <cplusplus/CppDocument.h>

#include <utils/delegates.h>
#include <utils/dropsupport.h>
#include <utils/fileutils.h>
#include <utils/navigationtreeview.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>
#include <utils/utilsicons.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Core;
using namespace CPlusPlus;
using namespace CppTools;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor {
namespace Internal {

enum {
    AnnotationRole = Qt::UserRole + 1,
    LinkRole
};

static Snapshot globalSnapshot()
{
    return CppModelManager::instance()->snapshot();
}

struct FileAndLine
{
    FileAndLine() = default;
    FileAndLine(const QString &f, int l) : file(f), line(l) {}

    QString file;
    int line = 0;
};

using FileAndLines = QList<FileAndLine>;

static FileAndLines findIncluders(const QString &filePath)
{
    FileAndLines result;
    const Snapshot snapshot = globalSnapshot();
    for (auto cit = snapshot.begin(), citEnd = snapshot.end(); cit != citEnd; ++cit) {
        const QString filePathFromSnapshot = cit.key().toString();
        Document::Ptr doc = cit.value();
        const QList<Document::Include> resolvedIncludes = doc->resolvedIncludes();
        for (const auto &includeFile : resolvedIncludes) {
            const QString includedFilePath = includeFile.resolvedFileName();
            if (includedFilePath == filePath)
                result.append(FileAndLine(filePathFromSnapshot, int(includeFile.line())));
        }
    }
    return result;
}

static FileAndLines findIncludes(const QString &filePath, const Snapshot &snapshot)
{
    FileAndLines result;
    if (Document::Ptr doc = snapshot.document(filePath)) {
        const QList<Document::Include> resolvedIncludes = doc->resolvedIncludes();
        for (const auto &includeFile : resolvedIncludes)
            result.append(FileAndLine(includeFile.resolvedFileName(), 0));
    }
    return result;
}

class CppIncludeHierarchyItem
    : public TypedTreeItem<CppIncludeHierarchyItem, CppIncludeHierarchyItem>
{
public:
    enum SubTree { RootItem, InIncludes, InIncludedBy };
    CppIncludeHierarchyItem() = default;

    void createChild(const QString &filePath, SubTree subTree,
                     int line = 0, bool definitelyNoChildren = false)
    {
        auto item = new CppIncludeHierarchyItem;
        item->m_fileName = filePath.mid(filePath.lastIndexOf('/') + 1);
        item->m_filePath = filePath;
        item->m_line = line;
        item->m_subTree = subTree;
        appendChild(item);
        for (auto ancestor = this; ancestor; ancestor = ancestor->parent()) {
            if (ancestor->filePath() == filePath) {
                item->m_isCyclic = true;
                break;
            }
        }
        if (filePath == model()->editorFilePath() || definitelyNoChildren)
            item->setChildrenChecked();
    }

    QString filePath() const
    {
        return isPhony() ? model()->editorFilePath() : m_filePath;
    }

private:
    bool isPhony() const { return !parent() || !parent()->parent(); }
    void setChildrenChecked() { m_checkedForChildren = true; }

    CppIncludeHierarchyModel *model() const
    {
        return static_cast<CppIncludeHierarchyModel *>(TreeItem::model());
    }

    QVariant data(int column, int role) const override;

    Qt::ItemFlags flags(int) const override
    {
        const Utils::Link link(Utils::FilePath::fromString(m_filePath), m_line);
        if (link.hasValidTarget())
            return Qt::ItemIsDragEnabled | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    bool canFetchMore() const override;
    void fetchMore() override;

    QString m_fileName;
    QString m_filePath;
    int m_line = 0;
    SubTree m_subTree = RootItem;
    bool m_isCyclic = false;
    bool m_checkedForChildren = false;
};

QVariant CppIncludeHierarchyItem::data(int column, int role) const
{
    Q_UNUSED(column)
    if (role == Qt::DisplayRole) {
        if (isPhony() && childCount() == 0)
            return QString(m_fileName + ' ' + CppIncludeHierarchyModel::tr("(none)"));
        if (m_isCyclic)
            return QString(m_fileName + ' ' + CppIncludeHierarchyModel::tr("(cyclic)"));
        return m_fileName;
    }

    if (isPhony())
        return QVariant();

    switch (role) {
        case Qt::ToolTipRole:
            return m_filePath;
        case Qt::DecorationRole:
            return FileIconProvider::icon(FilePath::fromString(m_filePath));
        case LinkRole:
            return QVariant::fromValue(Link(FilePath::fromString(m_filePath), m_line));
    }

    return QVariant();
}

bool CppIncludeHierarchyItem::canFetchMore() const
{
    if (m_isCyclic || m_checkedForChildren || childCount() > 0)
        return false;

    return !model()->m_searching || !model()->m_seen.contains(m_filePath);
}

void CppIncludeHierarchyItem::fetchMore()
{
    QTC_ASSERT(canFetchMore(), setChildrenChecked(); return);
    QTC_ASSERT(model(), return);
    QTC_ASSERT(m_subTree != RootItem, return); // Root should always be populated.

    model()->m_seen.insert(m_filePath);

    const QString editorFilePath = model()->editorFilePath();

    setChildrenChecked();
    if (m_subTree == InIncludes) {
        auto processor = CppToolsBridge::baseEditorDocumentProcessor(editorFilePath);
        QTC_ASSERT(processor, return);
        const Snapshot snapshot = processor->snapshot();
        const FileAndLines includes = findIncludes(filePath(), snapshot);
        for (const FileAndLine &include : includes) {
            const FileAndLines subIncludes = findIncludes(include.file, snapshot);
            bool definitelyNoChildren = subIncludes.isEmpty();
            createChild(include.file, InIncludes, include.line, definitelyNoChildren);
        }
    } else if (m_subTree == InIncludedBy) {
        const FileAndLines includers = findIncluders(filePath());
        for (const FileAndLine &includer : includers) {
            const FileAndLines subIncluders = findIncluders(includer.file);
            bool definitelyNoChildren = subIncluders.isEmpty();
            createChild(includer.file, InIncludedBy, includer.line, definitelyNoChildren);
        }
    }
}

void CppIncludeHierarchyModel::buildHierarchy(const QString &document)
{
    m_editorFilePath = document;
    rootItem()->removeChildren();
    rootItem()->createChild(tr("Includes"), CppIncludeHierarchyItem::InIncludes);
    rootItem()->createChild(tr("Included by"), CppIncludeHierarchyItem::InIncludedBy);
}

void CppIncludeHierarchyModel::setSearching(bool on)
{
    m_searching = on;
    m_seen.clear();
}


// CppIncludeHierarchyModel

CppIncludeHierarchyModel::CppIncludeHierarchyModel()
{
    setRootItem(new CppIncludeHierarchyItem); // FIXME: Remove in 4.2
}

Qt::DropActions CppIncludeHierarchyModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

QStringList CppIncludeHierarchyModel::mimeTypes() const
{
    return DropSupport::mimeTypesForFilePaths();
}

QMimeData *CppIncludeHierarchyModel::mimeData(const QModelIndexList &indexes) const
{
    auto data = new DropMimeData;
    for (const QModelIndex &index : indexes) {
        auto link = index.data(LinkRole).value<Utils::Link>();
        if (link.hasValidTarget())
            data->addFile(link.targetFilePath, link.targetLine, link.targetColumn);
    }
    return data;
}


// CppIncludeHierarchyTreeView

class CppIncludeHierarchyTreeView : public NavigationTreeView
{
public:
    CppIncludeHierarchyTreeView()
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key())
            QAbstractItemView::keyPressEvent(event);
        else
            NavigationTreeView::keyPressEvent(event);
    }
};


// IncludeFinder

class IncludeFinder : public ItemViewFind
{
public:
    IncludeFinder(QAbstractItemView *view, CppIncludeHierarchyModel *model)
        : ItemViewFind(view, Qt::DisplayRole, FetchMoreWhileSearching)
        , m_model(model)
    {}

private:
    Result findIncremental(const QString &txt, FindFlags findFlags) override
    {
        m_model->setSearching(true);
        Result result = ItemViewFind::findIncremental(txt, findFlags);
        m_model->setSearching(false);
        return result;
    }

    Result findStep(const QString &txt, FindFlags findFlags) override
    {
        m_model->setSearching(true);
        Result result = ItemViewFind::findStep(txt, findFlags);
        m_model->setSearching(false);
        return result;
    }

    CppIncludeHierarchyModel *m_model; // Not owned.
};


// CppIncludeHierarchyWidget

class CppIncludeHierarchyWidget : public QWidget
{
    Q_OBJECT

public:
    CppIncludeHierarchyWidget();
    ~CppIncludeHierarchyWidget() override { delete m_treeView; }

    void perform();

    void saveSettings(QSettings *settings, int position);
    void restoreSettings(QSettings *settings, int position);

private:
    void onItemActivated(const QModelIndex &index);
    void editorsClosed(const QList<IEditor *> &editors);
    void showNoIncludeHierarchyLabel();
    void showIncludeHierarchy();
    void syncFromEditorManager();

    CppIncludeHierarchyTreeView *m_treeView = nullptr;
    CppIncludeHierarchyModel m_model;
    AnnotatedItemDelegate m_delegate;
    TextEditorLinkLabel *m_inspectedFile = nullptr;
    QLabel *m_includeHierarchyInfoLabel = nullptr;
    QToolButton *m_toggleSync = nullptr;
    BaseTextEditor *m_editor = nullptr;
    QTimer *m_timer = nullptr;

    // CppIncludeHierarchyFactory needs private members for button access
    friend class CppIncludeHierarchyFactory;
};

CppIncludeHierarchyWidget::CppIncludeHierarchyWidget()
{
    m_delegate.setDelimiter(" ");
    m_delegate.setAnnotationRole(AnnotationRole);

    m_inspectedFile = new TextEditorLinkLabel(this);
    m_inspectedFile->setContentsMargins(5, 5, 5, 5);

    m_treeView = new CppIncludeHierarchyTreeView;
    m_treeView->setModel(&m_model);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setItemDelegate(&m_delegate);
    connect(m_treeView, &QAbstractItemView::activated, this, &CppIncludeHierarchyWidget::onItemActivated);

    m_includeHierarchyInfoLabel = new QLabel(tr("No include hierarchy available"), this);
    m_includeHierarchyInfoLabel->setAlignment(Qt::AlignCenter);
    m_includeHierarchyInfoLabel->setAutoFillBackground(true);
    m_includeHierarchyInfoLabel->setBackgroundRole(QPalette::Base);
    m_includeHierarchyInfoLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout,
            this, &CppIncludeHierarchyWidget::perform);

    m_toggleSync = new QToolButton(this);
    m_toggleSync->setIcon(Utils::Icons::LINK_TOOLBAR.icon());
    m_toggleSync->setCheckable(true);
    m_toggleSync->setToolTip(tr("Synchronize with Editor"));
    connect(m_toggleSync, &QToolButton::clicked,
            this, &CppIncludeHierarchyWidget::syncFromEditorManager);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_inspectedFile);
    layout->addWidget(ItemViewFind::createSearchableWrapper(new IncludeFinder(m_treeView, &m_model)));
    layout->addWidget(m_includeHierarchyInfoLabel);

    connect(CppEditorPlugin::instance(), &CppEditorPlugin::includeHierarchyRequested,
            this, &CppIncludeHierarchyWidget::perform);
    connect(EditorManager::instance(), &EditorManager::editorsClosed,
            this, &CppIncludeHierarchyWidget::editorsClosed);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &CppIncludeHierarchyWidget::syncFromEditorManager);

    syncFromEditorManager();
}

void CppIncludeHierarchyWidget::perform()
{
    showNoIncludeHierarchyLabel();

    m_editor = qobject_cast<CppEditor *>(EditorManager::currentEditor());
    if (!m_editor)
        return;

    const Utils::FilePath documentPath = m_editor->textDocument()->filePath();
    m_model.buildHierarchy(documentPath.toString());

    m_inspectedFile->setText(m_editor->textDocument()->displayName());
    m_inspectedFile->setLink(Utils::Link(documentPath));

    // expand "Includes" and "Included by"
    m_treeView->expand(m_model.index(0, 0));
    m_treeView->expand(m_model.index(1, 0));

    showIncludeHierarchy();
}

const bool kSyncDefault = false;

void CppIncludeHierarchyWidget::saveSettings(QSettings *settings, int position)
{
    const QString key = QString("IncludeHierarchy.%1.SyncWithEditor").arg(position);
    QtcSettings::setValueWithDefault(settings, key, m_toggleSync->isChecked(), kSyncDefault);
}

void CppIncludeHierarchyWidget::restoreSettings(QSettings *settings, int position)
{
    const QString key = QString("IncludeHierarchy.%1.SyncWithEditor").arg(position);
    m_toggleSync->setChecked(settings->value(key, kSyncDefault).toBool());
}

void CppIncludeHierarchyWidget::onItemActivated(const QModelIndex &index)
{
    const auto link = index.data(LinkRole).value<Utils::Link>();
    if (link.hasValidTarget())
        EditorManager::openEditorAt(link, Constants::CPPEDITOR_ID);
}

void CppIncludeHierarchyWidget::editorsClosed(const QList<IEditor *> &editors)
{
    for (const IEditor *editor : editors) {
        if (m_editor == editor)
            perform();
    }
}

void CppIncludeHierarchyWidget::showNoIncludeHierarchyLabel()
{
    m_inspectedFile->hide();
    m_treeView->hide();
    m_includeHierarchyInfoLabel->show();
}

void CppIncludeHierarchyWidget::showIncludeHierarchy()
{
    m_inspectedFile->show();
    m_treeView->show();
    m_includeHierarchyInfoLabel->hide();
}

void CppIncludeHierarchyWidget::syncFromEditorManager()
{
    if (!m_toggleSync->isChecked())
        return;

    auto editor = qobject_cast<CppEditor *>(EditorManager::currentEditor());
    if (!editor)
        return;

    auto document = qobject_cast<CppEditorDocument *>(editor->textDocument());
    if (!document)
        return;

    // Update the hierarchy immediately after a document change. If the
    // document is already parsed, cppDocumentUpdated is not triggered again.
    perform();

    // Use cppDocumentUpdated to catch parsing finished and later file updates.
    // The timer limits the amount of hierarchy updates.
    connect(document, &CppEditorDocument::cppDocumentUpdated,
            m_timer, QOverload<>::of(&QTimer::start),
            Qt::UniqueConnection);
}

// CppIncludeHierarchyFactory

CppIncludeHierarchyFactory::CppIncludeHierarchyFactory()
{
    setDisplayName(tr("Include Hierarchy"));
    setPriority(800);
    setId(Constants::INCLUDE_HIERARCHY_ID);
}

NavigationView CppIncludeHierarchyFactory::createWidget()
{
    auto hierarchyWidget = new CppIncludeHierarchyWidget;
    hierarchyWidget->perform();

    auto stack = new QStackedWidget;
    stack->addWidget(hierarchyWidget);

    NavigationView navigationView;
    navigationView.dockToolBarWidgets << hierarchyWidget->m_toggleSync;
    navigationView.widget = stack;
    return navigationView;
}

static CppIncludeHierarchyWidget *hierarchyWidget(QWidget *widget)
{
    auto stack = qobject_cast<QStackedWidget *>(widget);
    Q_ASSERT(stack);
    auto hierarchyWidget = qobject_cast<CppIncludeHierarchyWidget *>(stack->currentWidget());
    Q_ASSERT(hierarchyWidget);
    return hierarchyWidget;
}

void CppIncludeHierarchyFactory::saveSettings(QtcSettings *settings, int position, QWidget *widget)
{
    hierarchyWidget(widget)->saveSettings(settings, position);
}

void CppIncludeHierarchyFactory::restoreSettings(QSettings *settings, int position, QWidget *widget)
{
    hierarchyWidget(widget)->restoreSettings(settings, position);
}

} // namespace Internal
} // namespace CppEditor

#include "cppincludehierarchy.moc"
