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

#include "quicktestparser.h"
#include "quicktestframework.h"
#include "quicktesttreeitem.h"
#include "quicktestvisitors.h"
#include "quicktest_utils.h"
#include "../testcodeparser.h"
#include "../testtreemodel.h"

#include <cpptools/cppmodelmanager.h>
#include <cpptools/projectpart.h>
#include <projectexplorer/session.h>
#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/qmljsdialect.h>
#include <qmljstools/qmljsmodelmanager.h>
#include <utils/hostosinfo.h>
#include <utils/algorithm.h>
#include <utils/qtcassert.h>

using namespace QmlJS;

namespace Autotest {
namespace Internal {

TestTreeItem *QuickTestParseResult::createTestTreeItem() const
{
    if (itemType == TestTreeItem::Root || itemType == TestTreeItem::TestDataTag)
        return nullptr;

    QuickTestTreeItem *item = new QuickTestTreeItem(framework, name, fileName, itemType);
    item->setProFile(proFile);
    item->setLine(line);
    item->setColumn(column);
    for (const TestParseResult *funcResult : children)
        item->appendChild(funcResult->createTestTreeItem());
    return item;
}

static bool includesQtQuickTest(const CPlusPlus::Document::Ptr &doc,
                                const CPlusPlus::Snapshot &snapshot)
{
    static QStringList expectedHeaderPrefixes
            = Utils::HostOsInfo::isMacHost()
            ? QStringList({"QtQuickTest.framework/Headers", "QtQuickTest"})
            : QStringList({"QtQuickTest"});

    const QList<CPlusPlus::Document::Include> includes = doc->resolvedIncludes();

    for (const CPlusPlus::Document::Include &inc : includes) {
        if (inc.unresolvedFileName() == "QtQuickTest/quicktest.h") {
            for (const QString &prefix : expectedHeaderPrefixes) {
                if (inc.resolvedFileName().endsWith(
                            QString("%1/quicktest.h").arg(prefix))) {
                    return true;
                }
            }
        }
    }

    for (const QString &include : snapshot.allIncludesForDocument(doc->fileName())) {
        for (const QString &prefix : expectedHeaderPrefixes) {
            if (include.endsWith(QString("%1/quicktest.h").arg(prefix)))
                return true;
        }
    }

    for (const QString &prefix : expectedHeaderPrefixes) {
        if (CppParser::precompiledHeaderContains(snapshot,
                                                 Utils::FilePath::fromString(doc->fileName()),
                                                 QString("%1/quicktest.h").arg(prefix))) {
            return true;
        }
    }
    return false;
}

static QString quickTestSrcDir(const CppTools::CppModelManager *cppMM,
                               const Utils::FilePath &fileName)
{
    const QList<CppTools::ProjectPart::ConstPtr> parts = cppMM->projectPart(fileName);
    if (parts.size() > 0) {
        const ProjectExplorer::Macros &macros = parts.at(0)->projectMacros;
        auto found = std::find_if(
                    macros.cbegin(),
                    macros.cend(),
                    [] (const ProjectExplorer::Macro &macro) { return macro.key == "QUICK_TEST_SOURCE_DIR"; });
       if (found != macros.cend())  {
           QByteArray result = found->value;
           if (result.startsWith('"'))
               result.remove(result.length() - 1, 1).remove(0, 1);
           if (result.startsWith("\\\""))
               result.remove(result.length() - 2, 2).remove(0, 2);
           return QLatin1String(result);
       }
    }
    return QString();
}

QString QuickTestParser::quickTestName(const CPlusPlus::Document::Ptr &doc) const
{
    const QList<CPlusPlus::Document::MacroUse> macros = doc->macroUses();
    const Utils::FilePath filePath = Utils::FilePath::fromString(doc->fileName());

    for (const CPlusPlus::Document::MacroUse &macro : macros) {
        if (!macro.isFunctionLike())
            continue;
        const QByteArray name = macro.macro().name();
        if (QuickTestUtils::isQuickTestMacro(name)) {
            CPlusPlus::Document::Block arg = macro.arguments().at(0);
            return QLatin1String(getFileContent(filePath)
                                 .mid(int(arg.bytesBegin()), int(arg.bytesEnd() - arg.bytesBegin())));
        }
    }


    const QByteArray &fileContent = getFileContent(filePath);
    // check for using quick_test_main() directly
    CPlusPlus::Document::Ptr document = m_cppSnapshot.preprocessedDocument(fileContent, filePath);
    if (document.isNull())
        return QString();
    document->check();
    CPlusPlus::AST *ast = document->translationUnit()->ast();
    QuickTestAstVisitor astVisitor(document, m_cppSnapshot);
    astVisitor.accept(ast);
    if (!astVisitor.testBaseName().isEmpty())
        return astVisitor.testBaseName();

    // check for precompiled headers
    static QStringList expectedHeaderPrefixes
            = Utils::HostOsInfo::isMacHost()
            ? QStringList({"QtQuickTest.framework/Headers", "QtQuickTest"})
            : QStringList({"QtQuickTest"});
    bool pchIncludes = false;
    for (const QString &prefix : expectedHeaderPrefixes) {
        if (CppParser::precompiledHeaderContains(m_cppSnapshot, filePath,
                                                 QString("%1/quicktest.h").arg(prefix))) {
            pchIncludes = true;
            break;
        }
    }

    if (pchIncludes) {
        const QRegularExpression regex("\\bQUICK_TEST_(MAIN|OPENGL_MAIN|MAIN_WITH_SETUP)");
        const QRegularExpressionMatch match = regex.match(QString::fromUtf8(fileContent));
        if (match.hasMatch())
            return match.captured(); // we do not care for the name, just return something non-empty
    }
    return {};
}

QList<Document::Ptr> QuickTestParser::scanDirectoryForQuickTestQmlFiles(const QString &srcDir)
{
    QStringList dirs(srcDir);
    ModelManagerInterface *qmlJsMM = QmlJSTools::Internal::ModelManager::instance();
    // make sure even files not listed in pro file are available inside the snapshot
    QFutureInterface<void> future;
    PathsAndLanguages paths;
    paths.maybeInsert(Utils::FilePath::fromString(srcDir), Dialect::Qml);
    ModelManagerInterface::importScan(future, ModelManagerInterface::workingCopy(), paths, qmlJsMM,
        false /*emitDocumentChanges*/, false /*onlyTheLib*/, true /*forceRescan*/ );

    const Snapshot snapshot = QmlJSTools::Internal::ModelManager::instance()->snapshot();
    QDirIterator it(srcDir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi(it.fileInfo().canonicalFilePath());
        dirs.append(fi.filePath());
    }
    QMetaObject::invokeMethod(this, [this, dirs] { QuickTestParser::doUpdateWatchPaths(dirs); },
                              Qt::QueuedConnection);

    QList<Document::Ptr> foundDocs;

    for (const QString &path : qAsConst(dirs)) {
        const QList<Document::Ptr> docs = snapshot.documentsInDirectory(path);
        for (const Document::Ptr &doc : docs) {
            const QFileInfo fi(doc->fileName());
            // using working copy above might provide no more existing files
            if (!fi.exists())
                continue;
            const QString fileName(fi.fileName());
            if (fileName.startsWith("tst_") && fileName.endsWith(".qml"))
                foundDocs << doc;
        }
    }

    return foundDocs;
}

static bool checkQmlDocumentForQuickTestCode(QFutureInterface<TestParseResultPtr> futureInterface,
                                             const Document::Ptr &qmlJSDoc,
                                             ITestFramework *framework,
                                             const Utils::FilePath &proFile = Utils::FilePath())
{
    if (qmlJSDoc.isNull())
        return false;
    AST::Node *ast = qmlJSDoc->ast();
    QTC_ASSERT(ast, return false);
    Snapshot snapshot = ModelManagerInterface::instance()->snapshot();
    TestQmlVisitor qmlVisitor(qmlJSDoc, snapshot);
    AST::Node::accept(ast, &qmlVisitor);
    if (!qmlVisitor.isValid())
        return false;

    const QVector<QuickTestCaseSpec> &testCases = qmlVisitor.testCases();

    for (const QuickTestCaseSpec &testCase : testCases) {
        const QString testCaseName = testCase.m_caseName;

        QuickTestParseResult *parseResult = new QuickTestParseResult(framework);
        parseResult->proFile = proFile;
        parseResult->itemType = TestTreeItem::TestCase;
        if (!testCaseName.isEmpty()) {
            parseResult->fileName = testCase.m_locationAndType.m_filePath;
            parseResult->name = testCaseName;
            parseResult->line = testCase.m_locationAndType.m_line;
            parseResult->column = testCase.m_locationAndType.m_column;
        }

        for (const auto &function : testCase.m_functions) {
            QuickTestParseResult *funcResult = new QuickTestParseResult(framework);
            funcResult->name = function.m_name;
            funcResult->displayName = function.m_name;
            funcResult->itemType = function.m_type;
            funcResult->fileName = function.m_filePath;
            funcResult->line = function.m_line;
            funcResult->column = function.m_column;
            funcResult->proFile = proFile;

            parseResult->children.append(funcResult);
        }

        futureInterface.reportResult(TestParseResultPtr(parseResult));
    }
    return true;
}

bool QuickTestParser::handleQtQuickTest(QFutureInterface<TestParseResultPtr> futureInterface,
                                        CPlusPlus::Document::Ptr document,
                                        ITestFramework *framework)
{
    const CppTools::CppModelManager *modelManager = CppTools::CppModelManager::instance();
    if (quickTestName(document).isEmpty())
        return false;

    QList<CppTools::ProjectPart::ConstPtr> ppList = modelManager->projectPart(document->fileName());
    if (ppList.isEmpty()) // happens if shutting down while parsing
        return false;
    const Utils::FilePath cppFileName = Utils::FilePath::fromString(document->fileName());
    const Utils::FilePath proFile = Utils::FilePath::fromString(ppList.at(0)->projectFile);
    m_mainCppFiles.insert(cppFileName, proFile);
    const QString srcDir = quickTestSrcDir(modelManager, cppFileName);
    if (srcDir.isEmpty())
        return false;

    if (futureInterface.isCanceled())
        return false;
    const QList<Document::Ptr> qmlDocs = scanDirectoryForQuickTestQmlFiles(srcDir);
    bool result = false;
    for (const Document::Ptr &qmlJSDoc : qmlDocs) {
        if (futureInterface.isCanceled())
            break;
        result |= checkQmlDocumentForQuickTestCode(futureInterface, qmlJSDoc, framework, proFile);
    }
    return result;
}

static QMap<QString, QDateTime> qmlFilesWithMTime(const QString &directory)
{
    const QFileInfoList &qmlFiles = QDir(directory).entryInfoList({ "*.qml" },
                                                                  QDir::Files, QDir::Name);
    QMap<QString, QDateTime> filesAndDates;
    for (const QFileInfo &info : qmlFiles)
        filesAndDates.insert(info.fileName(), info.lastModified());
    return filesAndDates;
}

void QuickTestParser::handleDirectoryChanged(const QString &directory)
{
    const QMap<QString, QDateTime> &filesAndDates = qmlFilesWithMTime(directory);
    const QMap<QString, QDateTime> &watched = m_watchedFiles.value(directory);
    const QList<QString> &keys = watched.keys();
    if (filesAndDates.keys() != keys) { // removed or added files
        m_watchedFiles[directory] = filesAndDates;
        TestTreeModel::instance()->parser()->emitUpdateTestTree(this);
    } else { // we might still have different timestamps
        const bool timestampChanged = Utils::anyOf(keys, [&](const QString &file) {
            return filesAndDates.value(file) != watched.value(file);
        });
        if (timestampChanged) {
            PathsAndLanguages paths;
            paths.maybeInsert(Utils::FilePath::fromString(directory), Dialect::Qml);
            QFutureInterface<void> future;
            ModelManagerInterface *qmlJsMM = ModelManagerInterface::instance();
            ModelManagerInterface::importScan(future, ModelManagerInterface::workingCopy(), paths,
                                              qmlJsMM,
                                              true /*emitDocumentChanges*/,
                                              false /*onlyTheLib*/,
                                              true /*forceRescan*/ );
        }
    }
}

void QuickTestParser::doUpdateWatchPaths(const QStringList &directories)
{
    for (const QString &dir : directories) {
        m_directoryWatcher.addPath(dir);
        m_watchedFiles[dir] = qmlFilesWithMTime(dir);
    }
}

QuickTestParser::QuickTestParser(ITestFramework *framework)
    : CppParser(framework)
{
    connect(ProjectExplorer::SessionManager::instance(),
            &ProjectExplorer::SessionManager::startupProjectChanged, [this] {
        const QStringList &dirs = m_directoryWatcher.directories();
        if (!dirs.isEmpty())
            m_directoryWatcher.removePaths(dirs);
        m_watchedFiles.clear();
    });
    connect(&m_directoryWatcher, &QFileSystemWatcher::directoryChanged,
            this, &QuickTestParser::handleDirectoryChanged);
}

void QuickTestParser::init(const Utils::FilePaths &filesToParse, bool fullParse)
{
    m_qmlSnapshot = QmlJSTools::Internal::ModelManager::instance()->snapshot();
    if (!fullParse) {
        // in a full parse we get the correct entry points by the respective main
        m_proFilesForQmlFiles = QuickTestUtils::proFilesForQmlFiles(framework(), filesToParse);
        // get rid of cached main cpp files that are going to get processed anyhow
        for (const Utils::FilePath &file : filesToParse) {
            if (m_mainCppFiles.contains(file)) {
                m_mainCppFiles.remove(file);
                if (m_mainCppFiles.isEmpty())
                    break;
            }
        }
    } else {
        // get rid of all cached main cpp files
        m_mainCppFiles.clear();
    }
    CppParser::init(filesToParse, fullParse);
}

void QuickTestParser::release()
{
    m_qmlSnapshot = Snapshot();
    m_proFilesForQmlFiles.clear();
    CppParser::release();
}

bool QuickTestParser::processDocument(QFutureInterface<TestParseResultPtr> futureInterface,
                                      const Utils::FilePath &fileName)
{
    if (fileName.endsWith(".qml")) {
        const Utils::FilePath &proFile = m_proFilesForQmlFiles.value(fileName);
        if (proFile.isEmpty())
            return false;
        Document::Ptr qmlJSDoc = m_qmlSnapshot.document(fileName.toString());
        return checkQmlDocumentForQuickTestCode(futureInterface, qmlJSDoc, framework(), proFile);
    }
    if (!m_cppSnapshot.contains(fileName) || !selectedForBuilding(fileName))
        return false;
    CPlusPlus::Document::Ptr document = m_cppSnapshot.find(fileName).value();
    if (!includesQtQuickTest(document, m_cppSnapshot))
        return false;
    return handleQtQuickTest(futureInterface, document, framework());
}

Utils::FilePath QuickTestParser::projectFileForMainCppFile(const Utils::FilePath &fileName) const
{
    return m_mainCppFiles.contains(fileName) ? m_mainCppFiles.value(fileName) : Utils::FilePath();
}

} // namespace Internal
} // namespace Autotest
