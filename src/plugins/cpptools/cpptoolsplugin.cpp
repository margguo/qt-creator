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

#include "cpptoolsplugin.h"

#include "cppcodemodelsettingspage.h"
#include "cppcodestylesettingspage.h"
#include "cppfilesettingspage.h"
#include "cppmodelmanager.h"
#include "cppprojectfile.h"
#include "cppprojectupdater.h"
#include "cpptoolsbridge.h"
#include "cpptoolsbridgeqtcreatorimplementation.h"
#include "cpptoolsconstants.h"
#include "cpptoolsreuse.h"
#include "cpptoolssettings.h"
#include "projectinfo.h"
#include "stringtable.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/vcsmanager.h>
#include <cppeditor/cppeditorconstants.h>
#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projecttree.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/macroexpander.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include "compileroptionsbuilder_test.h"
#include "cppcodegen_test.h"
#include "cppcompletion_test.h"
#include "cppheadersource_test.h"
#include "cpplocalsymbols_test.h"
#include "cpplocatorfilter_test.h"
#include "cppmodelmanager_test.h"
#include "cpppointerdeclarationformatter_test.h"
#include "cppsourceprocessor_test.h"
#include "functionutils.h"
#include "includeutils.h"
#include "projectinfo_test.h"
#include "senddocumenttracker.h"
#include "symbolsearcher_test.h"
#include "typehierarchybuilder_test.h"
#endif

#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QMenu>
#include <QAction>

using namespace Core;
using namespace CPlusPlus;
using namespace ProjectExplorer;
using namespace Utils;

namespace CppTools {
namespace Internal {

enum { debug = 0 };

static CppToolsPlugin *m_instance = nullptr;
static QHash<QString, QString> m_headerSourceMapping;

class CppToolsPluginPrivate
{
public:
    CppToolsPluginPrivate() {}

    void initialize() { m_codeModelSettings.fromSettings(ICore::settings()); }

    ~CppToolsPluginPrivate()
    {
        ExtensionSystem::PluginManager::removeObject(&m_cppProjectUpdaterFactory);
        delete m_clangdSettingsPage;
    }

    StringTable stringTable;
    CppModelManager modelManager;
    CppCodeModelSettings m_codeModelSettings;
    CppToolsSettings settings;
    CppFileSettings m_fileSettings;
    CppFileSettingsPage m_cppFileSettingsPage{&m_fileSettings};
    CppCodeModelSettingsPage m_cppCodeModelSettingsPage{&m_codeModelSettings};
    ClangdSettingsPage *m_clangdSettingsPage = nullptr;
    CppCodeStyleSettingsPage m_cppCodeStyleSettingsPage;
    CppProjectUpdaterFactory m_cppProjectUpdaterFactory;
};

CppToolsPlugin::CppToolsPlugin()
{
    m_instance = this;
    CppToolsBridge::setCppToolsBridgeImplementation(std::make_unique<CppToolsBridgeQtCreatorImplementation>());
}

CppToolsPlugin::~CppToolsPlugin()
{
    delete d;
    d = nullptr;
    m_instance = nullptr;
}

CppToolsPlugin *CppToolsPlugin::instance()
{
    return m_instance;
}

void CppToolsPlugin::clearHeaderSourceCache()
{
    m_headerSourceMapping.clear();
}

FilePath CppToolsPlugin::licenseTemplatePath()
{
    return FilePath::fromString(m_instance->d->m_fileSettings.licenseTemplatePath);
}

QString CppToolsPlugin::licenseTemplate()
{
    return CppFileSettings::licenseTemplate();
}

bool CppToolsPlugin::usePragmaOnce()
{
    return m_instance->d->m_fileSettings.headerPragmaOnce;
}

const QStringList &CppToolsPlugin::headerSearchPaths()
{
    return m_instance->d->m_fileSettings.headerSearchPaths;
}

const QStringList &CppToolsPlugin::sourceSearchPaths()
{
    return m_instance->d->m_fileSettings.sourceSearchPaths;
}

const QStringList &CppToolsPlugin::headerPrefixes()
{
    return m_instance->d->m_fileSettings.headerPrefixes;
}

const QStringList &CppToolsPlugin::sourcePrefixes()
{
    return m_instance->d->m_fileSettings.sourcePrefixes;
}

bool CppToolsPlugin::initialize(const QStringList &arguments, QString *error)
{
    Q_UNUSED(arguments)
    Q_UNUSED(error)

    d = new CppToolsPluginPrivate;
    d->initialize();

    CppModelManager::instance()->registerJsExtension();
    ExtensionSystem::PluginManager::addObject(&d->m_cppProjectUpdaterFactory);

    // Menus
    ActionContainer *mtools = ActionManager::actionContainer(Core::Constants::M_TOOLS);
    ActionContainer *mcpptools = ActionManager::createMenu(CppTools::Constants::M_TOOLS_CPP);
    QMenu *menu = mcpptools->menu();
    menu->setTitle(tr("&C++"));
    menu->setEnabled(true);
    mtools->addMenu(mcpptools);

    // Actions
    Context context(CppEditor::Constants::CPPEDITOR_ID);

    QAction *switchAction = new QAction(tr("Switch Header/Source"), this);
    Command *command = ActionManager::registerAction(switchAction, Constants::SWITCH_HEADER_SOURCE, context, true);
    command->setDefaultKeySequence(QKeySequence(Qt::Key_F4));
    mcpptools->addAction(command);
    connect(switchAction, &QAction::triggered,
            this, &CppToolsPlugin::switchHeaderSource);

    QAction *openInNextSplitAction = new QAction(tr("Open Corresponding Header/Source in Next Split"), this);
    command = ActionManager::registerAction(openInNextSplitAction, Constants::OPEN_HEADER_SOURCE_IN_NEXT_SPLIT, context, true);
    command->setDefaultKeySequence(QKeySequence(HostOsInfo::isMacHost()
                                                ? tr("Meta+E, F4")
                                                : tr("Ctrl+E, F4")));
    mcpptools->addAction(command);
    connect(openInNextSplitAction, &QAction::triggered,
            this, &CppToolsPlugin::switchHeaderSourceInNextSplit);

    MacroExpander *expander = globalMacroExpander();
    expander->registerVariable("Cpp:LicenseTemplate",
                               tr("The license template."),
                               []() { return CppToolsPlugin::licenseTemplate(); });
    expander->registerFileVariables("Cpp:LicenseTemplatePath",
                                    tr("The configured path to the license template"),
                                    []() { return CppToolsPlugin::licenseTemplatePath(); });

    expander->registerVariable(
                "Cpp:PragmaOnce",
                tr("Insert \"#pragma once\" instead of \"#ifndef\" include guards into header file"),
                [] { return usePragmaOnce() ? QString("true") : QString(); });

    const auto panelFactory = new ProjectPanelFactory;
    panelFactory->setPriority(100);
    panelFactory->setDisplayName(tr("Clangd"));
    panelFactory->setCreateWidgetFunction([](Project *project) {
        return new ClangdProjectSettingsWidget(project);
    });
    ProjectPanelFactory::registerFactory(panelFactory);

    return true;
}

void CppToolsPlugin::extensionsInitialized()
{
    // The Cpp editor plugin, which is loaded later on, registers the Cpp mime types,
    // so, apply settings here
    d->m_fileSettings.fromSettings(ICore::settings());
    if (!d->m_fileSettings.applySuffixesToMimeDB())
        qWarning("Unable to apply cpp suffixes to mime database (cpp mime types not found).\n");
    if (CppModelManager::instance()->isClangCodeModelActive())
        d->m_clangdSettingsPage = new ClangdSettingsPage;
}

QVector<QObject *> CppToolsPlugin::createTestObjects() const
{
    return {
#ifdef WITH_TESTS
        new CodegenTest,
        new CompilerOptionsBuilderTest,
        new CompletionTest,
        new FunctionUtilsTest,
        new HeaderPathFilterTest,
        new HeaderSourceTest,
        new IncludeGroupsTest,
        new LocalSymbolsTest,
        new LocatorFilterTest,
        new ModelManagerTest,
        new PointerDeclarationFormatterTest,
        new ProjectFileCategorizerTest,
        new ProjectInfoGeneratorTest,
        new ProjectPartChooserTest,
        new DocumentTrackerTest,
        new SourceProcessorTest,
        new SymbolSearcherTest,
        new TypeHierarchyBuilderTest,
#endif
    };
}

CppCodeModelSettings *CppToolsPlugin::codeModelSettings()
{
    return &d->m_codeModelSettings;
}

CppFileSettings *CppToolsPlugin::fileSettings()
{
    return &instance()->d->m_fileSettings;
}

void CppToolsPlugin::switchHeaderSource()
{
    CppTools::switchHeaderSource();
}

void CppToolsPlugin::switchHeaderSourceInNextSplit()
{
    QString otherFile = correspondingHeaderOrSource(
                EditorManager::currentDocument()->filePath().toString());
    if (!otherFile.isEmpty())
        EditorManager::openEditor(otherFile, Id(), EditorManager::OpenInOtherSplit);
}

static QStringList findFilesInProject(const QString &name, const Project *project)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << name << project;

    if (!project)
        return QStringList();

    QString pattern = QString(1, QLatin1Char('/'));
    pattern += name;
    const QStringList projectFiles
            = transform(project->files(Project::AllFiles), &FilePath::toString);
    const QStringList::const_iterator pcend = projectFiles.constEnd();
    QStringList candidateList;
    for (QStringList::const_iterator it = projectFiles.constBegin(); it != pcend; ++it) {
        if (it->endsWith(pattern, HostOsInfo::fileNameCaseSensitivity()))
            candidateList.append(*it);
    }
    return candidateList;
}

// Return the suffixes that should be checked when trying to find a
// source belonging to a header and vice versa
static QStringList matchingCandidateSuffixes(ProjectFile::Kind kind)
{
    switch (kind) {
    case ProjectFile::AmbiguousHeader:
    case ProjectFile::CHeader:
    case ProjectFile::CXXHeader:
    case ProjectFile::ObjCHeader:
    case ProjectFile::ObjCXXHeader:
        return mimeTypeForName(Constants::C_SOURCE_MIMETYPE).suffixes()
                + mimeTypeForName(Constants::CPP_SOURCE_MIMETYPE).suffixes()
                + mimeTypeForName(Constants::OBJECTIVE_C_SOURCE_MIMETYPE).suffixes()
                + mimeTypeForName(Constants::OBJECTIVE_CPP_SOURCE_MIMETYPE).suffixes()
                + mimeTypeForName(Constants::CUDA_SOURCE_MIMETYPE).suffixes();
    case ProjectFile::CSource:
    case ProjectFile::ObjCSource:
        return mimeTypeForName(Constants::C_HEADER_MIMETYPE).suffixes();
    case ProjectFile::CXXSource:
    case ProjectFile::ObjCXXSource:
    case ProjectFile::CudaSource:
    case ProjectFile::OpenCLSource:
        return mimeTypeForName(Constants::CPP_HEADER_MIMETYPE).suffixes();
    default:
        return QStringList();
    }
}

static QStringList baseNameWithAllSuffixes(const QString &baseName, const QStringList &suffixes)
{
    QStringList result;
    const QChar dot = QLatin1Char('.');
    for (const QString &suffix : suffixes) {
        QString fileName = baseName;
        fileName += dot;
        fileName += suffix;
        result += fileName;
    }
    return result;
}

static QStringList baseNamesWithAllPrefixes(const QStringList &baseNames, bool isHeader)
{
    QStringList result;
    const QStringList &sourcePrefixes = m_instance->sourcePrefixes();
    const QStringList &headerPrefixes = m_instance->headerPrefixes();

    for (const QString &name : baseNames) {
        for (const QString &prefix : isHeader ? headerPrefixes : sourcePrefixes) {
            if (name.startsWith(prefix)) {
                QString nameWithoutPrefix = name.mid(prefix.size());
                result += nameWithoutPrefix;
                for (const QString &prefix : isHeader ? sourcePrefixes : headerPrefixes)
                    result += prefix + nameWithoutPrefix;
            }
        }
        for (const QString &prefix : isHeader ? sourcePrefixes : headerPrefixes)
            result += prefix + name;

    }
    return result;
}

static QStringList baseDirWithAllDirectories(const QDir &baseDir, const QStringList &directories)
{
    QStringList result;
    for (const QString &dir : directories)
        result << QDir::cleanPath(baseDir.absoluteFilePath(dir));
    return result;
}

static int commonFilePathLength(const QString &s1, const QString &s2)
{
    int length = qMin(s1.length(), s2.length());
    for (int i = 0; i < length; ++i)
        if (HostOsInfo::fileNameCaseSensitivity() == Qt::CaseSensitive) {
            if (s1[i] != s2[i])
                return i;
        } else {
            if (s1[i].toLower() != s2[i].toLower())
                return i;
        }
    return length;
}

static QString correspondingHeaderOrSourceInProject(const QFileInfo &fileInfo,
                                                    const QStringList &candidateFileNames,
                                                    const Project *project,
                                                    CacheUsage cacheUsage)
{
    QString bestFileName;
    int compareValue = 0;
    const QString filePath = fileInfo.filePath();
    for (const QString &candidateFileName : candidateFileNames) {
        const QStringList projectFiles = findFilesInProject(candidateFileName, project);
        // Find the file having the most common path with fileName
        for (const QString &projectFile : projectFiles) {
            int value = commonFilePathLength(filePath, projectFile);
            if (value > compareValue) {
                compareValue = value;
                bestFileName = projectFile;
            }
        }
    }
    if (!bestFileName.isEmpty()) {
        const QFileInfo candidateFi(bestFileName);
        QTC_ASSERT(candidateFi.isFile(), return QString());
        if (cacheUsage == CacheUsage::ReadWrite) {
            m_headerSourceMapping[fileInfo.absoluteFilePath()] = candidateFi.absoluteFilePath();
            m_headerSourceMapping[candidateFi.absoluteFilePath()] = fileInfo.absoluteFilePath();
        }
        return candidateFi.absoluteFilePath();
    }

    return QString();
}

} // namespace Internal

QString correspondingHeaderOrSource(const QString &fileName, bool *wasHeader, CacheUsage cacheUsage)
{
    using namespace Internal;

    const QFileInfo fi(fileName);
    ProjectFile::Kind kind = ProjectFile::classify(fileName);
    const bool isHeader = ProjectFile::isHeader(kind);
    if (wasHeader)
        *wasHeader = isHeader;
    if (m_headerSourceMapping.contains(fi.absoluteFilePath()))
        return m_headerSourceMapping.value(fi.absoluteFilePath());

    if (debug)
        qDebug() << Q_FUNC_INFO << fileName <<  kind;

    if (kind == ProjectFile::Unsupported)
        return QString();

    const QString baseName = fi.completeBaseName();
    const QString privateHeaderSuffix = QLatin1String("_p");
    const QStringList suffixes = matchingCandidateSuffixes(kind);

    QStringList candidateFileNames = baseNameWithAllSuffixes(baseName, suffixes);
    if (isHeader) {
        if (baseName.endsWith(privateHeaderSuffix)) {
            QString sourceBaseName = baseName;
            sourceBaseName.truncate(sourceBaseName.size() - privateHeaderSuffix.size());
            candidateFileNames += baseNameWithAllSuffixes(sourceBaseName, suffixes);
        }
    } else {
        QString privateHeaderBaseName = baseName;
        privateHeaderBaseName.append(privateHeaderSuffix);
        candidateFileNames += baseNameWithAllSuffixes(privateHeaderBaseName, suffixes);
    }

    const QDir absoluteDir = fi.absoluteDir();
    QStringList candidateDirs(absoluteDir.absolutePath());
    // If directory is not root, try matching against its siblings
    const QStringList searchPaths = isHeader ? m_instance->sourceSearchPaths()
                                             : m_instance->headerSearchPaths();
    candidateDirs += baseDirWithAllDirectories(absoluteDir, searchPaths);

    candidateFileNames += baseNamesWithAllPrefixes(candidateFileNames, isHeader);

    // Try to find a file in the same or sibling directories first
    for (const QString &candidateDir : qAsConst(candidateDirs)) {
        for (const QString &candidateFileName : qAsConst(candidateFileNames)) {
            const QString candidateFilePath = candidateDir + QLatin1Char('/') + candidateFileName;
            const QString normalized = FileUtils::normalizedPathName(candidateFilePath);
            const QFileInfo candidateFi(normalized);
            if (candidateFi.isFile()) {
                if (cacheUsage == CacheUsage::ReadWrite) {
                    m_headerSourceMapping[fi.absoluteFilePath()] = candidateFi.absoluteFilePath();
                    if (!isHeader || !baseName.endsWith(privateHeaderSuffix))
                        m_headerSourceMapping[candidateFi.absoluteFilePath()] = fi.absoluteFilePath();
                }
                return candidateFi.absoluteFilePath();
            }
        }
    }

    // Find files in the current project
    Project *currentProject = ProjectTree::currentProject();
    if (currentProject) {
        const QString path = correspondingHeaderOrSourceInProject(fi, candidateFileNames,
                                                                  currentProject, cacheUsage);
        if (!path.isEmpty())
            return path;

    // Find files in other projects
    } else {
        CppModelManager *modelManager = CppModelManager::instance();
        const QList<ProjectInfo::ConstPtr> projectInfos = modelManager->projectInfos();
        for (const ProjectInfo::ConstPtr &projectInfo : projectInfos) {
            const Project *project = projectForProjectInfo(*projectInfo);
            if (project == currentProject)
                continue; // We have already checked the current project.

            const QString path = correspondingHeaderOrSourceInProject(fi, candidateFileNames,
                                                                      project, cacheUsage);
            if (!path.isEmpty())
                return path;
        }
    }

    return QString();
}

} // namespace CppTools
