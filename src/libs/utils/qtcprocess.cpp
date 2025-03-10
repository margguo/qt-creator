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

#include "qtcprocess.h"

#include "commandline.h"
#include "executeondestruction.h"
#include "hostosinfo.h"
#include "launcherinterface.h"
#include "launcherpackets.h"
#include "launchersocket.h"
#include "qtcassert.h"
#include "stringutils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLoggingCategory>
#include <QTextCodec>
#include <QThread>
#include <QTimer>

#ifdef QT_GUI_LIB
// qmlpuppet does not use that.
#include <QApplication>
#include <QMessageBox>
#endif

#include <algorithm>
#include <limits.h>
#include <memory>

#ifdef Q_OS_WIN
#ifdef QTCREATOR_PCH_H
#define CALLBACK WINAPI
#endif
#include <qt_windows.h>
#endif

using namespace Utils::Internal;

namespace Utils {
namespace Internal {

enum { debug = 0 };
enum { syncDebug = 0 };

enum { defaultMaxHangTimerCount = 10 };

static Q_LOGGING_CATEGORY(processLog, "qtc.utils.qtcprocess", QtWarningMsg)

static DeviceProcessHooks s_deviceHooks;

// Data for one channel buffer (stderr/stdout)
class ChannelBuffer
{
public:
    void clearForRun();

    void handleRest();
    void append(const QByteArray &text);

    QByteArray rawData;
    QString incompleteLineBuffer; // lines not yet signaled
    QTextCodec *codec = nullptr; // Not owner
    std::unique_ptr<QTextCodec::ConverterState> codecState;
    std::function<void(const QString &lines)> outputCallback;

    bool emitSingleLines = true;
    bool keepRawData = true;
};

class ProcessInterface : public QObject
{
    Q_OBJECT
public:
    ProcessInterface(ProcessMode processMode)
        : QObject()
        , m_processMode(processMode) {}

    virtual QByteArray readAllStandardOutput() = 0;
    virtual QByteArray readAllStandardError() = 0;

    virtual void setProcessEnvironment(const QProcessEnvironment &environment) = 0;
    virtual void setWorkingDirectory(const QString &dir) = 0;
    virtual void start(const QString &program, const QStringList &arguments,
                       const QByteArray &writeData) = 0;
    virtual void terminate() = 0;
    virtual void kill() = 0;
    virtual void close() = 0;
    virtual qint64 write(const QByteArray &data) = 0;

    virtual void setStandardInputFile(const QString &fileName) = 0;
    virtual void setProcessChannelMode(QProcess::ProcessChannelMode mode) = 0;

    virtual QString program() const = 0;
    virtual QProcess::ProcessError error() const = 0;
    virtual QProcess::ProcessState state() const = 0;
    virtual qint64 processId() const = 0;
    virtual QProcess::ExitStatus exitStatus() const = 0;
    virtual QString errorString() const = 0;
    virtual void setErrorString(const QString &str) = 0;

    virtual bool waitForStarted(int msecs) = 0;
    virtual bool waitForReadyRead(int msecs) = 0;
    virtual bool waitForFinished(int msecs) = 0;

    void setLowPriority() { m_lowPriority = true; }
    bool isLowPriority() const { return m_lowPriority; }
    void setUnixTerminalDisabled() { m_unixTerminalDisabled = true; }
    bool isUnixTerminalDisabled() const { return m_unixTerminalDisabled; }

    void setBelowNormalPriority() { m_belowNormalPriority = true; }
    bool isBelowNormalPriority() const { return m_belowNormalPriority; }
    void setNativeArguments(const QString &arguments) { m_nativeArguments = arguments; }
    QString nativeArguments() const { return m_nativeArguments; }

signals:
    void started();
    void finished(int exitCode, QProcess::ExitStatus status);
    void errorOccurred(QProcess::ProcessError error);
    void readyReadStandardOutput();
    void readyReadStandardError();

protected:
    ProcessMode processMode() const { return m_processMode; }
private:
    const ProcessMode m_processMode;
    bool m_belowNormalPriority = false;
    QString m_nativeArguments;
    bool m_lowPriority = false;
    bool m_unixTerminalDisabled = false;
};

class QProcessImpl : public ProcessInterface
{
public:
    QProcessImpl(ProcessMode processMode) : ProcessInterface(processMode)
    {
        connect(&m_process, &QProcess::started,
                this, &QProcessImpl::handleStarted);
        connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ProcessInterface::finished);
        connect(&m_process, &QProcess::errorOccurred,
                this, &ProcessInterface::errorOccurred);
        connect(&m_process, &QProcess::readyReadStandardOutput,
                this, &ProcessInterface::readyReadStandardOutput);
        connect(&m_process, &QProcess::readyReadStandardError,
                this, &ProcessInterface::readyReadStandardError);
    }

    QByteArray readAllStandardOutput() override { return m_process.readAllStandardOutput(); }
    QByteArray readAllStandardError() override { return m_process.readAllStandardError(); }

    void setProcessEnvironment(const QProcessEnvironment &environment) override
    { m_process.setProcessEnvironment(environment); }
    void setWorkingDirectory(const QString &dir) override
    { m_process.setWorkingDirectory(dir); }
    void start(const QString &program, const QStringList &arguments, const QByteArray &writeData) override
    {
        m_processStartHandler.setProcessMode(processMode());
        m_processStartHandler.setWriteData(writeData);
        if (isBelowNormalPriority())
            m_processStartHandler.setBelowNormalPriority(&m_process);
        m_processStartHandler.setNativeArguments(&m_process, nativeArguments());
        if (isLowPriority())
            m_process.setLowPriority();
        if (isUnixTerminalDisabled())
            m_process.setUnixTerminalDisabled();
        m_process.start(program, arguments, m_processStartHandler.openMode());
        m_processStartHandler.handleProcessStart(&m_process);
    }
    void terminate() override
    { m_process.terminate(); }
    void kill() override
    { m_process.kill(); }
    void close() override
    { m_process.close(); }
    qint64 write(const QByteArray &data) override
    { return m_process.write(data); }

    void setStandardInputFile(const QString &fileName) override
    { m_process.setStandardInputFile(fileName); }
    void setProcessChannelMode(QProcess::ProcessChannelMode mode) override
    { m_process.setProcessChannelMode(mode); }

    QString program() const override
    { return m_process.program(); }
    QProcess::ProcessError error() const override
    { return m_process.error(); }
    QProcess::ProcessState state() const override
    { return m_process.state(); }
    qint64 processId() const override
    { return m_process.processId(); }
    QProcess::ExitStatus exitStatus() const override
    { return m_process.exitStatus(); }
    QString errorString() const override
    { return m_process.errorString(); }
    void setErrorString(const QString &str) override
    { m_process.setErrorString(str); }

    bool waitForStarted(int msecs) override
    { return m_process.waitForStarted(msecs); }
    bool waitForReadyRead(int msecs) override
    { return m_process.waitForReadyRead(msecs); }
    bool waitForFinished(int msecs) override
    { return m_process.waitForFinished(msecs); }

private:
    void handleStarted()
    {
        m_processStartHandler.handleProcessStarted(&m_process);
        emit started();
    }
    ProcessHelper m_process;
    ProcessStartHandler m_processStartHandler;
};

static uint uniqueToken()
{
    static std::atomic_uint globalUniqueToken = 0;
    return ++globalUniqueToken;
}

class ProcessLauncherImpl : public ProcessInterface
{
    Q_OBJECT
public:
    ProcessLauncherImpl(ProcessMode processMode)
        : ProcessInterface(processMode), m_token(uniqueToken())
    {
        m_handle = LauncherInterface::socket()->registerHandle(token(), processMode);
        connect(m_handle, &CallerHandle::errorOccurred,
                this, &ProcessInterface::errorOccurred);
        connect(m_handle, &CallerHandle::started,
                this, &ProcessInterface::started);
        connect(m_handle, &CallerHandle::finished,
                this, &ProcessInterface::finished);
        connect(m_handle, &CallerHandle::readyReadStandardOutput,
                this, &ProcessInterface::readyReadStandardOutput);
        connect(m_handle, &CallerHandle::readyReadStandardError,
                this, &ProcessInterface::readyReadStandardError);
    }
    ~ProcessLauncherImpl() override
    {
        cancel();
        LauncherInterface::socket()->unregisterHandle(token());
    }

    QByteArray readAllStandardOutput() override { return m_handle->readAllStandardOutput(); }
    QByteArray readAllStandardError() override { return m_handle->readAllStandardError(); }

    void setProcessEnvironment(const QProcessEnvironment &environment) override
    { m_handle->setProcessEnvironment(environment); }
    void setWorkingDirectory(const QString &dir) override { m_handle->setWorkingDirectory(dir); }
    void start(const QString &program, const QStringList &arguments, const QByteArray &writeData) override
    {
        if (isBelowNormalPriority())
            m_handle->setBelowNormalPriority();
        m_handle->setNativeArguments(nativeArguments());
        if (isLowPriority())
            m_handle->setLowPriority();
        if (isUnixTerminalDisabled())
            m_handle->setUnixTerminalDisabled();
        m_handle->start(program, arguments, writeData);
    }
    void terminate() override { cancel(); } // TODO: what are differences among terminate, kill and close?
    void kill() override { cancel(); } // TODO: see above
    void close() override { cancel(); } // TODO: see above
    qint64 write(const QByteArray &data) override { return m_handle->write(data); }

    void setStandardInputFile(const QString &fileName) override { m_handle->setStandardInputFile(fileName); }
    void setProcessChannelMode(QProcess::ProcessChannelMode mode) override { m_handle->setProcessChannelMode(mode); }

    QString program() const override { return m_handle->program(); }
    QProcess::ProcessError error() const override { return m_handle->error(); }
    QProcess::ProcessState state() const override { return m_handle->state(); }
    qint64 processId() const override { return m_handle->processId(); }
    QProcess::ExitStatus exitStatus() const override { return m_handle->exitStatus(); }
    QString errorString() const override { return m_handle->errorString(); }
    void setErrorString(const QString &str) override { m_handle->setErrorString(str); }

    bool waitForStarted(int msecs) override { return m_handle->waitForStarted(msecs); }
    bool waitForReadyRead(int msecs) override { return m_handle->waitForReadyRead(msecs); }
    bool waitForFinished(int msecs) override { return m_handle->waitForFinished(msecs); }

private:
    typedef void (ProcessLauncherImpl::*PreSignal)(void);

    void cancel();

    void handleSocketError(const QString &message);
    void handleSocketReady();

    quintptr token() const { return m_token; }

    const uint m_token = 0;
    // Lives in launcher's thread.
    CallerHandle *m_handle = nullptr;
};

void ProcessLauncherImpl::cancel()
{
    m_handle->cancel();
}

static ProcessInterface *newProcessInstance(QtcProcess::ProcessImpl processImpl, ProcessMode mode)
{
    if (processImpl == QtcProcess::QProcessImpl)
        return new QProcessImpl(mode);
    return new ProcessLauncherImpl(mode);
}

class QtcProcessPrivate : public QObject
{
public:
    explicit QtcProcessPrivate(QtcProcess *parent,
                               QtcProcess::ProcessImpl processImpl,
                               ProcessMode processMode)
        : q(parent), m_process(newProcessInstance(processImpl, processMode)), m_processMode(processMode)
    {
        connect(m_process, &ProcessInterface::started,
                q, &QtcProcess::started);
        connect(m_process, &ProcessInterface::finished,
                this, &QtcProcessPrivate::slotFinished);
        connect(m_process, &ProcessInterface::errorOccurred,
                this, &QtcProcessPrivate::slotError);
        connect(m_process, &ProcessInterface::readyReadStandardOutput,
                this, &QtcProcessPrivate::handleReadyReadStandardOutput);
        connect(m_process, &ProcessInterface::readyReadStandardError,
                this, &QtcProcessPrivate::handleReadyReadStandardError);
        connect(&m_timer, &QTimer::timeout, this, &QtcProcessPrivate::slotTimeout);
        m_timer.setInterval(1000);
    }

    ~QtcProcessPrivate()
    {
        delete m_process;
    }

    void handleReadyReadStandardOutput()
    {
        m_stdOut.append(m_process->readAllStandardOutput());
        m_hangTimerCount = 0;
        emit q->readyReadStandardOutput();
    }

    void handleReadyReadStandardError()
    {
        m_stdErr.append(m_process->readAllStandardError());
        m_hangTimerCount = 0;
        emit q->readyReadStandardError();
    }

    QtcProcess *q;
    ProcessInterface *m_process;
    const ProcessMode m_processMode;
    CommandLine m_commandLine;
    FilePath m_workingDirectory;
    Environment m_environment;
    QByteArray m_writeData;
    bool m_haveEnv = false;
    bool m_useCtrlCStub = false;

    void slotTimeout();
    void slotFinished(int exitCode, QProcess::ExitStatus e);
    void slotError(QProcess::ProcessError);
    void clearForRun();

    QtcProcess::Result interpretExitCode(int exitCode);

    QTextCodec *m_codec = QTextCodec::codecForLocale();
    QTimer m_timer;
    QEventLoop m_eventLoop;
    QtcProcess::Result m_result = QtcProcess::StartFailed;
    QProcess::ExitStatus m_exitStatus = QProcess::NormalExit;
    int m_exitCode = -1;
    ChannelBuffer m_stdOut;
    ChannelBuffer m_stdErr;
    ExitCodeInterpreter m_exitCodeInterpreter;

    int m_hangTimerCount = 0;
    int m_maxHangTimerCount = defaultMaxHangTimerCount;
    bool m_startFailure = false;
    bool m_timeOutMessageBoxEnabled = false;
    bool m_waitingForUser = false;
    bool m_processUserEvents = false;
};

void QtcProcessPrivate::clearForRun()
{
    m_hangTimerCount = 0;
    m_stdOut.clearForRun();
    m_stdOut.codec = m_codec;
    m_stdErr.clearForRun();
    m_stdErr.codec = m_codec;
    m_result = QtcProcess::StartFailed;
    m_exitCode = -1;
    m_startFailure = false;
}

QtcProcess::Result QtcProcessPrivate::interpretExitCode(int exitCode)
{
    if (m_exitCodeInterpreter)
        return m_exitCodeInterpreter(exitCode);

    // default:
    return exitCode ? QtcProcess::FinishedWithError : QtcProcess::FinishedWithSuccess;
}

} // Internal

/*!
    \class Utils::QtcProcess

    \brief The QtcProcess class provides functionality for with processes.

    \sa Utils::ProcessArgs
*/

static QtcProcess::ProcessImpl defaultProcessImpl()
{
    if (qEnvironmentVariableIsSet("QTC_USE_QPROCESS"))
        return QtcProcess::QProcessImpl;
    return QtcProcess::ProcessLauncherImpl;
}

QtcProcess::QtcProcess(ProcessImpl processImpl, ProcessMode processMode, QObject *parent)
    : QObject(parent), d(new QtcProcessPrivate(this, processImpl, processMode))
{
    static int qProcessExitStatusMeta = qRegisterMetaType<QProcess::ExitStatus>();
    static int qProcessProcessErrorMeta = qRegisterMetaType<QProcess::ProcessError>();
    Q_UNUSED(qProcessExitStatusMeta)
    Q_UNUSED(qProcessProcessErrorMeta)
}

QtcProcess::QtcProcess(ProcessImpl processImpl, QObject *parent)
    : QtcProcess(processImpl, ProcessMode::Reader, parent) {}

QtcProcess::QtcProcess(ProcessMode processMode, QObject *parent)
    : QtcProcess(defaultProcessImpl(), processMode, parent) {}

QtcProcess::QtcProcess(QObject *parent)
    : QtcProcess(defaultProcessImpl(), ProcessMode::Reader, parent) {}

QtcProcess::~QtcProcess()
{
    delete d;
}

ProcessMode QtcProcess::processMode() const
{
    return d->m_processMode;
}

void QtcProcess::setEnvironment(const Environment &env)
{
    d->m_environment = env;
    d->m_haveEnv = true;
}

void QtcProcess::unsetEnvironment()
{
    d->m_environment = Environment();
    d->m_haveEnv = false;
}

const Environment &QtcProcess::environment() const
{
    return d->m_environment;
}

void QtcProcess::setCommand(const CommandLine &cmdLine)
{
    if (d->m_workingDirectory.needsDevice() && cmdLine.executable().needsDevice()) {
        QTC_CHECK(d->m_workingDirectory.host() == cmdLine.executable().host());
    }
    d->m_commandLine  = cmdLine;
}

const CommandLine &QtcProcess::commandLine() const
{
    return d->m_commandLine;
}

FilePath QtcProcess::workingDirectory() const
{
    return d->m_workingDirectory;
}

void QtcProcess::setWorkingDirectory(const FilePath &dir)
{
    if (dir.needsDevice() && d->m_commandLine.executable().needsDevice()) {
        QTC_CHECK(dir.host() == d->m_commandLine.executable().host());
    }
    d->m_workingDirectory = dir;
}

void QtcProcess::setWorkingDirectory(const QString &dir)
{
    setWorkingDirectory(FilePath::fromString(dir));
}

void QtcProcess::setUseCtrlCStub(bool enabled)
{
    // Do not use the stub in debug mode. Activating the stub will shut down
    // Qt Creator otherwise, because they share the same Windows console.
    // See QTCREATORBUG-11995 for details.
#ifndef QT_DEBUG
    d->m_useCtrlCStub = enabled;
#else
    Q_UNUSED(enabled)
#endif
}

void QtcProcess::start()
{
    d->clearForRun();

    if (d->m_commandLine.executable().needsDevice()) {
        QTC_ASSERT(s_deviceHooks.startProcessHook, return);
        s_deviceHooks.startProcessHook(*this);
        return;
    }

    if (processLog().isDebugEnabled()) {
        static int n = 0;
        qCDebug(processLog) << "STARTING PROCESS: " << ++n << "  " << d->m_commandLine.toUserOutput();
    }

    Environment env;
    if (d->m_haveEnv) {
        if (d->m_environment.size() == 0)
            qWarning("QtcProcess::start: Empty environment set when running '%s'.",
                     qPrintable(d->m_commandLine.executable().toString()));
        env = d->m_environment;

        d->m_process->setProcessEnvironment(env.toProcessEnvironment());
    } else {
        env = Environment::systemEnvironment();
    }

    d->m_process->setWorkingDirectory(d->m_workingDirectory.path());

    QString command;
    ProcessArgs arguments;
    bool success = ProcessArgs::prepareCommand(d->m_commandLine, &command, &arguments, &env,
                                               &d->m_workingDirectory);

    if (d->m_commandLine.executable().osType() == OsTypeWindows) {
        QString args;
        if (d->m_useCtrlCStub) {
            if (d->m_process->isLowPriority())
                ProcessArgs::addArg(&args, "-nice");
            ProcessArgs::addArg(&args, QDir::toNativeSeparators(command));
            command = QCoreApplication::applicationDirPath()
                    + QLatin1String("/qtcreator_ctrlc_stub.exe");
        } else if (d->m_process->isLowPriority()) {
            d->m_process->setBelowNormalPriority();
        }
        ProcessArgs::addArgs(&args, arguments.toWindowsArgs());
#ifdef Q_OS_WIN
        d->m_process->setNativeArguments(args);
#endif
        // Note: Arguments set with setNativeArgs will be appended to the ones
        // passed with start() below.
        d->m_process->start(command, QStringList(), d->m_writeData);
    } else {
        if (!success) {
            setErrorString(tr("Error in command line."));
            // Should be FailedToStart, but we cannot set the process error from the outside,
            // so it would be inconsistent.
            emit errorOccurred(QProcess::UnknownError);
            return;
        }
        d->m_process->start(command, arguments.toUnixArgs(), d->m_writeData);
    }
}

#ifdef Q_OS_WIN
static BOOL sendMessage(UINT message, HWND hwnd, LPARAM lParam)
{
    DWORD dwProcessID;
    GetWindowThreadProcessId(hwnd, &dwProcessID);
    if ((DWORD)lParam == dwProcessID) {
        SendNotifyMessage(hwnd, message, 0, 0);
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK sendShutDownMessageToAllWindowsOfProcess_enumWnd(HWND hwnd, LPARAM lParam)
{
    static UINT uiShutDownMessage = RegisterWindowMessage(L"qtcctrlcstub_shutdown");
    return sendMessage(uiShutDownMessage, hwnd, lParam);
}

BOOL CALLBACK sendInterruptMessageToAllWindowsOfProcess_enumWnd(HWND hwnd, LPARAM lParam)
{
    static UINT uiInterruptMessage = RegisterWindowMessage(L"qtcctrlcstub_interrupt");
    return sendMessage(uiInterruptMessage, hwnd, lParam);
}
#endif

void QtcProcess::terminate()
{
#ifdef Q_OS_WIN
    if (d->m_useCtrlCStub)
        EnumWindows(sendShutDownMessageToAllWindowsOfProcess_enumWnd, processId());
    else
#endif
    d->m_process->terminate();
}

void QtcProcess::interrupt()
{
#ifdef Q_OS_WIN
    QTC_ASSERT(d->m_useCtrlCStub, return);
    EnumWindows(sendInterruptMessageToAllWindowsOfProcess_enumWnd, processId());
#endif
}

bool QtcProcess::startDetached(const CommandLine &cmd, const FilePath &workingDirectory, qint64 *pid)
{
    return QProcess::startDetached(cmd.executable().toUserOutput(),
                                   cmd.splitArguments(),
                                   workingDirectory.toUserOutput(),
                                   pid);
}

void QtcProcess::setLowPriority()
{
    d->m_process->setLowPriority();
}

void QtcProcess::setDisableUnixTerminal()
{
    d->m_process->setUnixTerminalDisabled();
}

void QtcProcess::setStandardInputFile(const QString &inputFile)
{
    d->m_process->setStandardInputFile(inputFile);
}

void QtcProcess::setRemoteProcessHooks(const DeviceProcessHooks &hooks)
{
    s_deviceHooks = hooks;
}

bool QtcProcess::stopProcess()
{
    if (state() == QProcess::NotRunning)
        return true;
    terminate();
    if (waitForFinished(300))
        return true;
    kill();
    return waitForFinished(300);
}

static bool askToKill(const QString &command)
{
#ifdef QT_GUI_LIB
    if (QThread::currentThread() != QCoreApplication::instance()->thread())
        return true;
    const QString title = QtcProcess::tr("Process not Responding");
    QString msg = command.isEmpty() ?
                QtcProcess::tr("The process is not responding.") :
                QtcProcess::tr("The process \"%1\" is not responding.").arg(command);
    msg += ' ';
    msg += QtcProcess::tr("Would you like to terminate it?");
    // Restore the cursor that is set to wait while running.
    const bool hasOverrideCursor = QApplication::overrideCursor() != nullptr;
    if (hasOverrideCursor)
        QApplication::restoreOverrideCursor();
    QMessageBox::StandardButton answer = QMessageBox::question(nullptr, title, msg, QMessageBox::Yes|QMessageBox::No);
    if (hasOverrideCursor)
        QApplication::setOverrideCursor(Qt::WaitCursor);
    return answer == QMessageBox::Yes;
#else
    Q_UNUSED(command)
    return true;
#endif
}

// Helper for running a process synchronously in the foreground with timeout
// detection (taking effect after no more output
// occurs on stderr/stdout as opposed to waitForFinished()). Returns false if a timeout
// occurs. Checking of the process' exit state/code still has to be done.

bool QtcProcess::readDataFromProcess(int timeoutS,
                                     QByteArray *stdOut,
                                     QByteArray *stdErr,
                                     bool showTimeOutMessageBox)
{
    enum { syncDebug = 0 };
    if (syncDebug)
        qDebug() << ">readDataFromProcess" << timeoutS;
    if (state() != QProcess::Running) {
        qWarning("readDataFromProcess: Process in non-running state passed in.");
        return false;
    }

    // Keep the process running until it has no longer has data
    bool finished = false;
    bool hasData = false;
    do {
        finished = waitForFinished(timeoutS > 0 ? timeoutS * 1000 : -1)
                || state() == QProcess::NotRunning;
        // First check 'stdout'
        const QByteArray newStdOut = readAllStandardOutput();
        if (!newStdOut.isEmpty()) {
            hasData = true;
            if (stdOut)
                stdOut->append(newStdOut);
        }
        // Check 'stderr' separately. This is a special handling
        // for 'git pull' and the like which prints its progress on stderr.
        const QByteArray newStdErr = readAllStandardError();
        if (!newStdErr.isEmpty()) {
            hasData = true;
            if (stdErr)
                stdErr->append(newStdErr);
        }
        // Prompt user, pretend we have data if says 'No'.
        const bool hang = !hasData && !finished;
        hasData = hang && showTimeOutMessageBox && !askToKill(d->m_process->program());
    } while (hasData && !finished);
    if (syncDebug)
        qDebug() << "<readDataFromProcess" << finished;
    return finished;
}

QString QtcProcess::normalizeNewlines(const QString &text)
{
    QString res = text;
    const auto newEnd = std::unique(res.begin(), res.end(), [](const QChar &c1, const QChar &c2) {
        return c1 == '\r' && c2 == '\r'; // QTCREATORBUG-24556
    });
    res.chop(std::distance(newEnd, res.end()));
    res.replace("\r\n", "\n");
    return res;
}

QtcProcess::Result QtcProcess::result() const
{
    return d->m_result;
}

void QtcProcess::setResult(Result result)
{
    d->m_result = result;
}

int QtcProcess::exitCode() const
{
    return d->m_exitCode;
}


// Path utilities

// Locate a binary in a directory, applying all kinds of
// extensions the operating system supports.
static QString checkBinary(const QDir &dir, const QString &binary)
{
    // naive UNIX approach
    const QFileInfo info(dir.filePath(binary));
    if (info.isFile() && info.isExecutable())
        return info.absoluteFilePath();

    // Does the OS have some weird extension concept or does the
    // binary have a 3 letter extension?
    if (HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost())
        return QString();
    const int dotIndex = binary.lastIndexOf(QLatin1Char('.'));
    if (dotIndex != -1 && dotIndex == binary.size() - 4)
        return  QString();

    switch (HostOsInfo::hostOs()) {
    case OsTypeLinux:
    case OsTypeOtherUnix:
    case OsTypeOther:
        break;
    case OsTypeWindows: {
            static const char *windowsExtensions[] = {".cmd", ".bat", ".exe", ".com"};
            // Check the Windows extensions using the order
            const int windowsExtensionCount = sizeof(windowsExtensions)/sizeof(const char*);
            for (int e = 0; e < windowsExtensionCount; e ++) {
                const QFileInfo windowsBinary(dir.filePath(binary + QLatin1String(windowsExtensions[e])));
                if (windowsBinary.isFile() && windowsBinary.isExecutable())
                    return windowsBinary.absoluteFilePath();
            }
        }
        break;
    case OsTypeMac: {
            // Check for Mac app folders
            const QFileInfo appFolder(dir.filePath(binary + QLatin1String(".app")));
            if (appFolder.isDir()) {
                QString macBinaryPath = appFolder.absoluteFilePath();
                macBinaryPath += QLatin1String("/Contents/MacOS/");
                macBinaryPath += binary;
                const QFileInfo macBinary(macBinaryPath);
                if (macBinary.isFile() && macBinary.isExecutable())
                    return macBinary.absoluteFilePath();
            }
        }
        break;
    }
    return QString();
}

QString QtcProcess::locateBinary(const QString &path, const QString &binary)
{
    // Absolute file?
    const QFileInfo absInfo(binary);
    if (absInfo.isAbsolute())
        return checkBinary(absInfo.dir(), absInfo.fileName());

    // Windows finds binaries  in the current directory
    if (HostOsInfo::isWindowsHost()) {
        const QString currentDirBinary = checkBinary(QDir::current(), binary);
        if (!currentDirBinary.isEmpty())
            return currentDirBinary;
    }

    const QStringList paths = path.split(HostOsInfo::pathListSeparator());
    if (paths.empty())
        return QString();
    const QStringList::const_iterator cend = paths.constEnd();
    for (QStringList::const_iterator it = paths.constBegin(); it != cend; ++it) {
        const QDir dir(*it);
        const QString rc = checkBinary(dir, binary);
        if (!rc.isEmpty())
            return rc;
    }
    return QString();
}

Environment QtcProcess::systemEnvironmentForBinary(const FilePath &filePath)
{
    if (filePath.needsDevice()) {
        QTC_ASSERT(s_deviceHooks.systemEnvironmentForBinary, return {});
        return s_deviceHooks.systemEnvironmentForBinary(filePath);
    }

    return Environment::systemEnvironment();
}

void QtcProcess::setProcessChannelMode(QProcess::ProcessChannelMode mode)
{
    d->m_process->setProcessChannelMode(mode);
}

QProcess::ProcessError QtcProcess::error() const
{
    return d->m_process->error();
}

QProcess::ProcessState QtcProcess::state() const
{
    return d->m_process->state();
}

QString QtcProcess::errorString() const
{
    return d->m_process->errorString();
}

void QtcProcess::setErrorString(const QString &str)
{
    d->m_process->setErrorString(str);
}

qint64 QtcProcess::processId() const
{
    return d->m_process->processId();
}

bool QtcProcess::waitForStarted(int msecs)
{
    return d->m_process->waitForStarted(msecs);
}

bool QtcProcess::waitForReadyRead(int msecs)
{
    return d->m_process->waitForReadyRead(msecs);
}

bool QtcProcess::waitForFinished(int msecs)
{
    return d->m_process->waitForFinished(msecs);
}

QByteArray QtcProcess::readAllStandardOutput()
{
    QByteArray buf = d->m_stdOut.rawData;
    d->m_stdOut.rawData.clear();
    return buf;
}

QByteArray QtcProcess::readAllStandardError()
{
    QByteArray buf = d->m_stdErr.rawData;
    d->m_stdErr.rawData.clear();
    return buf;
}

QProcess::ExitStatus QtcProcess::exitStatus() const
{
    return d->m_process->exitStatus();
}

void QtcProcess::kill()
{
    d->m_process->kill();
}

qint64 QtcProcess::write(const QByteArray &input)
{
    QTC_ASSERT(processMode() == ProcessMode::Writer, return -1);
    return d->m_process->write(input);
}

void QtcProcess::close()
{
    d->m_process->close();
}

void QtcProcess::beginFeed()
{
    d->clearForRun();
}

void QtcProcess::endFeed()
{
    d->slotFinished(0, QProcess::NormalExit);
}

void QtcProcess::feedStdOut(const QByteArray &data)
{
    d->m_stdOut.append(data);
    d->m_hangTimerCount = 0;
    emit readyReadStandardOutput();
}

QString QtcProcess::locateBinary(const QString &binary)
{
    const QByteArray path = qgetenv("PATH");
    return locateBinary(QString::fromLocal8Bit(path), binary);
}


/*!
    \class Utils::SynchronousProcess

    \brief The SynchronousProcess class runs a synchronous process in its own
    event loop that blocks only user input events. Thus, it allows for the GUI to
    repaint and append output to log windows.

    The callbacks set with setStdOutCallback(), setStdErrCallback() are called
    with complete lines based on the '\\n' marker.
    They would typically be used for log windows.

    Alternatively you can used setStdOutLineCallback() and setStdErrLineCallback()
    to process the output line by line.

    There is a timeout handling that takes effect after the last data have been
    read from stdout/stdin (as opposed to waitForFinished(), which measures time
    since it was invoked). It is thus also suitable for slow processes that
    continuously output data (like version system operations).

    The property timeOutMessageBoxEnabled influences whether a message box is
    shown asking the user if they want to kill the process on timeout (default: false).

    There are also static utility functions for dealing with fully synchronous
    processes, like reading the output with correct timeout handling.

    Caution: This class should NOT be used if there is a chance that the process
    triggers opening dialog boxes (for example, by file watchers triggering),
    as this will cause event loop problems.
*/

QString QtcProcess::exitMessage()
{
    const QString fullCmd = commandLine().toUserOutput();
    switch (result()) {
    case FinishedWithSuccess:
        return QtcProcess::tr("The command \"%1\" finished successfully.").arg(fullCmd);
    case FinishedWithError:
        return QtcProcess::tr("The command \"%1\" terminated with exit code %2.")
            .arg(fullCmd).arg(exitCode());
    case TerminatedAbnormally:
        return QtcProcess::tr("The command \"%1\" terminated abnormally.").arg(fullCmd);
    case StartFailed:
        return QtcProcess::tr("The command \"%1\" could not be started.").arg(fullCmd);
    case Hang:
        return QtcProcess::tr("The command \"%1\" did not respond within the timeout limit (%2 s).")
            .arg(fullCmd).arg(d->m_maxHangTimerCount);
    }
    return QString();
}

QByteArray QtcProcess::allRawOutput() const
{
    QTC_CHECK(d->m_stdOut.keepRawData);
    QTC_CHECK(d->m_stdErr.keepRawData);
    if (!d->m_stdOut.rawData.isEmpty() && !d->m_stdErr.rawData.isEmpty()) {
        QByteArray result = d->m_stdOut.rawData;
        if (!result.endsWith('\n'))
            result += '\n';
        result += d->m_stdErr.rawData;
        return result;
    }
    return !d->m_stdOut.rawData.isEmpty() ? d->m_stdOut.rawData : d->m_stdErr.rawData;
}

QString QtcProcess::allOutput() const
{
    QTC_CHECK(d->m_stdOut.keepRawData);
    QTC_CHECK(d->m_stdErr.keepRawData);
    const QString out = stdOut();
    const QString err = stdErr();

    if (!out.isEmpty() && !err.isEmpty()) {
        QString result = out;
        if (!result.endsWith('\n'))
            result += '\n';
        result += err;
        return result;
    }
    return !out.isEmpty() ? out : err;
}

QString QtcProcess::stdOut() const
{
    QTC_CHECK(d->m_stdOut.keepRawData);
    return normalizeNewlines(d->m_codec->toUnicode(d->m_stdOut.rawData));
}

QString QtcProcess::stdErr() const
{
    // FIXME: The tighter check below is actually good theoretically, but currently
    // ShellCommand::runFullySynchronous triggers it and disentangling there
    // is not trivial. So weaken it a bit for now.
    //QTC_CHECK(d->m_stdErr.keepRawData);
    QTC_CHECK(d->m_stdErr.keepRawData || d->m_stdErr.rawData.isEmpty());
    return normalizeNewlines(d->m_codec->toUnicode(d->m_stdErr.rawData));
}

QByteArray QtcProcess::rawStdOut() const
{
    QTC_CHECK(d->m_stdOut.keepRawData);
    return d->m_stdOut.rawData;
}

QTCREATOR_UTILS_EXPORT QDebug operator<<(QDebug str, const QtcProcess &r)
{
    QDebug nsp = str.nospace();
    nsp << "QtcProcess: result="
        << r.d->m_result << " ex=" << r.exitCode() << '\n'
        << r.d->m_stdOut.rawData.size() << " bytes stdout, stderr=" << r.d->m_stdErr.rawData << '\n';
    return str;
}

void ChannelBuffer::clearForRun()
{
    rawData.clear();
    codecState.reset(new QTextCodec::ConverterState);
    incompleteLineBuffer.clear();
}

/* Check for complete lines read from the device and return them, moving the
 * buffer position. */
void ChannelBuffer::append(const QByteArray &text)
{
    if (text.isEmpty())
        return;

    if (keepRawData)
        rawData += text;

    // Line-wise operation below:
    if (!outputCallback)
        return;

    // Convert and append the new input to the buffer of incomplete lines
    incompleteLineBuffer.append(codec->toUnicode(text.constData(), text.size(), codecState.get()));

    do {
        // Any completed lines in the incompleteLineBuffer?
        int pos = -1;
        if (emitSingleLines) {
            const int posn = incompleteLineBuffer.indexOf('\n');
            const int posr = incompleteLineBuffer.indexOf('\r');
            if (posn != -1) {
                if (posr != -1) {
                    if (posn == posr + 1)
                        pos = posn; // \r followed by \n -> line end, use the \n.
                    else
                        pos = qMin(posr, posn); // free floating \r and \n: Use the first one.
                } else {
                    pos = posn;
                }
            } else {
                pos = posr; // Make sure internal '\r' triggers a line output
            }
        } else {
            pos = qMax(incompleteLineBuffer.lastIndexOf('\n'),
                       incompleteLineBuffer.lastIndexOf('\r'));
        }

        if (pos == -1)
            break;

        // Get completed lines and remove them from the incompleteLinesBuffer:
        const QString line = QtcProcess::normalizeNewlines(incompleteLineBuffer.left(pos + 1));
        incompleteLineBuffer = incompleteLineBuffer.mid(pos + 1);

        QTC_ASSERT(outputCallback, return);
        outputCallback(line);

        if (!emitSingleLines)
            break;
    } while (true);
}

void ChannelBuffer::handleRest()
{
    if (outputCallback && !incompleteLineBuffer.isEmpty()) {
        outputCallback(incompleteLineBuffer);
        incompleteLineBuffer.clear();
    }
}

void QtcProcess::setProcessUserEventWhileRunning()
{
    d->m_processUserEvents = true;
}

void QtcProcess::setTimeoutS(int timeoutS)
{
    if (timeoutS > 0)
        d->m_maxHangTimerCount = qMax(2, timeoutS);
    else
        d->m_maxHangTimerCount = INT_MAX / 1000;
}

void QtcProcess::setCodec(QTextCodec *c)
{
    QTC_ASSERT(c, return);
    d->m_codec = c;
}

void QtcProcess::setTimeOutMessageBoxEnabled(bool v)
{
    d->m_timeOutMessageBoxEnabled = v;
}

void QtcProcess::setExitCodeInterpreter(const ExitCodeInterpreter &interpreter)
{
    d->m_exitCodeInterpreter = interpreter;
}

void QtcProcess::setWriteData(const QByteArray &writeData)
{
    d->m_writeData = writeData;
}

#ifdef QT_GUI_LIB
static bool isGuiThread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread();
}
#endif

void QtcProcess::runBlocking()
{
    // FIXME: Implement properly

    if (d->m_commandLine.executable().needsDevice()) {
        QtcProcess::start();
        waitForFinished();
        return;
    };

    qCDebug(processLog).noquote() << "Starting blocking:" << d->m_commandLine.toUserOutput()
        << " process user events: " << d->m_processUserEvents;
    ExecuteOnDestruction logResult([this] { qCDebug(processLog) << *this; });

    if (d->m_processUserEvents) {
        QtcProcess::start();

        // On Windows, start failure is triggered immediately if the
        // executable cannot be found in the path. Do not start the
        // event loop in that case.
        if (!d->m_startFailure) {
            d->m_timer.start();
#ifdef QT_GUI_LIB
            if (isGuiThread())
                QApplication::setOverrideCursor(Qt::WaitCursor);
#endif
            d->m_eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
            d->m_stdOut.append(d->m_process->readAllStandardOutput());
            d->m_stdErr.append(d->m_process->readAllStandardError());

            d->m_timer.stop();
#ifdef QT_GUI_LIB
            if (isGuiThread())
                QApplication::restoreOverrideCursor();
#endif
        }
    } else {
        QtcProcess::start();
        if (!waitForStarted(d->m_maxHangTimerCount * 1000)) {
            d->m_result = QtcProcess::StartFailed;
            return;
        }
        if (!waitForFinished(d->m_maxHangTimerCount * 1000)) {
            d->m_result = QtcProcess::Hang;
            terminate();
            if (!waitForFinished(1000)) {
                kill();
                waitForFinished(1000);
            }
        }

        if (state() != QProcess::NotRunning)
            return;

        d->m_stdOut.append(d->m_process->readAllStandardOutput());
        d->m_stdErr.append(d->m_process->readAllStandardError());
    }
}

void QtcProcess::setStdOutCallback(const std::function<void (const QString &)> &callback)
{
    d->m_stdOut.outputCallback = callback;
    d->m_stdOut.emitSingleLines = false;
}

void QtcProcess::setStdOutLineCallback(const std::function<void (const QString &)> &callback)
{
    d->m_stdOut.outputCallback = callback;
    d->m_stdOut.emitSingleLines = true;
    d->m_stdOut.keepRawData = false;
}

void QtcProcess::setStdErrCallback(const std::function<void (const QString &)> &callback)
{
    d->m_stdErr.outputCallback = callback;
    d->m_stdErr.emitSingleLines = false;
}

void QtcProcess::setStdErrLineCallback(const std::function<void (const QString &)> &callback)
{
    d->m_stdErr.outputCallback = callback;
    d->m_stdErr.emitSingleLines = true;
    d->m_stdErr.keepRawData = false;
}

void QtcProcessPrivate::slotTimeout()
{
    if (!m_waitingForUser && (++m_hangTimerCount > m_maxHangTimerCount)) {
        if (debug)
            qDebug() << Q_FUNC_INFO << "HANG detected, killing";
        m_waitingForUser = true;
        const bool terminate = !m_timeOutMessageBoxEnabled
            || askToKill(m_commandLine.executable().toString());
        m_waitingForUser = false;
        if (terminate) {
            q->stopProcess();
            m_result = QtcProcess::Hang;
        } else {
            m_hangTimerCount = 0;
        }
    } else {
        if (debug)
            qDebug() << Q_FUNC_INFO << m_hangTimerCount;
    }
}

void QtcProcessPrivate::slotFinished(int exitCode, QProcess::ExitStatus status)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << exitCode << status;
    m_hangTimerCount = 0;
    m_exitStatus = status;

    switch (status) {
    case QProcess::NormalExit:
        m_result = interpretExitCode(exitCode);
        m_exitCode = exitCode;
        break;
    case QProcess::CrashExit:
        // Was hang detected before and killed?
        if (m_result != QtcProcess::Hang)
            m_result = QtcProcess::TerminatedAbnormally;
        m_exitCode = -1;
        break;
    }
    m_eventLoop.quit();

    m_stdOut.handleRest();
    m_stdErr.handleRest();

    emit q->finished();
}

void QtcProcessPrivate::slotError(QProcess::ProcessError error)
{
    m_hangTimerCount = 0;
    if (debug)
        qDebug() << Q_FUNC_INFO << error;
    // Was hang detected before and killed?
    if (m_result != QtcProcess::Hang)
        m_result = QtcProcess::StartFailed;
    m_startFailure = true;
    m_eventLoop.quit();

    emit q->errorOccurred(error);
}

} // namespace Utils

#include "qtcprocess.moc"
