/****************************************************************************
**
** Copyright (C) 2016 Orgad Shaneh <orgads@gmail.com>.
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

#include "gitgrep.h"
#include "gitclient.h"
#include "gitconstants.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/vcsmanager.h>
#include <texteditor/findinfiles.h>
#include <vcsbase/vcscommand.h>
#include <vcsbase/vcsbaseconstants.h>

#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/filesearch.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/runextensions.h>
#include <utils/textfileformat.h>

#include <QCheckBox>
#include <QFuture>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QRegularExpressionValidator>
#include <QScopedPointer>
#include <QSettings>
#include <QTextStream>

namespace Git {
namespace Internal {

class GitGrepParameters
{
public:
    QString ref;
    bool recurseSubmodules = false;
    QString id() const { return recurseSubmodules ? ref + ".Rec" : ref; }
};

using namespace Core;
using namespace Utils;
using VcsBase::VcsCommand;

namespace {

const char GitGrepRef[] = "GitGrepRef";

class GitGrepRunner
{
    using FutureInterfaceType = QFutureInterface<FileSearchResultList>;

public:
    GitGrepRunner(const TextEditor::FileFindParameters &parameters)
        : m_parameters(parameters)
    {
        m_directory = FilePath::fromString(parameters.additionalParameters.toString());
        m_command.reset(GitClient::instance()->createCommand(m_directory));
        m_vcsBinary = GitClient::instance()->vcsBinary();
    }

    struct Match
    {
        Match() = default;
        Match(int start, int length) :
            matchStart(start), matchLength(length) {}

        int matchStart = 0;
        int matchLength = 0;
        QStringList regexpCapturedTexts;
    };

    void processLine(const QString &line, FileSearchResultList *resultList) const
    {
        if (line.isEmpty())
            return;
        static const QLatin1String boldRed("\x1b[1;31m");
        static const QLatin1String resetColor("\x1b[m");
        FileSearchResult single;
        const int lineSeparator = line.indexOf(QChar::Null);
        QString filePath = line.left(lineSeparator);
        if (!m_ref.isEmpty() && filePath.startsWith(m_ref))
            filePath.remove(0, m_ref.length());
        single.fileName = m_directory.pathAppended(filePath).toString();
        const int textSeparator = line.indexOf(QChar::Null, lineSeparator + 1);
        single.lineNumber = line.mid(lineSeparator + 1, textSeparator - lineSeparator - 1).toInt();
        QString text = line.mid(textSeparator + 1);
        QRegularExpression regexp;
        QVector<Match> matches;
        if (m_parameters.flags & FindRegularExpression) {
            const QRegularExpression::PatternOptions patternOptions =
                    (m_parameters.flags & FindCaseSensitively)
                    ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
            regexp.setPattern(m_parameters.text);
            regexp.setPatternOptions(patternOptions);
        }
        for (;;) {
            const int matchStart = text.indexOf(boldRed);
            if (matchStart == -1)
                break;
            const int matchTextStart = matchStart + boldRed.size();
            const int matchEnd = text.indexOf(resetColor, matchTextStart);
            QTC_ASSERT(matchEnd != -1, break);
            const int matchLength = matchEnd - matchTextStart;
            Match match(matchStart, matchLength);
            const QString matchText = text.mid(matchTextStart, matchLength);
            if (m_parameters.flags & FindRegularExpression)
                match.regexpCapturedTexts = regexp.match(matchText).capturedTexts();
            matches.append(match);
            text = text.left(matchStart) + matchText + text.mid(matchEnd + resetColor.size());
        }
        single.matchingLine = text;

        for (const auto &match : qAsConst(matches)) {
            single.matchStart = match.matchStart;
            single.matchLength = match.matchLength;
            single.regexpCapturedTexts = match.regexpCapturedTexts;
            resultList->append(single);
        }
    }

    void read(FutureInterfaceType &fi, const QString &text)
    {
        FileSearchResultList resultList;
        QString t = text;
        QTextStream stream(&t);
        while (!stream.atEnd() && !fi.isCanceled())
            processLine(stream.readLine(), &resultList);
        if (!resultList.isEmpty())
            fi.reportResult(resultList);
    }

    void operator()(FutureInterfaceType &fi)
    {
        Core::ProgressTimer progress(fi, 5);
        QStringList arguments = {
            "-c", "color.grep.match=bold red",
            "-c", "color.grep=always",
            "grep", "-zn", "--no-full-name"
        };
        if (!(m_parameters.flags & FindCaseSensitively))
            arguments << "-i";
        if (m_parameters.flags & FindWholeWords)
            arguments << "-w";
        if (m_parameters.flags & FindRegularExpression)
            arguments << "-P";
        else
            arguments << "-F";
        arguments << "-e" << m_parameters.text;
        GitGrepParameters params = m_parameters.searchEngineParameters.value<GitGrepParameters>();
        if (params.recurseSubmodules)
            arguments << "--recurse-submodules";
        if (!params.ref.isEmpty()) {
            arguments << params.ref;
            m_ref = params.ref + ':';
        }
        const QStringList filterArgs =
                m_parameters.nameFilters.isEmpty() ? QStringList("*") // needed for exclusion filters
                                                   : m_parameters.nameFilters;
        const QStringList exclusionArgs =
                Utils::transform(m_parameters.exclusionFilters, [](const QString &filter) {
                    return QString(":!" + filter);
                });
        arguments << "--" << filterArgs << exclusionArgs;
        m_command->addFlags(VcsCommand::SilentOutput | VcsCommand::SuppressFailMessage);
        m_command->setProgressiveOutput(true);
        QFutureWatcher<FileSearchResultList> watcher;
        QObject::connect(&watcher,
                         &QFutureWatcher<FileSearchResultList>::canceled,
                         m_command.get(),
                         &VcsCommand::cancel);
        watcher.setFuture(fi.future());
        QObject::connect(m_command.get(),
                         &VcsCommand::stdOutText,
                         [this, &fi](const QString &text) { read(fi, text); });
        QtcProcess proc;
        proc.setTimeoutS(0);
        m_command->runCommand(proc, {m_vcsBinary, arguments});
        switch (proc.result()) {
        case QtcProcess::TerminatedAbnormally:
        case QtcProcess::StartFailed:
        case QtcProcess::Hang:
            fi.reportCanceled();
            break;
        case QtcProcess::FinishedWithSuccess:
        case QtcProcess::FinishedWithError:
            // When no results are found, git-grep exits with non-zero status.
            // Do not consider this as an error.
            break;
        }
    }

private:
    FilePath m_vcsBinary;
    FilePath m_directory;
    QString m_ref;
    TextEditor::FileFindParameters m_parameters;
    std::unique_ptr<VcsCommand> m_command;
};

} // namespace

static bool isGitDirectory(const FilePath &path)
{
    static IVersionControl *gitVc = VcsManager::versionControl(VcsBase::Constants::VCS_ID_GIT);
    QTC_ASSERT(gitVc, return false);
    return gitVc == VcsManager::findVersionControlForDirectory(path, nullptr);
}

GitGrep::GitGrep(GitClient *client)
    : m_client(client)
{
    m_widget = new QWidget;
    auto layout = new QHBoxLayout(m_widget);
    layout->setContentsMargins(0, 0, 0, 0);
    m_treeLineEdit = new FancyLineEdit;
    m_treeLineEdit->setPlaceholderText(tr("Tree (optional)"));
    m_treeLineEdit->setToolTip(tr("Can be HEAD, tag, local or remote branch, or a commit hash.\n"
                                  "Leave empty to search through the file system."));
    const QRegularExpression refExpression("[\\S]*");
    m_treeLineEdit->setValidator(new QRegularExpressionValidator(refExpression, this));
    layout->addWidget(m_treeLineEdit);
    if (client->gitVersion() >= 0x021300) {
        m_recurseSubmodules = new QCheckBox(tr("Recurse submodules"));
        layout->addWidget(m_recurseSubmodules);
    }
    TextEditor::FindInFiles *findInFiles = TextEditor::FindInFiles::instance();
    QTC_ASSERT(findInFiles, return);
    connect(findInFiles, &TextEditor::FindInFiles::pathChanged,
            m_widget, [this](const QString &path) {
        setEnabled(isGitDirectory(FilePath::fromString(path)));
    });
    connect(this, &SearchEngine::enabledChanged, m_widget, &QWidget::setEnabled);
    findInFiles->addSearchEngine(this);
}

GitGrep::~GitGrep()
{
    delete m_widget;
}

QString GitGrep::title() const
{
    return tr("Git Grep");
}

QString GitGrep::toolTip() const
{
    const QString ref = m_treeLineEdit->text();
    if (!ref.isEmpty())
        return tr("Ref: %1\n%2").arg(ref);
    return QLatin1String("%1");
}

QWidget *GitGrep::widget() const
{
    return m_widget;
}

QVariant GitGrep::parameters() const
{
    GitGrepParameters params;
    params.ref = m_treeLineEdit->text();
    if (m_recurseSubmodules)
        params.recurseSubmodules = m_recurseSubmodules->isChecked();
    return QVariant::fromValue(params);
}

void GitGrep::readSettings(QSettings *settings)
{
    m_treeLineEdit->setText(settings->value(GitGrepRef).toString());
}

void GitGrep::writeSettings(QSettings *settings) const
{
    settings->setValue(GitGrepRef, m_treeLineEdit->text());
}

QFuture<FileSearchResultList> GitGrep::executeSearch(const TextEditor::FileFindParameters &parameters,
        TextEditor::BaseFileFind * /*baseFileFind*/)
{
    return Utils::runAsync(GitGrepRunner(parameters));
}

IEditor *GitGrep::openEditor(const SearchResultItem &item,
                             const TextEditor::FileFindParameters &parameters)
{
    GitGrepParameters params = parameters.searchEngineParameters.value<GitGrepParameters>();
    if (params.ref.isEmpty() || item.path().isEmpty())
        return nullptr;
    const QString path = QDir::fromNativeSeparators(item.path().first());
    const FilePath topLevel = FilePath::fromString(parameters.additionalParameters.toString());
    IEditor *editor = m_client->openShowEditor(
                topLevel, params.ref, path, GitClient::ShowEditor::OnlyIfDifferent);
    if (editor)
        editor->gotoLine(item.mainRange().begin.line, item.mainRange().begin.column);
    return editor;
}

} // Internal
} // Git

Q_DECLARE_METATYPE(Git::Internal::GitGrepParameters)
