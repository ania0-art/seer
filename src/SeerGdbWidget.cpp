#include "SeerGdbWidget.h"
#include "SeerLogWidget.h"
#include "SeerMemoryVisualizerWidget.h"
#include "SeerArrayVisualizerWidget.h"
#include "SeerUtl.h"
#include <QtGui/QFont>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtCore/QDebug>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

SeerGdbWidget::SeerGdbWidget (QWidget* parent) : QWidget(parent) {

    _executableName             = "";
    _executableArguments        = "";
    _executableWorkingDirectory = "";
    _executablePid              = 0;
    _gdbMonitor                 = 0;
    _gdbProcess                 = 0;
    _consoleWidget              = 0;
    _breakpointsBrowserWidget   = 0;
    _watchpointsBrowserWidget   = 0;
    _catchpointsBrowserWidget   = 0;
    _gdbOutputLog               = 0;
    _seerOutputLog              = 0;
    _gdbProgram                 = "/usr/bin/gdb";
    _gdbArguments               = "--interpreter=mi";
    _gdbASyncMode               = true;
    _consoleMode                = "";
    _rememberManualCommandCount = 10;

    setNewExecutableFlag(true);

    setupUi(this);

    // Setup the widgets
    QFont font;
    font.setFamily("monospace [Consolas]");
    font.setFixedPitch(true);
    font.setStyleHint(QFont::TypeWriter);

    _breakpointsBrowserWidget             = new SeerBreakpointsBrowserWidget(this);
    _watchpointsBrowserWidget             = new SeerWatchpointsBrowserWidget(this);
    _catchpointsBrowserWidget             = new SeerCatchpointsBrowserWidget(this);
    _gdbOutputLog                         = new SeerTildeEqualAmpersandLogWidget(this);
    _seerOutputLog                        = new SeerCaretAsteriskLogWidget(this);

    logsTabWidget->addTab(_breakpointsBrowserWidget, "Breakpoints");
    logsTabWidget->addTab(_watchpointsBrowserWidget, "Watchpoints");
    logsTabWidget->addTab(_catchpointsBrowserWidget, "Catchpoints");
    logsTabWidget->addTab(_gdbOutputLog,  "GDB  output");
    logsTabWidget->addTab(_seerOutputLog, "Seer output");
    logsTabWidget->setCurrentIndex(0);

    manualCommandComboBox->setFont(font);
    manualCommandComboBox->setEditable(true);
    manualCommandComboBox->lineEdit()->setPlaceholderText("Manually enter a gdb/mi command...");
    manualCommandComboBox->lineEdit()->setToolTip("Manually enter a gdb/mi command.");
    manualCommandComboBox->lineEdit()->setClearButtonEnabled(true);

    // Create the gdb process.
    _gdbProcess = new QProcess(this);

    // Create the gdb monitor.
    _gdbMonitor = new GdbMonitor(this);
    _gdbMonitor->setProcess(_gdbProcess);

    // Connect things.
    QObject::connect(manualCommandComboBox->lineEdit(),                         &QLineEdit::returnPressed,                                                                  this,                                                           &SeerGdbWidget::handleExecute);

    QObject::connect(_gdbProcess,                                               &QProcess::readyReadStandardOutput,                                                         _gdbMonitor,                                                    &GdbMonitor::handleReadyReadStandardOutput);
    QObject::connect(_gdbProcess,                                               &QProcess::readyReadStandardError,                                                          _gdbMonitor,                                                    &GdbMonitor::handleReadyReadStandardError);
    QObject::connect(_gdbProcess,                                               static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),            this,                                                           &SeerGdbWidget::handleGdbProcessFinished); // ??? Do we care about the gdb process ending? For now, terminate Seer.
    QObject::connect(_gdbProcess,                                               static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),          this,                                                           &SeerGdbWidget::handleGdbProcessErrored);

    QObject::connect(_gdbMonitor,                                               &GdbMonitor::tildeTextOutput,                                                               _gdbOutputLog,                                                  &SeerTildeEqualAmpersandLogWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::equalTextOutput,                                                               _gdbOutputLog,                                                  &SeerTildeEqualAmpersandLogWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::ampersandTextOutput,                                                           _gdbOutputLog,                                                  &SeerTildeEqualAmpersandLogWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               _seerOutputLog,                                                 &SeerCaretAsteriskLogWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::astrixTextOutput,                                                              _seerOutputLog,                                                 &SeerCaretAsteriskLogWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::astrixTextOutput,                                                              editorManagerWidget,                                            &SeerEditorManagerWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               editorManagerWidget,                                            &SeerEditorManagerWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               sourceLibraryManagerWidget->sourceBrowserWidget(),              &SeerSourceBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               sourceLibraryManagerWidget->sharedLibraryBrowserWidget(),       &SeerSharedLibraryBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               stackManagerWidget->stackFramesBrowserWidget(),                 &SeerStackFramesBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               stackManagerWidget->stackLocalsBrowserWidget(),                 &SeerStackLocalsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               stackManagerWidget->stackArgumentsBrowserWidget(),              &SeerStackArgumentsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               stackManagerWidget,                                             &SeerStackManagerWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               threadManagerWidget->threadIdsBrowserWidget(),                  &SeerThreadIdsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               threadManagerWidget->threadFramesBrowserWidget(),               &SeerThreadFramesBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               _breakpointsBrowserWidget,                                      &SeerBreakpointsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               _watchpointsBrowserWidget,                                      &SeerWatchpointsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               _catchpointsBrowserWidget,                                      &SeerCatchpointsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               variableManagerWidget->registerValuesBrowserWidget(),           &SeerRegisterValuesBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               variableManagerWidget->variableTrackerBrowserWidget(),          &SeerVariableTrackerBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::caretTextOutput,                                                               variableManagerWidget->variableLoggerBrowserWidget(),           &SeerVariableLoggerBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::astrixTextOutput,                                                              _watchpointsBrowserWidget,                                      &SeerWatchpointsBrowserWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::astrixTextOutput,                                                              this,                                                           &SeerGdbWidget::handleText);
    QObject::connect(_gdbMonitor,                                               &GdbMonitor::equalTextOutput,                                                               this,                                                           &SeerGdbWidget::handleText);

    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::refreshBreakpointsList,                                           this,                                                           &SeerGdbWidget::handleGdbBreakpointWatchpointCatchpointList);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::refreshStackFrames,                                               this,                                                           &SeerGdbWidget::handleGdbStackListFrames);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::insertBreakpoint,                                                 this,                                                           &SeerGdbWidget::handleGdbBreakpointInsert);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::deleteBreakpoints,                                                this,                                                           &SeerGdbWidget::handleGdbBreakpointDelete);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::enableBreakpoints,                                                this,                                                           &SeerGdbWidget::handleGdbBreakpointEnable);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::disableBreakpoints,                                               this,                                                           &SeerGdbWidget::handleGdbBreakpointDisable);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::runToLine,                                                        this,                                                           &SeerGdbWidget::handleGdbRunToLine);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::addVariableLoggerExpression,                                      variableManagerWidget->variableLoggerBrowserWidget(),           &SeerVariableLoggerBrowserWidget::addVariableExpression);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::addVariableTrackerExpression,                                     this,                                                           &SeerGdbWidget::handleGdbDataAddExpression);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::refreshVariableTrackerValues,                                     this,                                                           &SeerGdbWidget::handleGdbDataListValues);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::addMemoryVisualize,                                               this,                                                           &SeerGdbWidget::handleGdbMemoryAddExpression);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::evaluateVariableExpression,                                       this,                                                           &SeerGdbWidget::handleGdbDataEvaluateExpression);
    QObject::connect(editorManagerWidget,                                       &SeerEditorManagerWidget::evaluateVariableExpression,                                       variableManagerWidget->variableLoggerBrowserWidget(),           &SeerVariableLoggerBrowserWidget::handleEvaluateVariableExpression);

    QObject::connect(sourceLibraryManagerWidget->sourceBrowserWidget(),         &SeerSourceBrowserWidget::refreshSourceList,                                                this,                                                           &SeerGdbWidget::handleGdbExecutableSources);
    QObject::connect(sourceLibraryManagerWidget->sourceBrowserWidget(),         &SeerSourceBrowserWidget::selectedFile,                                                     editorManagerWidget,                                            &SeerEditorManagerWidget::handleOpenFile);
    QObject::connect(sourceLibraryManagerWidget->sharedLibraryBrowserWidget(),  &SeerSharedLibraryBrowserWidget::refreshSharedLibraryList,                                  this,                                                           &SeerGdbWidget::handleGdbExecutableSharedLibraries);

    QObject::connect(stackManagerWidget->stackFramesBrowserWidget(),            &SeerStackFramesBrowserWidget::refreshStackFrames,                                          this,                                                           &SeerGdbWidget::handleGdbStackListFrames);
    QObject::connect(stackManagerWidget->stackLocalsBrowserWidget(),            &SeerStackLocalsBrowserWidget::refreshStackLocals,                                          this,                                                           &SeerGdbWidget::handleGdbStackListLocals);
    QObject::connect(stackManagerWidget->stackFramesBrowserWidget(),            &SeerStackFramesBrowserWidget::selectedFrame,                                               this,                                                           &SeerGdbWidget::handleGdbStackSelectFrame);
    QObject::connect(stackManagerWidget->stackArgumentsBrowserWidget(),         &SeerStackArgumentsBrowserWidget::refreshStackArguments,                                    this,                                                           &SeerGdbWidget::handleGdbStackListArguments);
    QObject::connect(stackManagerWidget->stackFramesBrowserWidget(),            &SeerStackFramesBrowserWidget::selectedFile,                                                editorManagerWidget,                                            &SeerEditorManagerWidget::handleOpenFile);
    QObject::connect(stackManagerWidget,                                        &SeerStackManagerWidget::refreshThreadFrames,                                               this,                                                           &SeerGdbWidget::handleGdbThreadListFrames);

    QObject::connect(variableManagerWidget->variableTrackerBrowserWidget(),     &SeerVariableTrackerBrowserWidget::refreshVariableTrackerValues,                            this,                                                           &SeerGdbWidget::handleGdbDataListExpressions);
    QObject::connect(variableManagerWidget->variableTrackerBrowserWidget(),     &SeerVariableTrackerBrowserWidget::refreshVariableTrackerNames,                             this,                                                           &SeerGdbWidget::handleGdbDataListValues);
    QObject::connect(variableManagerWidget->variableTrackerBrowserWidget(),     &SeerVariableTrackerBrowserWidget::addVariableExpression,                                   this,                                                           &SeerGdbWidget::handleGdbDataAddExpression);
    QObject::connect(variableManagerWidget->variableTrackerBrowserWidget(),     &SeerVariableTrackerBrowserWidget::deleteVariableExpressions,                               this,                                                           &SeerGdbWidget::handleGdbDataDeleteExpressions);
    QObject::connect(variableManagerWidget->variableLoggerBrowserWidget(),      &SeerVariableLoggerBrowserWidget::evaluateVariableExpression,                               this,                                                           &SeerGdbWidget::handleGdbDataEvaluateExpression);

    QObject::connect(variableManagerWidget->registerValuesBrowserWidget(),      &SeerRegisterValuesBrowserWidget::refreshRegisterNames,                                     this,                                                           &SeerGdbWidget::handleGdbRegisterListNames);
    QObject::connect(variableManagerWidget->registerValuesBrowserWidget(),      &SeerRegisterValuesBrowserWidget::refreshRegisterValues,                                    this,                                                           &SeerGdbWidget::handleGdbRegisterListValues);

    QObject::connect(threadManagerWidget->threadIdsBrowserWidget(),             &SeerThreadIdsBrowserWidget::refreshThreadIds,                                              this,                                                           &SeerGdbWidget::handleGdbThreadListIds);
    QObject::connect(threadManagerWidget->threadIdsBrowserWidget(),             &SeerThreadIdsBrowserWidget::selectedThread,                                                this,                                                           &SeerGdbWidget::handleGdbThreadSelectId);
    QObject::connect(threadManagerWidget->threadFramesBrowserWidget(),          &SeerThreadFramesBrowserWidget::refreshThreadFrames,                                        this,                                                           &SeerGdbWidget::handleGdbThreadListFrames);
    QObject::connect(threadManagerWidget->threadFramesBrowserWidget(),          &SeerThreadFramesBrowserWidget::selectedFile,                                               editorManagerWidget,                                            &SeerEditorManagerWidget::handleOpenFile);

    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::refreshBreakpointsList,                                      this,                                                           &SeerGdbWidget::handleGdbBreakpointWatchpointCatchpointList);
    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::deleteBreakpoints,                                           this,                                                           &SeerGdbWidget::handleGdbBreakpointDelete);
    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::enableBreakpoints,                                           this,                                                           &SeerGdbWidget::handleGdbBreakpointEnable);
    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::disableBreakpoints,                                          this,                                                           &SeerGdbWidget::handleGdbBreakpointDisable);
    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::insertBreakpoint,                                            this,                                                           &SeerGdbWidget::handleGdbBreakpointInsert);
    QObject::connect(_breakpointsBrowserWidget,                                 &SeerBreakpointsBrowserWidget::selectedFile,                                                editorManagerWidget,                                            &SeerEditorManagerWidget::handleOpenFile);

    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::refreshWatchpointsList,                                      this,                                                           &SeerGdbWidget::handleGdbBreakpointWatchpointCatchpointList);
    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::deleteWatchpoints,                                           this,                                                           &SeerGdbWidget::handleGdbWatchpointDelete);
    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::enableWatchpoints,                                           this,                                                           &SeerGdbWidget::handleGdbWatchpointEnable);
    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::disableWatchpoints,                                          this,                                                           &SeerGdbWidget::handleGdbWatchpointDisable);
    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::insertWatchpoint,                                            this,                                                           &SeerGdbWidget::handleGdbWatchpointInsert);
    QObject::connect(_watchpointsBrowserWidget,                                 &SeerWatchpointsBrowserWidget::selectedFile,                                                editorManagerWidget,                                            &SeerEditorManagerWidget::handleOpenFile);

    QObject::connect(_catchpointsBrowserWidget,                                 &SeerCatchpointsBrowserWidget::refreshCatchpointsList,                                      this,                                                           &SeerGdbWidget::handleGdbBreakpointWatchpointCatchpointList);
    QObject::connect(_catchpointsBrowserWidget,                                 &SeerCatchpointsBrowserWidget::deleteCatchpoints,                                           this,                                                           &SeerGdbWidget::handleGdbCatchpointDelete);
    QObject::connect(_catchpointsBrowserWidget,                                 &SeerCatchpointsBrowserWidget::enableCatchpoints,                                           this,                                                           &SeerGdbWidget::handleGdbCatchpointEnable);
    QObject::connect(_catchpointsBrowserWidget,                                 &SeerCatchpointsBrowserWidget::disableCatchpoints,                                          this,                                                           &SeerGdbWidget::handleGdbCatchpointDisable);
    QObject::connect(_catchpointsBrowserWidget,                                 &SeerCatchpointsBrowserWidget::insertCatchpoint,                                            this,                                                           &SeerGdbWidget::handleGdbCatchpointInsert);

    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       stackManagerWidget->stackFramesBrowserWidget(),                 &SeerStackFramesBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       stackManagerWidget->stackLocalsBrowserWidget(),                 &SeerStackLocalsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       stackManagerWidget->stackArgumentsBrowserWidget(),              &SeerStackArgumentsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       threadManagerWidget->threadIdsBrowserWidget(),                  &SeerThreadIdsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       threadManagerWidget->threadFramesBrowserWidget(),               &SeerThreadFramesBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       variableManagerWidget->registerValuesBrowserWidget(),           &SeerRegisterValuesBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       variableManagerWidget->variableTrackerBrowserWidget(),          &SeerVariableTrackerBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       _breakpointsBrowserWidget,                                      &SeerBreakpointsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       _watchpointsBrowserWidget,                                      &SeerWatchpointsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       _catchpointsBrowserWidget,                                      &SeerCatchpointsBrowserWidget::handleStoppingPointReached);
    QObject::connect(this,                                                      &SeerGdbWidget::stoppingPointReached,                                                       stackManagerWidget,                                             &SeerStackManagerWidget::handleStoppingPointReached);

    QObject::connect(leftCenterRightSplitter,                                   &QSplitter::splitterMoved,                                                                  this,                                                           &SeerGdbWidget::handleSplitterMoved);
    QObject::connect(sourceLibraryVariableManagerSplitter,                      &QSplitter::splitterMoved,                                                                  this,                                                           &SeerGdbWidget::handleSplitterMoved);
    QObject::connect(codeManagerLogTabsSplitter,                                &QSplitter::splitterMoved,                                                                  this,                                                           &SeerGdbWidget::handleSplitterMoved);
    QObject::connect(stackThreadManagersplitter,                                &QSplitter::splitterMoved,                                                                  this,                                                           &SeerGdbWidget::handleSplitterMoved);
    QObject::connect(manualCommandComboBox,                                     QOverload<int>::of(&QComboBox::activated),                                                  this,                                                           &SeerGdbWidget::handleManualCommandChanged);

    // Restore window settings.
    setConsoleMode("normal");
    readSettings();
}

SeerGdbWidget::~SeerGdbWidget () {

    deleteConsole();

    if (_gdbMonitor) {
        delete _gdbMonitor;
    }

    if (_gdbProcess) {
        _gdbProcess->kill();
        _gdbProcess->waitForFinished();
        delete _gdbProcess;
    }
}

GdbMonitor* SeerGdbWidget::gdbMonitor () {
    return _gdbMonitor;
}

QProcess* SeerGdbWidget::gdbProcess () {
    return _gdbProcess;
}

void SeerGdbWidget::setExecutableName (const QString& executableName) {

    _executableName = executableName;

    setNewExecutableFlag(true);
}

const QString& SeerGdbWidget::executableName () const {
    return _executableName;
}

void SeerGdbWidget::setNewExecutableFlag (bool flag) {
    _newExecutableFlag = flag;
}

bool SeerGdbWidget::newExecutableFlag () const {
    return _newExecutableFlag;
}

void SeerGdbWidget::setExecutableArguments (const QString& executableArguments) {

    _executableArguments = executableArguments;
}

const QString& SeerGdbWidget::executableArguments () const {
    return _executableArguments;
}

void SeerGdbWidget::setExecutableWorkingDirectory (const QString& executableWorkingDirectory) {

    _executableWorkingDirectory = executableWorkingDirectory;
}

const QString& SeerGdbWidget::executableWorkingDirectory () const {
    return _executableWorkingDirectory;
}

void SeerGdbWidget::setExecutablePid (int pid) {
    _executablePid = pid;
}

int SeerGdbWidget::executablePid () const {
    return _executablePid;
}

void SeerGdbWidget::setExecutableHostPort (const QString& hostPort) {
    _executableHostPort = hostPort;
}

const QString& SeerGdbWidget::executableHostPort () const {
    return _executableHostPort;
}

void SeerGdbWidget::setExecutableCoreFilename (const QString& coreFilename) {
    _executableCoreFilename = coreFilename;
}

const QString& SeerGdbWidget::executableCoreFilename () const {
    return _executableCoreFilename;
}

void SeerGdbWidget::setExecutableLaunchMode (const QString& launchMode) {
    _executableLaunchMode = launchMode;
}

const QString& SeerGdbWidget::executableLaunchMode () const {
    return _executableLaunchMode;
}

void SeerGdbWidget::setGdbProgram (const QString& program) {

    _gdbProgram = program;
}

QString SeerGdbWidget::gdbProgram () const {

    return _gdbProgram;
}

void SeerGdbWidget::setGdbArguments (const QString& arguments) {

    _gdbArguments = arguments;
}

QString SeerGdbWidget::gdbArguments () const {

    return _gdbArguments;
}

void SeerGdbWidget::setGdbAsyncMode (bool flag) {

    _gdbASyncMode = flag;
}

bool SeerGdbWidget::gdbAsyncMode () const {

    return _gdbASyncMode;
}

SeerEditorManagerWidget* SeerGdbWidget::editorManager () {

    return editorManagerWidget;
}

const SeerEditorManagerWidget* SeerGdbWidget::editorManager () const {

    return editorManagerWidget;
}

void SeerGdbWidget::handleText (const QString& text) {

    if (text.startsWith("*running,thread-id=\"all\"")) {

    // Probably a better way to handle all these types of stops.
    }else if (text.startsWith("*stopped,")) {
        emit stoppingPointReached();

    }else if (text.startsWith("=thread-group-started,")) {
        // =thread-group-started,id="i1",pid="30916"

        QString pid_text = Seer::parseFirst(text, "pid=", '"', '"', false);

        //qDebug() << "Inferior pid = " << pid_text;

        setExecutablePid(pid_text.toLong());

    }else{
        // All other text is ignored by this widget.
    }
}

void SeerGdbWidget::handleExecute () {

    //qDebug() << "QComboBox: " << manualCommandComboBox->currentText();

    QString command = manualCommandComboBox->currentText();
    manualCommandComboBox->clearEditText();

    handleGdbCommand(command);
}

void SeerGdbWidget::handleGdbCommand (const QString& command) {

    if (_gdbProcess->state() == QProcess::NotRunning) {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable has not been started yet or has already exited.\n\n") +
                                   "(" + command + ")",
                                   QMessageBox::Ok);
        return;
    }

    QString str = command + "\n";    // Ensure there's a trailing RETURN.

    QByteArray bytes = str.toUtf8(); // 8-bit Unicode Transformation Format

    _gdbProcess->write(bytes);       // Send the data into the stdin stream of the bash child process
}

void SeerGdbWidget::handleGdbExit () {
    handleGdbCommand("-gdb-exit");
}

void SeerGdbWidget::handleGdbRunExecutable () {

    // Has a executable name been provided?
    if (executableName() == "") {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable name has not been provided.\n\nUse File->Debug..."),
                                   QMessageBox::Ok);
        return;
    }

    // Kill previous gdb, if any.
    if (isGdbRuning() == true) {

        int result = QMessageBox::warning(this, "Seer",
                                          QString("The executable is already running.\n\nAre you sure to restart it?"),
                                          QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);

        if (result == QMessageBox::Cancel) {
            return;
        }

        killGdb();
    }

    // Delete old console.
    deleteConsole();

    // If gdb isn't running, start it.
    if (isGdbRuning() == false) {
        startGdb();

        if (gdbAsyncMode()) {
            handleGdbCommand("-gdb-set mi-async on"); // Turn on async mode so the 'interrupt' can happen.
        }
    }

    // Create a new console.
    createConsole();

    setExecutableLaunchMode("run");
    setExecutablePid(0);

    // Get list of breakpoints and catchpoints.
    QStringList breakpointsList;
    QStringList watchpointsList;
    QStringList catchpointsList;

    if (newExecutableFlag() == false) {
        breakpointsList = _breakpointsBrowserWidget->breakpointsText();
        watchpointsList = _watchpointsBrowserWidget->watchpointsText();
        catchpointsList = _catchpointsBrowserWidget->catchpointsText();
    }

    handleGdbExecutableName();              // Load the program into the gdb process.
    handleGdbExecutableSources();           // Load the program source files.
    handleGdbExecutableSharedLibraries();   // Load the program shared libraries.
    handleGdbExecutableArguments();         // Set the program's arguments before running.
    handleGdbExecutableWorkingDirectory();  // Set the program's working directory before running.
    handleGdbTtyDeviceName();               // Set the program's tty device for stdin and stdout.

    // Reload old breakpoints and catchpoints.
    if (newExecutableFlag() == false) {
        handleGdbBreakpointReload(breakpointsList);
      //handleGdbWatchpointReload(watchpointsList); // Doesn't work. The 'expression' is no longer in scope.
        handleGdbCatchpointReload(catchpointsList);
    }

    setNewExecutableFlag(false);

    handleGdbCommand("-exec-run");
}

void SeerGdbWidget::handleGdbStartExecutable () {

    // Has a executable name been provided?
    if (executableName() == "") {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable name has not been provided.\n\nUse File->Debug..."),
                                   QMessageBox::Ok);
        return;
    }

    //qDebug() << ": newExecutableFlag = " << newExecutableFlag();

    // Kill previous gdb, if any.
    if (isGdbRuning() == true) {

        int result = QMessageBox::warning(this, "Seer",
                                          QString("The executable is already running.\n\nAre you sure to restart it?"),
                                          QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);

        if (result == QMessageBox::Cancel) {
            return;
        }

        killGdb();
    }

    // Delete old console.
    deleteConsole();

    // If gdb isn't running, start it.
    if (isGdbRuning() == false) {
        startGdb();

        if (gdbAsyncMode()) {
            handleGdbCommand("-gdb-set mi-async on"); // Turn on async mode so the 'interrupt' can happen.
        }
    }

    // Create a new console.
    createConsole();

    setExecutableLaunchMode("start");
    setExecutablePid(0);

    // Get list of breakpoints.
    QStringList breakpointsList;
    QStringList watchpointsList;
    QStringList catchpointsList;

    if (newExecutableFlag() == false) {
        breakpointsList = _breakpointsBrowserWidget->breakpointsText();
        watchpointsList = _watchpointsBrowserWidget->watchpointsText();
        catchpointsList = _catchpointsBrowserWidget->catchpointsText();
    }

    handleGdbExecutableName();              // Load the program into the gdb process.
    handleGdbExecutableSources();           // Load the program source files.
    handleGdbExecutableSharedLibraries();   // Load the program shared libraries.
    handleGdbExecutableWorkingDirectory();  // Set the program's working directory before running.
    handleGdbExecutableArguments();         // Set the program's arguments before running.
    handleGdbTtyDeviceName();               // Set the program's tty device for stdin and stdout.

    // Reload old breakpoints.
    if (newExecutableFlag() == false) {
        handleGdbBreakpointReload(breakpointsList);
      //handleGdbWatchpointReload(watchpointsList); // Doesn't work. The 'expression' is no longer in scope.
        handleGdbCatchpointReload(catchpointsList);
    }

    setNewExecutableFlag(false);

    // Run the executable.
    handleGdbCommand("-exec-run --start");
}

void SeerGdbWidget::handleGdbAttachExecutable () {

    // Has a executable name been provided?
    if (executableName() == "") {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable name has not been provided.\n\nUse File->Debug..."),
                                   QMessageBox::Ok);
        return;
    }

    // Delete old console.
    deleteConsole();

    // Kill previous gdb, if any.
    if (isGdbRuning() == true) {
        killGdb();
    }

    // If gdb isn't running, start it.
    if (isGdbRuning() == false) {
        startGdb();

        if (gdbAsyncMode()) {
            handleGdbCommand("-gdb-set mi-async on"); // Turn on async mode so the 'interrupt' can happen.
        }
    }

    // Create a new console.
    createConsole();

    setExecutableLaunchMode("attach");

    handleGdbExecutableName();              // Load the program into the gdb process.
    handleGdbExecutableSources();           // Load the program source files.
    handleGdbExecutableSharedLibraries();   // Load the program shared libraries.
    handleGdbTtyDeviceName();               // Set the program's tty device for stdin and stdout.

    handleGdbCommand(QString("-target-attach %1").arg(executablePid()));
}

void SeerGdbWidget::handleGdbConnectExecutable () {

    // Has a executable name been provided?
    if (executableName() == "") {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable name has not been provided.\n\nUse File->Debug..."),
                                   QMessageBox::Ok);
        return;
    }

    // Delete old console.
    deleteConsole();

    // Kill previous gdb, if any.
    if (isGdbRuning() == true) {
        killGdb();
    }

    // If gdb isn't running, start it.
    if (isGdbRuning() == false) {
        startGdb();

        if (gdbAsyncMode()) {
            handleGdbCommand("-gdb-set mi-async on"); // Turn on async mode so the 'interrupt' can happen.
        }
    }

    // Create a new console.
    createConsole();

    setExecutableLaunchMode("connect");
    setExecutablePid(0);

    handleGdbTtyDeviceName();               // Set the program's tty device for stdin and stdout.

    handleGdbCommand(QString("-target-select extended-remote %1").arg(executableHostPort()));

    handleGdbExecutableName();              // Load the program into the gdb process.
    handleGdbExecutableSources();           // Load the program source files.
    handleGdbExecutableSharedLibraries();   // Load the program shared libraries.
    handleGdbCommand("-target-download");

    // "-file-symbol-file %s"
    // "-file-exec-file %s"
    // "-target-download"
}

void SeerGdbWidget::handleGdbCoreFileExecutable () {

    // Has a executable name been provided?
    if (executableName() == "") {
        QMessageBox::warning(this, "Seer",
                                   QString("The executable name has not been provided.\n\nUse File->Debug..."),
                                   QMessageBox::Ok);
        return;
    }

    // Delete old console.
    deleteConsole();

    // Kill previous gdb, if any.
    if (isGdbRuning() == true) {
        killGdb();
    }

    // If gdb isn't running, start it.
    if (isGdbRuning() == false) {
        startGdb();

        if (gdbAsyncMode()) {
            handleGdbCommand("-gdb-set mi-async on"); // Turn on async mode so the 'interrupt' can happen.
        }
    }

    // Create a new console.
    createConsole();

    setExecutableLaunchMode("corefile");
    setExecutablePid(0);

    handleGdbExecutableName();              // Load the program into the gdb process.
    handleGdbExecutableSources();           // Load the program source files.
    handleGdbExecutableSharedLibraries();   // Load the program shared libraries.
    handleGdbTtyDeviceName();               // Set the program's tty device for stdin and stdout.

    handleGdbCommand(QString("-target-select core %1").arg(executableCoreFilename()));
}

void SeerGdbWidget::handleGdbRunToLine (QString fullname, int lineno) {

    if (executableLaunchMode() == "") {
        return;
    }

    QString command = "-exec-until " + fullname + ":" + QString::number(lineno);

    handleGdbCommand(command);
}

void SeerGdbWidget::handleGdbNext () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-exec-next");
}

void SeerGdbWidget::handleGdbStep () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-exec-step");
}

void SeerGdbWidget::handleGdbFinish () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-exec-finish");
}

void SeerGdbWidget::handleGdbContinue () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-exec-continue");
}

void SeerGdbWidget::handleGdbInterrupt () {

    sendGdbInterrupt(-1);
}

void SeerGdbWidget::handleGdbInterruptSIGINT () {

    sendGdbInterrupt(SIGINT);
}

void SeerGdbWidget::handleGdbInterruptSIGKILL () {

    sendGdbInterrupt(SIGKILL);
}

void SeerGdbWidget::handleGdbInterruptSIGFPE () {

    sendGdbInterrupt(SIGFPE);
}

void SeerGdbWidget::handleGdbInterruptSIGSEGV () {

    sendGdbInterrupt(SIGSEGV);
}

void SeerGdbWidget::handleGdbInterruptSIGUSR1 () {

    sendGdbInterrupt(SIGUSR1);
}

void SeerGdbWidget::handleGdbInterruptSIGUSR2 () {

    sendGdbInterrupt(SIGUSR2);
}

void SeerGdbWidget::handleGdbExecutableSources () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-file-list-exec-source-files");
}

void SeerGdbWidget::handleGdbExecutableSharedLibraries () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-file-list-shared-libraries");
}

void SeerGdbWidget::handleGdbExecutableName () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString("-file-exec-and-symbols ") + executableName());
}

void SeerGdbWidget::handleGdbExecutableArguments () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString("-exec-arguments ") + executableArguments());
}

void SeerGdbWidget::handleGdbExecutableWorkingDirectory () {

    if (executableLaunchMode() == "") {
        return;
    }

    if (executableWorkingDirectory() != "") {
        handleGdbCommand(QString("-environment-cd ") + executableWorkingDirectory());
    }
}

void SeerGdbWidget::handleGdbTtyDeviceName () {

    if (_consoleWidget->ttyDeviceName() != "") {

        //qDebug() << "Setting TTY name to" << _consoleWidget->ttyDeviceName();

        handleGdbCommand(QString("-inferior-tty-set  ") + _consoleWidget->ttyDeviceName());
    }
}

void SeerGdbWidget::handleGdbStackListFrames () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-stack-list-frames");
}

void SeerGdbWidget::handleGdbStackSelectFrame (int frameno) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString("-stack-select-frame %1").arg(frameno));

    emit stoppingPointReached();
}

void SeerGdbWidget::handleGdbStackListLocals () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-stack-list-variables --all-values");
}

void SeerGdbWidget::handleGdbStackListArguments () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-stack-list-arguments --all-values");
}

void SeerGdbWidget::handleGdbBreakpointWatchpointCatchpointList () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-list");
}

void SeerGdbWidget::handleGdbBreakpointDelete (QString breakpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-delete " + breakpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbBreakpointEnable (QString breakpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-enable " + breakpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbBreakpointDisable (QString breakpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-disable " + breakpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbBreakpointInsert (QString breakpoint) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-insert " + breakpoint);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbBreakpointReload (QStringList breakpointsText) {

      for (int i=0; i<breakpointsText.size(); i++) {
           handleGdbBreakpointInsert(breakpointsText[i]);
      }
}

void SeerGdbWidget::handleGdbWatchpointReload (QStringList watchpointsText) {

      for (int i=0; i<watchpointsText.size(); i++) {
           handleGdbWatchpointInsert(watchpointsText[i]);
      }
}

void SeerGdbWidget::handleGdbCatchpointReload (QStringList catchpointsText) {

      for (int i=0; i<catchpointsText.size(); i++) {
           handleGdbCatchpointInsert(catchpointsText[i]);
      }
}

void SeerGdbWidget::handleGdbWatchpointDelete (QString watchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-delete " + watchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbWatchpointEnable (QString watchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-enable " + watchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbWatchpointDisable (QString watchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-disable " + watchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbWatchpointInsert (QString watchpoint) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-watch " + watchpoint);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbCatchpointDelete (QString catchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-delete " + catchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbCatchpointEnable (QString catchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-enable " + catchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbCatchpointDisable (QString catchpoints) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-break-disable " + catchpoints);
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbCatchpointInsert (QString catchpoint) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-catch-" + catchpoint); // A little bit different than break insert or watch insert.
    handleGdbBreakpointWatchpointCatchpointList();
}

void SeerGdbWidget::handleGdbThreadListIds () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-thread-list-ids");
}

void SeerGdbWidget::handleGdbThreadListFrames () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-thread-info");
}

void SeerGdbWidget::handleGdbThreadSelectId (int threadid) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString("-thread-select %1").arg(threadid));

    emit stoppingPointReached();
}

void SeerGdbWidget::handleGdbRegisterListNames () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-data-list-register-names");
}

void SeerGdbWidget::handleGdbRegisterListValues () {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand("-data-list-register-values N");
}

void SeerGdbWidget::handleGdbDataEvaluateExpression (int expressionid, QString expression) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString::number(expressionid) + "-data-evaluate-expression \"" + expression + "\"");
}

void SeerGdbWidget::handleGdbDataListValues () {

    if (executableLaunchMode() == "") {
        return;
    }

    for (int i=0; i<_dataExpressionId.size(); i++) {
        handleGdbCommand(QString::number(_dataExpressionId[i]) + "-data-evaluate-expression \"" + _dataExpressionName[i] + "\"");
    }
}

void SeerGdbWidget::handleGdbDataListExpressions () {

    if (executableLaunchMode() == "") {
        return;
    }

    // ^done,DataExpressionTable={
    //     entry={id="2",expression="a"},
    //     entry={id="3",expression="i"}
    // }

    QString text;
    text += "^done,DataExpressionTable={";

    for (int i=0; i<_dataExpressionId.size(); i++) {
        if (i != 0) {
            text += ",";
        }
        text += "entry={id=\"" + QString::number(_dataExpressionId[i]) + "\",expression=\"" + _dataExpressionName[i] + "\"}";
    }

    text += "}";

    gdbMonitor()->handleTextOutput(text);
}

void SeerGdbWidget::handleGdbDataAddExpression (QString expression) {

    if (executableLaunchMode() == "") {
        return;
    }

    // Is this one already present?
    // ??? Emit an error?
    if (_dataExpressionName.indexOf(expression) >= 0) {
        return;
    }

    // Add it to our list of variables.
    _dataExpressionName.push_back(expression);
    _dataExpressionId.push_back(Seer::createID());

    // ^done,DataExpressionAdded={
    //     id="2",
    //     expression="a"
    // }

    QString text = QString("^done,DataExpressionAdded={id=\"%1\",expression=\"%2\"}").arg(_dataExpressionId.back()).arg(_dataExpressionName.back());

    //qDebug() << text;

    gdbMonitor()->handleTextOutput(text);
}

void SeerGdbWidget::handleGdbDataDeleteExpressions (QString expressionids) {

    if (executableLaunchMode() == "") {
        return;
    }

    // ^done,DataExpressionDeleted={
    //     entry={id="2", expression="a"},
    //     entry={id="3", expression="a"}
    // }

    QString text;
    text += "^done,DataExpressionDeleted={";

    if (expressionids == "*") {

        bool first = true;
        for (int i=0; i<_dataExpressionId.size(); i++) {

            if (first == false) {
                text += ",";
            }

            text += "entry={id=\"" + QString::number(_dataExpressionId[i]) + "\",expression=\"" + _dataExpressionName[i] + "\"}";
            first = false;
        }

        _dataExpressionId.clear();
        _dataExpressionName.clear();

    }else{

        QStringList ids = expressionids.split(' ', QString::SkipEmptyParts);

        bool first = true;
        for (int i=0; i<ids.size(); i++) {

            if (ids[i] == "") {
                continue;
            }

            int index = _dataExpressionId.indexOf(ids[i].toInt());

            if (index < 0) {
                continue;
            }

            if (first == false) {
                text += ",";
            }

            text += "entry={id=\"" + QString::number(_dataExpressionId[index]) + "\",expression=\"" + _dataExpressionName[index] + "\"}";
            first = false;

            _dataExpressionId.remove(index);
            _dataExpressionName.remove(index);
        }
    }

    text += "}";

    gdbMonitor()->handleTextOutput(text);
}

void SeerGdbWidget::handleGdbMemoryAddExpression (QString expression) {

    Q_UNUSED(expression);

    if (executableLaunchMode() == "") {
        return;
    }

    SeerMemoryVisualizerWidget* w = new SeerMemoryVisualizerWidget(0);
    w->show();

    // Connect things.
    QObject::connect(_gdbMonitor,  &GdbMonitor::astrixTextOutput,                           w,    &SeerMemoryVisualizerWidget::handleText);
    QObject::connect(_gdbMonitor,  &GdbMonitor::caretTextOutput,                            w,    &SeerMemoryVisualizerWidget::handleText);
    QObject::connect(w,            &SeerMemoryVisualizerWidget::evaluateVariableExpression, this, &SeerGdbWidget::handleGdbDataEvaluateExpression);
    QObject::connect(w,            &SeerMemoryVisualizerWidget::evaluateMemoryExpression,   this, &SeerGdbWidget::handleGdbMemoryEvaluateExpression);

    // Tell the visualizer what variable to use.
    w->setVariableName(expression);
}

void SeerGdbWidget::handleGdbMemoryEvaluateExpression (int expressionid, QString address, int count) {

    if (executableLaunchMode() == "") {
        return;
    }

    handleGdbCommand(QString::number(expressionid) + "-data-read-memory-bytes " + address + " " + QString::number(count));
}

void SeerGdbWidget::handleGdbMemoryVisualizer () {
    handleGdbMemoryAddExpression("");
}

void SeerGdbWidget::handleGdbArrayAddExpression (QString expression) {

    Q_UNUSED(expression);

    if (executableLaunchMode() == "") {
        return;
    }

    SeerArrayVisualizerWidget* w = new SeerArrayVisualizerWidget(0);
    w->show();

    // Connect things.
    QObject::connect(_gdbMonitor,  &GdbMonitor::astrixTextOutput,                           w,    &SeerArrayVisualizerWidget::handleText);
    QObject::connect(_gdbMonitor,  &GdbMonitor::caretTextOutput,                            w,    &SeerArrayVisualizerWidget::handleText);
    QObject::connect(w,            &SeerArrayVisualizerWidget::evaluateVariableExpression,  this, &SeerGdbWidget::handleGdbDataEvaluateExpression);
    QObject::connect(w,            &SeerArrayVisualizerWidget::evaluateMemoryExpression,    this, &SeerGdbWidget::handleGdbMemoryEvaluateExpression);

    // Tell the visualizer what variable to use.
    w->setVariableName(expression);
}

void SeerGdbWidget::handleGdbArrayVisualizer () {
    handleGdbArrayAddExpression("");
}

void SeerGdbWidget::handleSplitterMoved (int pos, int index) {

    //qDebug() << "Splitter moved to " << pos << index;

    writeSettings();
}

void SeerGdbWidget::handleManualCommandChanged () {

    //qDebug() << "Manual Command ComboBox changed";

    writeSettings();
}

void SeerGdbWidget::handleGdbProcessFinished (int exitCode, QProcess::ExitStatus exitStatus) {

    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    //qDebug() << "Gdb process finished. Exit code =" << exitCode << "Exit status =" << exitStatus;
}

void SeerGdbWidget::handleGdbProcessErrored (QProcess::ProcessError errorStatus) {

    Q_UNUSED(errorStatus);

    //qDebug() << "Error launching gdb process. Error =" << errorStatus;

    if (errorStatus == QProcess::FailedToStart) {
        QMessageBox::warning(this, "Seer",
                                   QString("Unable to launch the GDB program.\n\n") +
                                   QString("(%1 %2)").arg(gdbProgram()).arg(gdbArguments()),
                                   QMessageBox::Ok);
    }
}

void SeerGdbWidget::writeSettings () {

    QSettings settings;

    settings.beginGroup("mainwindowsplitters"); {
        settings.setValue("leftCenterRightSplitter",              leftCenterRightSplitter->saveState());
        settings.setValue("codeManagerLogTabsSplitter",           codeManagerLogTabsSplitter->saveState());
        settings.setValue("sourceLibraryVariableManagerSplitter", sourceLibraryVariableManagerSplitter->saveState());
        settings.setValue("stackThreadManagersplitter",           stackThreadManagersplitter->saveState());
    } settings.endGroup();

    settings.beginGroup("consolewindow"); {
        settings.setValue("start", consoleMode());
    }settings.endGroup();

    settings.beginWriteArray("manualgdbcommandshistory"); {

        QStringList commands = manualCommands(rememberManualCommandCount());

        for (int i = 0; i < commands.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue("command", commands[i]);
        }

    } settings.endArray();

    settings.beginWriteArray("sourcealternatedirectories"); {

        QStringList directories = sourceAlternateDirectories();

        for (int i = 0; i < directories.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue("directory", directories[i]);
        }

    } settings.endArray();
}

void SeerGdbWidget::readSettings () {

    QSettings settings;

    settings.beginGroup("mainwindowsplitters"); {
        leftCenterRightSplitter->restoreState(settings.value("leftCenterRightSplitter").toByteArray());
        codeManagerLogTabsSplitter->restoreState(settings.value("codeManagerLogTabsSplitter").toByteArray());
        sourceLibraryVariableManagerSplitter->restoreState(settings.value("sourceLibraryVariableManagerSplitter").toByteArray());
        stackThreadManagersplitter->restoreState(settings.value("stackThreadManagersplitter").toByteArray());
    } settings.endGroup();

    settings.beginGroup("consolewindow"); {
        setConsoleMode(settings.value("start", "normal").toString());
    } settings.endGroup();

    int size = settings.beginReadArray("manualgdbcommandshistory"); {

        QStringList commands;

        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);

            commands << settings.value("command").toString();
        }

        setManualCommands(commands);
    } settings.endArray();

    size = settings.beginReadArray("sourcealternatedirectories"); {

        QStringList directories;

        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);

            directories << settings.value("directory").toString();
        }

        setSourceAlternateDirectories(directories);

    } settings.endArray();
}

bool SeerGdbWidget::isGdbRuning () const {

    if (_gdbProcess->state() == QProcess::NotRunning) {
        return false;
    }

    return true;
}

void SeerGdbWidget::startGdb () {

    // Don't do anything, if already running.
    if (isGdbRuning()) {
        qDebug() << "Already running";

        return;
    }

    // Set the gdb program name to use.
    _gdbProcess->setProgram(gdbProgram());

    // Build the gdb argument list.
    QStringList args = gdbArguments().split(' ', QString::SkipEmptyParts);

    // Give the gdb process the argument list.
    _gdbProcess->setArguments(args);

    // Start the gdb process.
    _gdbProcess->start();
}

void SeerGdbWidget::killGdb () {

    // Don't do anything, if already running.
    if (isGdbRuning() == false) {
        return;
    }

    // Kill the process.
    _gdbProcess->kill();

    // Wait for it to end.
    _gdbProcess->waitForFinished();

    // Sanity check.
    if (isGdbRuning()) {
        qDebug() << "Is running but shouldn't be.";
    }

    // Clear the launch mode.
    setExecutableLaunchMode("");
}

void SeerGdbWidget::createConsole () {

    deleteConsole(); // Delete old console, if any.

    if (_consoleWidget == 0) {
        _consoleWidget = new SeerConsoleWidget(0);
        _consoleWidget->setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

        setConsoleMode(consoleMode());
    }
}

void SeerGdbWidget::deleteConsole () {

    if (_consoleWidget) {
        delete _consoleWidget;
        _consoleWidget = 0;
    }
}

void SeerGdbWidget::setConsoleMode (const QString& mode) {

    _consoleMode = mode;

    if (_consoleWidget != 0) {
        if (_consoleMode == "normal") {
            _consoleWidget->show();
            _consoleWidget->setWindowState(Qt::WindowNoState);

        }else if (_consoleMode == "minimized") {
            _consoleWidget->show();
            _consoleWidget->setWindowState(Qt::WindowMinimized);

        }else if (_consoleMode == "hidden") {
            _consoleWidget->hide();

        }else if (_consoleMode == "") {
            _consoleMode = "normal";
            _consoleWidget->show();
            _consoleWidget->setWindowState(Qt::WindowNoState);

        }else{
        }
    }
}

QString SeerGdbWidget::consoleMode () const {

    if (_consoleMode == "") {
        return "normal";
    }

    return _consoleMode;
}

void SeerGdbWidget::setManualCommands (const QStringList& commands) {

    manualCommandComboBox->clear();

    manualCommandComboBox->addItems(commands);
}

QStringList SeerGdbWidget::manualCommands(int count) const {

    //qDebug() << "Count =" << count;

    // Select all if a zero.
    if (count == 0) {
        count = manualCommandComboBox->count();
    }

    // No more than count.
    if (count > manualCommandComboBox->count()) {
        count = manualCommandComboBox->count();
    }

    // Calculate starting position in list.
    int index = manualCommandComboBox->count() - count;

    // Get the list.
    QStringList list;

    for (; index<count; index++) {
        list << manualCommandComboBox->itemText(index);
    }

    return list;
}

void SeerGdbWidget::setRememberManualCommandCount (int count) {

    _rememberManualCommandCount = count;
}

int SeerGdbWidget::rememberManualCommandCount () const {

    return _rememberManualCommandCount;
}

void SeerGdbWidget::clearManualCommandHistory () {

    // Zap the entries in the combobox.
    manualCommandComboBox->clear();

    // Write the settings.
    writeSettings();
}

const QStringList& SeerGdbWidget::sourceAlternateDirectories() const {

    return editorManager()->editorAlternateDirectories();
}

void SeerGdbWidget::setSourceAlternateDirectories (const QStringList& alternateDirectories) {

    editorManager()->setEditorAlternateDirectories(alternateDirectories);
}

void SeerGdbWidget::sendGdbInterrupt (int signal) {

    //qDebug() << "Sending an interrupt to the program. Signal =" << signal;

    if (executableLaunchMode() == "") {
        return;
    }

    if (executablePid() < 1) {
        QMessageBox::warning(this, "Seer",
                                   QString("No executable is running or I don't know the PID of the process."),
                                   QMessageBox::Ok);
        return;
    }

    // Use kill() to send the signal to the inferior.
    // -exec-interrupt does not work for -exec-until when the line
    // number is not in the current function. In this case, -exec-until
    // behaves like -exec-continue but -exec-interrupt has no effect. :^(
    // We do kill the ability to use a different signal, though. :^)

    if (signal < 0) {
        handleGdbCommand("-exec-interrupt");

    }else{
        int stat = kill(executablePid(), signal);
        if (stat < 0) {
            QMessageBox::warning(this, "Seer",
                                       QString("Unable to send signal '%1' to pid %2.\nError = '%3'").arg(strsignal(signal)).arg(executablePid()).arg(strerror(errno)),
                                       QMessageBox::Ok);
        }
    }
}

