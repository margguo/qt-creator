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

#include "coreplugin.h"
#include "designmode.h"
#include "editmode.h"
#include "helpmanager.h"
#include "icore.h"
#include "idocument.h"
#include "iwizardfactory.h"
#include "mainwindow.h"
#include "modemanager.h"
#include "reaper_p.h"
#include "themechooser.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/findplugin.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/locator/locator.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/fileutils.h>

#include <app/app_version.h>
#include <extensionsystem/pluginerroroverview.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/commandline.h>
#include <utils/infobar.h>
#include <utils/macroexpander.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/pathchooser.h>
#include <utils/savefile.h>
#include <utils/stringutils.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QUuid>

#include <cstdlib>

using namespace Core;
using namespace Core::Internal;
using namespace Utils;

static CorePlugin *m_instance = nullptr;

const char kWarnCrashReportingSetting[] = "WarnCrashReporting";
const char kEnvironmentChanges[] = "Core/EnvironmentChanges";

void CorePlugin::setupSystemEnvironment()
{
    m_instance->m_startupSystemEnvironment = Environment::systemEnvironment();
    const EnvironmentItems changes = EnvironmentItem::fromStringList(
        ICore::settings()->value(kEnvironmentChanges).toStringList());
    setEnvironmentChanges(changes);
}

CorePlugin::CorePlugin()
{
    qRegisterMetaType<Id>();
    qRegisterMetaType<Core::Search::TextPosition>();
    qRegisterMetaType<Utils::CommandLine>();
    qRegisterMetaType<Utils::FilePath>();
    m_instance = this;
    m_reaper = new ProcessReapers;
    setupSystemEnvironment();
}

CorePlugin::~CorePlugin()
{
    delete m_reaper;
    IWizardFactory::destroyFeatureProvider();
    Find::destroy();

    delete m_locator;
    delete m_editMode;

    DesignMode::destroyModeIfRequired();

    delete m_mainWindow;
    setCreatorTheme(nullptr);
}

CorePlugin *CorePlugin::instance()
{
    return m_instance;
}

struct CoreArguments {
    QColor overrideColor;
    Id themeId;
    bool presentationMode = false;
};

CoreArguments parseArguments(const QStringList &arguments)
{
    CoreArguments args;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("-color")) {
            const QString colorcode(arguments.at(i + 1));
            args.overrideColor = QColor(colorcode);
            i++; // skip the argument
        }
        if (arguments.at(i) == QLatin1String("-presentationMode"))
            args.presentationMode = true;
        if (arguments.at(i) == QLatin1String("-theme")) {
            args.themeId = Id::fromString(arguments.at(i + 1));
            i++; // skip the argument
        }
    }
    return args;
}

bool CorePlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    // register all mime types from all plugins
    for (ExtensionSystem::PluginSpec *plugin : ExtensionSystem::PluginManager::plugins()) {
        if (!plugin->isEffectivelyEnabled())
            continue;
        const QJsonObject metaData = plugin->metaData();
        const QJsonValue mimetypes = metaData.value("Mimetypes");
        QString mimetypeString;
        if (Utils::readMultiLineString(mimetypes, &mimetypeString))
            Utils::addMimeTypes(plugin->name() + ".mimetypes", mimetypeString.trimmed().toUtf8());
    }

    if (ThemeEntry::availableThemes().isEmpty()) {
        *errorMessage = tr("No themes found in installation.");
        return false;
    }
    const CoreArguments args = parseArguments(arguments);
    Theme::initialPalette(); // Initialize palette before setting it
    Theme *themeFromArg = ThemeEntry::createTheme(args.themeId);
    setCreatorTheme(themeFromArg ? themeFromArg
                                 : ThemeEntry::createTheme(ThemeEntry::themeSetting()));
    InfoBar::initialize(ICore::settings());
    new ActionManager(this);
    ActionManager::setPresentationModeEnabled(args.presentationMode);
    m_mainWindow = new MainWindow;
    if (args.overrideColor.isValid())
        m_mainWindow->setOverrideColor(args.overrideColor);
    m_locator = new Locator;
    std::srand(unsigned(QDateTime::currentDateTime().toSecsSinceEpoch()));
    m_mainWindow->init();
    m_editMode = new EditMode;
    ModeManager::activateMode(m_editMode->id());

    IWizardFactory::initialize();

    // Make sure we respect the process's umask when creating new files
    SaveFile::initializeUmask();

    Find::initialize();
    m_locator->initialize();

    MacroExpander *expander = Utils::globalMacroExpander();
    expander->registerVariable("CurrentDate:ISO", tr("The current date (ISO)."),
                               []() { return QDate::currentDate().toString(Qt::ISODate); });
    expander->registerVariable("CurrentTime:ISO", tr("The current time (ISO)."),
                               []() { return QTime::currentTime().toString(Qt::ISODate); });
    expander->registerVariable("CurrentDate:RFC", tr("The current date (RFC2822)."),
                               []() { return QDate::currentDate().toString(Qt::RFC2822Date); });
    expander->registerVariable("CurrentTime:RFC", tr("The current time (RFC2822)."),
                               []() { return QTime::currentTime().toString(Qt::RFC2822Date); });
    expander->registerVariable("CurrentDate:Locale", tr("The current date (Locale)."),
                               []() { return QLocale::system()
                                        .toString(QDate::currentDate(), QLocale::ShortFormat); });
    expander->registerVariable("CurrentTime:Locale", tr("The current time (Locale)."),
                               []() { return QLocale::system()
                                        .toString(QTime::currentTime(), QLocale::ShortFormat); });
    expander->registerVariable("Config:DefaultProjectDirectory", tr("The configured default directory for projects."),
                               []() { return DocumentManager::projectsDirectory().toString(); });
    expander->registerVariable("Config:LastFileDialogDirectory", tr("The directory last visited in a file dialog."),
                               []() { return DocumentManager::fileDialogLastVisitedDirectory(); });
    expander->registerVariable("HostOs:isWindows",
                               tr("Is %1 running on Windows?").arg(Constants::IDE_DISPLAY_NAME),
                               []() { return QVariant(Utils::HostOsInfo::isWindowsHost()).toString(); });
    expander->registerVariable("HostOs:isOSX",
                               tr("Is %1 running on OS X?").arg(Constants::IDE_DISPLAY_NAME),
                               []() { return QVariant(Utils::HostOsInfo::isMacHost()).toString(); });
    expander->registerVariable("HostOs:isLinux",
                               tr("Is %1 running on Linux?").arg(Constants::IDE_DISPLAY_NAME),
                               []() { return QVariant(Utils::HostOsInfo::isLinuxHost()).toString(); });
    expander->registerVariable("HostOs:isUnix",
                               tr("Is %1 running on any unix-based platform?")
                                   .arg(Constants::IDE_DISPLAY_NAME),
                               []() { return QVariant(Utils::HostOsInfo::isAnyUnixHost()).toString(); });
    expander->registerVariable("HostOs:PathListSeparator",
                               tr("The path list separator for the platform."),
                               []() { return QString(Utils::HostOsInfo::pathListSeparator()); });
    expander->registerVariable("HostOs:ExecutableSuffix",
                               tr("The platform executable suffix."),
                               []() { return QString(Utils::HostOsInfo::withExecutableSuffix("")); });
    expander->registerVariable("IDE:ResourcePath",
                               tr("The directory where %1 finds its pre-installed resources.")
                                   .arg(Constants::IDE_DISPLAY_NAME),
                               []() { return ICore::resourcePath().toString(); });
    expander->registerPrefix("CurrentDate:", tr("The current date (QDate formatstring)."),
                             [](const QString &fmt) { return QDate::currentDate().toString(fmt); });
    expander->registerPrefix("CurrentTime:", tr("The current time (QTime formatstring)."),
                             [](const QString &fmt) { return QTime::currentTime().toString(fmt); });
    expander->registerVariable("UUID", tr("Generate a new UUID."),
                               []() { return QUuid::createUuid().toString(); });

    expander->registerPrefix("#:", tr("A comment."), [](const QString &) { return QString(); });

    Utils::PathChooser::setAboutToShowContextMenuHandler(&CorePlugin::addToPathChooserContextMenu);

#ifdef ENABLE_CRASHPAD
    connect(ICore::instance(), &ICore::coreOpened, this, &CorePlugin::warnAboutCrashReporing,
            Qt::QueuedConnection);
#endif

    return true;
}

void CorePlugin::extensionsInitialized()
{
    DesignMode::createModeIfRequired();
    Find::extensionsInitialized();
    m_locator->extensionsInitialized();
    m_mainWindow->extensionsInitialized();
    if (ExtensionSystem::PluginManager::hasError()) {
        auto errorOverview = new ExtensionSystem::PluginErrorOverview(m_mainWindow);
        errorOverview->setAttribute(Qt::WA_DeleteOnClose);
        errorOverview->setModal(true);
        errorOverview->show();
    }
    checkSettings();
}

bool CorePlugin::delayedInitialize()
{
    m_locator->delayedInitialize();
    IWizardFactory::allWizardFactories(); // scan for all wizard factories
    return true;
}

QObject *CorePlugin::remoteCommand(const QStringList & /* options */,
                                   const QString &workingDirectory,
                                   const QStringList &args)
{
    if (!ExtensionSystem::PluginManager::isInitializationDone()) {
        connect(ExtensionSystem::PluginManager::instance(), &ExtensionSystem::PluginManager::initializationDone,
                this, [this, workingDirectory, args]() {
                    remoteCommand(QStringList(), workingDirectory, args);
        });
        return nullptr;
    }
    const FilePaths filePaths = Utils::transform(args, FilePath::fromString);
    IDocument *res = MainWindow::openFiles(
                filePaths,
                ICore::OpenFilesFlags(ICore::SwitchMode | ICore::CanContainLineAndColumnNumbers | ICore::SwitchSplitIfAlreadyVisible),
                workingDirectory);
    m_mainWindow->raiseWindow();
    return res;
}

Environment CorePlugin::startupSystemEnvironment()
{
    return m_instance->m_startupSystemEnvironment;
}

EnvironmentItems CorePlugin::environmentChanges()
{
    return m_instance->m_environmentChanges;
}

void CorePlugin::setEnvironmentChanges(const EnvironmentItems &changes)
{
    if (m_instance->m_environmentChanges == changes)
        return;
    m_instance->m_environmentChanges = changes;
    Environment systemEnv = m_instance->m_startupSystemEnvironment;
    systemEnv.modify(changes);
    Environment::setSystemEnvironment(systemEnv);
    ICore::settings()->setValueWithDefault(kEnvironmentChanges,
                                           EnvironmentItem::toStringList(changes));
    if (ICore::instance())
        emit ICore::instance()->systemEnvironmentChanged();
}

void CorePlugin::fileOpenRequest(const QString &f)
{
    remoteCommand(QStringList(), QString(), QStringList(f));
}

void CorePlugin::addToPathChooserContextMenu(Utils::PathChooser *pathChooser, QMenu *menu)
{
    QList<QAction*> actions = menu->actions();
    QAction *firstAction = actions.isEmpty() ? nullptr : actions.first();

    if (QDir().exists(pathChooser->filePath().toString())) {
        auto *showInGraphicalShell = new QAction(Core::FileUtils::msgGraphicalShellAction(), menu);
        connect(showInGraphicalShell, &QAction::triggered, pathChooser, [pathChooser]() {
            Core::FileUtils::showInGraphicalShell(pathChooser, pathChooser->filePath());
        });
        menu->insertAction(firstAction, showInGraphicalShell);

        auto *showInTerminal = new QAction(Core::FileUtils::msgTerminalHereAction(), menu);
        connect(showInTerminal, &QAction::triggered, pathChooser, [pathChooser]() {
            if (pathChooser->openTerminalHandler())
                pathChooser->openTerminalHandler()();
            else
                FileUtils::openTerminal(pathChooser->filePath());
        });
        menu->insertAction(firstAction, showInTerminal);

    } else {
        auto *mkPathAct = new QAction(tr("Create Folder"), menu);
        connect(mkPathAct, &QAction::triggered, pathChooser, [pathChooser]() {
            QDir().mkpath(pathChooser->filePath().toString());
            pathChooser->triggerChanged();
        });
        menu->insertAction(firstAction, mkPathAct);
    }

    if (firstAction)
        menu->insertSeparator(firstAction);
}

void CorePlugin::checkSettings()
{
    const auto showMsgBox = [this](const QString &msg, QMessageBox::Icon icon) {
        connect(ICore::instance(), &ICore::coreOpened, this, [msg, icon]() {
            QMessageBox msgBox(ICore::dialogParent());
            msgBox.setWindowTitle(tr("Settings File Error"));
            msgBox.setText(msg);
            msgBox.setIcon(icon);
            msgBox.exec();
        }, Qt::QueuedConnection);
    };
    const QSettings * const userSettings = ICore::settings();
    QString errorDetails;
    switch (userSettings->status()) {
    case QSettings::NoError: {
        const QFileInfo fi(userSettings->fileName());
        if (fi.exists() && !fi.isWritable()) {
            const QString errorMsg = tr("The settings file \"%1\" is not writable.\n"
                    "You will not be able to store any %2 settings.")
                    .arg(QDir::toNativeSeparators(userSettings->fileName()),
                         QLatin1String(Core::Constants::IDE_DISPLAY_NAME));
            showMsgBox(errorMsg, QMessageBox::Warning);
        }
        return;
    }
    case QSettings::AccessError:
        errorDetails = tr("The file is not readable.");
        break;
    case QSettings::FormatError:
        errorDetails = tr("The file is invalid.");
        break;
    }
    const QString errorMsg = tr("Error reading settings file \"%1\": %2\n"
            "You will likely experience further problems using this instance of %3.")
            .arg(QDir::toNativeSeparators(userSettings->fileName()), errorDetails,
                 QLatin1String(Core::Constants::IDE_DISPLAY_NAME));
    showMsgBox(errorMsg, QMessageBox::Critical);
}

void CorePlugin::warnAboutCrashReporing()
{
    if (!ICore::infoBar()->canInfoBeAdded(kWarnCrashReportingSetting))
        return;

    QString warnStr = ICore::settings()->value("CrashReportingEnabled", false).toBool()
            ? tr("%1 collects crash reports for the sole purpose of fixing bugs. "
                 "To disable this feature go to %2.")
            : tr("%1 can collect crash reports for the sole purpose of fixing bugs. "
                 "To enable this feature go to %2.");

    if (Utils::HostOsInfo::isMacHost()) {
        warnStr = warnStr.arg(Core::Constants::IDE_DISPLAY_NAME)
                         .arg(Core::Constants::IDE_DISPLAY_NAME + tr(" > Preferences > Environment > System"));
    } else {
        warnStr = warnStr.arg(Core::Constants::IDE_DISPLAY_NAME)
                         .arg(tr("Tools > Options > Environment > System"));
    }

    Utils::InfoBarEntry info(kWarnCrashReportingSetting, warnStr,
                             Utils::InfoBarEntry::GlobalSuppression::Enabled);
    info.setCustomButtonInfo(tr("Configure..."), [] {
        ICore::infoBar()->removeInfo(kWarnCrashReportingSetting);
        ICore::infoBar()->globallySuppressInfo(kWarnCrashReportingSetting);
        ICore::showOptionsDialog(Core::Constants::SETTINGS_ID_SYSTEM);
    });

    info.setDetailsWidgetCreator([]() -> QWidget * {
        auto label = new QLabel;
        label->setWordWrap(true);
        label->setOpenExternalLinks(true);
        label->setText(msgCrashpadInformation());
        label->setContentsMargins(0, 0, 0, 8);
        return label;
    });
    ICore::infoBar()->addInfo(info);
}

// static
QString CorePlugin::msgCrashpadInformation()
{
    return tr("%1 uses Google Crashpad for collecting crashes and sending them to our backend "
              "for processing. Crashpad may capture arbitrary contents from crashed process’ "
              "memory, including user sensitive information, URLs, and whatever other content "
              "users have trusted %1 with. The collected crash reports are however only used "
              "for the sole purpose of fixing bugs.").arg(Core::Constants::IDE_DISPLAY_NAME)
            + "<br><br>" + tr("More information:")
            + "<br><a href='https://chromium.googlesource.com/crashpad/crashpad/+/master/doc/"
                           "overview_design.md'>" + tr("Crashpad Overview") + "</a>"
              "<br><a href='https://sentry.io/security/'>" + tr("%1 security policy").arg("Sentry.io")
            + "</a>";
}

ExtensionSystem::IPlugin::ShutdownFlag CorePlugin::aboutToShutdown()
{
    Find::aboutToShutdown();
    ExtensionSystem::IPlugin::ShutdownFlag shutdownFlag = m_locator->aboutToShutdown(
        [this] { emit asynchronousShutdownFinished(); });
    m_mainWindow->aboutToShutdown();
    return shutdownFlag;
}
