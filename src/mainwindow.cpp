/***
 * Copyright (c) 2018, Robert Alm Nilsson
 * Copyright (c) 2013, Dan Hasting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the organization nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***/

#include "mainwindow.h"

#include "global.h"
#include "common.h"
#include "error.h"
#include "core.h"
#include "settings.h"

#include "dialogs/aboutguidialog.h"
#include "dialogs/cheatdialog.h"
#include "dialogs/configeditor.h"
#include "dialogs/downloaddialog.h"
#include "dialogs/gamesettingsdialog.h"
#include "dialogs/logdialog.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/inputdialog.h"

#include "emulation/glwindow.h"
#include "emulation/emulation.h"

#include "roms/romcollection.h"
#include "roms/thegamesdbscraper.h"

#include "views/gridview.h"
#include "views/listview.h"
#include "views/tableview.h"

#include "osal/osal_preproc.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QApplication>
#include <QDesktopWidget>
#include <QActionGroup>

extern Emulation emulation;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(AppName);
    setWindowIcon(QIcon(":/images/"+AppNameLower+".png"));
    installEventFilter(this);

    autoloadSettings();

    romCollection = new RomCollection(QStringList() << "*.z64" << "*.v64" << "*.n64" << "*.zip",
                                      QStringList() << SETTINGS.value("Paths/roms","").toString().split("|"),
                                      this);
    createMenu();
    createRomView();

    connect(&emulation, SIGNAL(started()),
            this, SLOT(disableButtons()),
            Qt::BlockingQueuedConnection);
    connect(&emulation, SIGNAL(resumed()), this, SLOT(emulationResumed()));
    connect(&emulation, SIGNAL(paused()), this, SLOT(emulationPaused()));
    connect(&emulation, SIGNAL(toggleFullscreen()), this, SLOT(toggleFullscreen()));
    connect(&emulation, SIGNAL(destroyGlWindow()),
            this, SLOT(destroyGlWindow()),
            Qt::BlockingQueuedConnection);
    connect(&emulation, SIGNAL(finished()), this, SLOT(enableButtons()));
    connect(&emulation, SIGNAL(createGlWindow(QSurfaceFormat*)),
            this, SLOT(createGlWindow(QSurfaceFormat*)),
            Qt::BlockingQueuedConnection);
    connect(&emulation, SIGNAL(resize(int, int)),
            this, SLOT(resizeWindow(int, int)),
            Qt::BlockingQueuedConnection);

    connect(romCollection, SIGNAL(updateStarted(bool)), this, SLOT(disableViews(bool)));
    connect(romCollection, SIGNAL(romAdded(Rom*, int)), this, SLOT(addToView(Rom*, int)));
    connect(romCollection, SIGNAL(updateEnded(int, bool)), this, SLOT(enableViews(int, bool)));

    romCollection->cachedRoms(false, true);


    setMenuBar(menuBar);

    mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);
    setGeometry(QRect(SETTINGS.value("Geometry/windowx", 0).toInt(),
                      SETTINGS.value("Geometry/windowy", 0).toInt(),
                      SETTINGS.value("Geometry/width", 900).toInt(),
                      SETTINGS.value("Geometry/height", 600).toInt()));

    if (SETTINGS.value("View/fullscreen", "").toString() == "true") {
        updateFullScreenMode();
    }

    mainLayout = new QVBoxLayout(mainWidget);

    mainLayout->addWidget(emptyView);
    mainLayout->addWidget(tableView);
    mainLayout->addWidget(gridView);
    mainLayout->addWidget(listView);
    mainLayout->addWidget(disabledView);

    mainLayout->setMargin(0);

    mainWidget->setLayout(mainLayout);
    mainWidget->setMinimumSize(300, 200);
}


void MainWindow::createGlWindow(QSurfaceFormat *format)
{
    mainGeometry = saveGeometry();
    extern GlWindow *glWindow;
    glWindow = new GlWindow;
    QWidget *container = QWidget::createWindowContainer(glWindow);
    container->setFocusPolicy(Qt::StrongFocus);
    bool hide = SETTINGS.value("Graphics/hideCursor", "false").toString() == "true";
    if (hide) {
        glWindow->setCursor(Qt::BlankCursor);
    }
    glWindow->setFormat(*format);
    mainWidget = takeCentralWidget();
    setCentralWidget(container);
    container->setFocus();
    int x = SETTINGS.value("Geometry/gameWindowx", -1).toInt();
    int y = SETTINGS.value("Geometry/gameWindowy", -1).toInt();
    if (x != -1 && y != -1) {
        QRect r = geometry();
        r.setX(x);
        r.setY(y);
        setGeometry(r);
    } else {
        move(QApplication::desktop()->availableGeometry().center()
                - rect().center());
    }

    if (SETTINGS.value("Graphics/fullscreen", "") == "true") {
        QMainWindow::menuBar()->setHidden(true);
        showFullScreen();
    }

    m64p_rom_settings romSettings;
    emulation.getRomSettings(sizeof romSettings, &romSettings);
    setWindowTitle(QString(romSettings.goodname) + " - " + AppName);

    pauseAction->setVisible(true);
    resumeAction->setVisible(false);
    startAction->setVisible(false);
    while (!glWindow->isValid()) {
        QCoreApplication::processEvents();
    }
}


void MainWindow::destroyGlWindow()
{
    extern GlWindow *glWindow;
    glWindow->destroy();
    glWindow = NULL;
    setCentralWidget(mainWidget);
    SETTINGS.setValue("Geometry/gameWindowx", geometry().x());
    SETTINGS.setValue("Geometry/gameWindowy", geometry().y());
    if (SETTINGS.value("Graphics/fullscreen", "") == "true") {
        // Don't know why but have to go fullscreen before going back,
        // otherwise it doesn't go back properly.
        showFullScreen();
        showNormal();
        QMainWindow::menuBar()->setHidden(false);
    }
    restoreGeometry(mainGeometry);
    setWindowTitle(AppName);
    pauseAction->setVisible(false);
    resumeAction->setVisible(false);
    startAction->setVisible(true);
}


void MainWindow::resizeWindow(int width, int height)
{
    int h = height;
    if (!menuBar->isNativeMenuBar()) {
        h += menuBar->height();
    }
    resize(width, h);
}


void MainWindow::addToView(Rom *currentRom, int count)
{
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    if (visibleLayout == "table") {
        tableView->addToTableView(currentRom);
    } else if (visibleLayout == "grid") {
        gridView->addToGridView(currentRom, count, false);
    } else if (visibleLayout == "list") {
        listView->addToListView(currentRom, count, false);
    }
}


void MainWindow::autoloadSettings()
{
    QString pluginPath = SETTINGS.value("Paths/plugins", "").toString();
    QString dataPath = SETTINGS.value("Paths/data", "").toString();

#ifdef OS_LINUX_OR_BSD
    // If user has not entered any settings, check common locations for them

    if (pluginPath == "") {
        QStringList pluginCheck = {
            "/usr/local/lib/mupen64plus",
            "/usr/lib64/mupen64plus/mupen64plus",
            "/usr/lib/mupen64plus/mupen64plus",
            "/usr/lib/i386-linux-gnu/mupen64plus",
            "/usr/lib/x86_64-linux-gnu/mupen64plus",
            "/usr/lib64/mupen64plus",
            "/usr/lib/mupen64plus",
        };

        foreach (QString check, pluginCheck) {
            QDir dir(check);
            QStringList files = dir.entryList({"mupen64plus-*.so"});
            if (!files.isEmpty()) {
                pluginPath = dir.path();
                SETTINGS.setValue("Paths/plugins", pluginPath);
                break;
            }
        }
    }

    if (dataPath == "") {
        QStringList dataCheck = {
            "/usr/local/share/mupen64plus",
            "/usr/share/games/mupen64plus",
            "/usr/share/mupen64plus",
        };

        foreach (QString check, dataCheck) {
            if (QFileInfo(check+"/mupen64plus.ini").exists()) {
                dataPath = check;
                SETTINGS.setValue("Paths/data", dataPath);
                break;
            }
        }
    }
#endif

    QDir currentDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    if (pluginPath == "") {
        if (!currentDir.entryList({"mupen64plus-*.dll"}).isEmpty()) {
            pluginPath = currentDir.path();
            SETTINGS.setValue("Paths/plugins", pluginPath);
        }
    }

    if (dataPath == "") {
        if (currentDir.exists("mupen64plus.ini")) {
            dataPath = currentDir.path();
            SETTINGS.setValue("Paths/data", dataPath);
        }
    }
#endif


    // Set default plugins

    QString p;
    p = getCurrentVideoPlugin();
    if (p == "") {
        p = getAvailableVideoPlugins().value(0);
    }
    if (p != "") {
        SETTINGS.setValue("Plugins/video", p);
    }

    p = getCurrentAudioPlugin();
    if (p == "") {
        p = getAvailableAudioPlugins().value(0);
    }
    if (p != "") {
        SETTINGS.setValue("Plugins/audio", p);
    }

    p = getCurrentInputPlugin();
    if (p == "") {
        p = getAvailableInputPlugins().value(0);
    }
    if (p != "") {
        SETTINGS.setValue("Plugins/input", p);
    }

    p = getCurrentRspPlugin();
    if (p == "") {
        p = getAvailableRspPlugins().value(0);
    }
    if (p != "") {
        SETTINGS.setValue("Plugins/rsp", p);
    }


    // Check default location for mupen64plus.cfg in case user wants to use editor
    QString configPath = SETTINGS.value("Paths/config", "").toString();

    if (configPath == "") {
#if QT_VERSION >= 0x050000
        QString homeDir = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).first();
#else
        QString homeDir = QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
#endif

#ifdef Q_OS_WIN
        QString configCheck = homeDir + "/AppData/Roaming/Mupen64Plus/";
#else
        QString configCheck = homeDir + "/.config/mupen64plus";
#endif

        if (QFileInfo(configCheck+"/mupen64plus.cfg").exists()) {
            SETTINGS.setValue("Paths/config", configCheck);
        }
    }
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if (emulation.isExecuting()) {
        emulation.stopGame();
        while (emulation.isExecuting()) {
            QCoreApplication::processEvents();
        }
        restoreGeometry(mainGeometry);
    }
    SETTINGS.setValue("Geometry/windowx", geometry().x());
    SETTINGS.setValue("Geometry/windowy", geometry().y());
    SETTINGS.setValue("Geometry/width", geometry().width());
    SETTINGS.setValue("Geometry/height", geometry().height());
    if (isMaximized()) {
        SETTINGS.setValue("Geometry/maximized", true);
    } else {
        SETTINGS.setValue("Geometry/maximized", "");
    }

    tableView->saveColumnWidths();

    event->accept();
}


void MainWindow::createMenu()
{
    menuBar = new QMenuBar(this);


    // File
    fileMenu = new QMenu(tr("&File"), this);
    openAction = fileMenu->addAction(tr("&Open ROM..."));
    fileMenu->addSeparator();
    refreshAction = fileMenu->addAction(tr("&Refresh List"));
    downloadAction = fileMenu->addAction(tr("&Download/Update Info..."));
    deleteAction = fileMenu->addAction(tr("D&elete Current Info..."));
#ifndef Q_OS_OSX
    // OSX does not show the quit action so the separator is unneeded
    fileMenu->addSeparator();
#endif
    quitAction = fileMenu->addAction(tr("&Quit"));

    openAction->setShortcut(Qt::CTRL + Qt::Key_O);
#ifdef OS_LINUX_OR_BSD
    quitAction->setShortcut(Qt::CTRL + Qt::Key_Q);
#endif

    openAction->setIcon(QIcon::fromTheme("document-open"));
    refreshAction->setIcon(QIcon::fromTheme("view-refresh"));
    quitAction->setIcon(QIcon::fromTheme("application-exit"));

    downloadAction->setEnabled(false);
    deleteAction->setEnabled(false);

    menuBar->addMenu(fileMenu);

    connect(openAction, SIGNAL(triggered()), this, SLOT(openRom()));
    connect(refreshAction, SIGNAL(triggered()), romCollection, SLOT(addRoms()));
    connect(downloadAction, SIGNAL(triggered()), this, SLOT(openDownloader()));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(openDeleteDialog()));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(close()));


    // Emulation
    emulationMenu = new QMenu(tr("&Emulation"), this);
    startAction = emulationMenu->addAction(tr("&Start"));
    resumeAction = emulationMenu->addAction(tr("Re&sume"));
    pauseAction = emulationMenu->addAction(tr("Pau&se"));
    frameAction = emulationMenu->addAction(tr("Advance &frame"));
    resetAction = emulationMenu->addAction(tr("&Reset"));
    stopAction = emulationMenu->addAction(tr("St&op"));
    emulationMenu->addSeparator();
    saveStateAction = emulationMenu->addAction(tr("S&ave state"));
    loadStateAction = emulationMenu->addAction(tr("&Load state"));
    slotMenu = emulationMenu->addMenu(tr("Select slo&t"));
    QActionGroup *agroup = new QActionGroup(this);
    for (int i = 0; i < 10; i++) {
        QAction *a = slotMenu->addAction("Slot " + QString::number(i));
        a->setActionGroup(agroup);
        a->setCheckable(true);
        a->setShortcut(Qt::Key_0 + i);
        connect(a, &QAction::triggered, [i](bool checked) {
            if (checked) {
                emulation.setSaveSlot(i);
            }
        });
    }
    cheatsAction = emulationMenu->addAction(tr("&Cheats..."));

    {
        QList<QKeySequence> seq;
        seq << Qt::Key_P << Qt::Key_F2;
        resumeAction->setShortcuts(seq);
        pauseAction->setShortcuts(seq);
    }
    {
        QList<QKeySequence> seq;
        seq << Qt::Key_F9 << Qt::Key_F1;
        resetAction->setShortcuts(seq);
    }
    frameAction->setShortcut(Qt::Key_Period);
    stopAction->setShortcut(Qt::Key_Escape);
    saveStateAction->setShortcut(Qt::Key_F5);
    loadStateAction->setShortcut(Qt::Key_F7);

    startAction->setIcon(QIcon::fromTheme("media-playback-start"));
    resumeAction->setIcon(QIcon::fromTheme("media-playback-start"));
    pauseAction->setIcon(QIcon::fromTheme("media-playback-pause"));
    frameAction->setIcon(QIcon::fromTheme("go-next"));
    stopAction->setIcon(QIcon::fromTheme("media-playback-stop"));

    startAction->setEnabled(false);
    resumeAction->setVisible(false);
    pauseAction->setVisible(false);
    frameAction->setEnabled(false);
    resetAction->setEnabled(false);
    stopAction->setEnabled(false);
    saveStateAction->setEnabled(false);
    loadStateAction->setEnabled(false);

    menuBar->addMenu(emulationMenu);

    connect(startAction, SIGNAL(triggered()), this, SLOT(launchRomFromMenu()));
    connect(resumeAction, SIGNAL(triggered()), &emulation, SLOT(play()));
    connect(pauseAction, SIGNAL(triggered()), &emulation, SLOT(pause()));
    connect(frameAction, SIGNAL(triggered()), &emulation, SLOT(advanceFrame()));
    connect(resetAction, SIGNAL(triggered()), &emulation, SLOT(resetSoft()));
    connect(saveStateAction, SIGNAL(triggered()), &emulation, SLOT(saveState()));
    connect(loadStateAction, SIGNAL(triggered()), &emulation, SLOT(loadState()));
    connect(stopAction, SIGNAL(triggered()), this, SLOT(stopEmulator()));
    connect(cheatsAction, SIGNAL(triggered()), this, SLOT(showCheats()));


    // Settings
    settingsMenu = new QMenu(tr("&Settings"), this);
    configureGameAction = settingsMenu->addAction(tr("Configure &Game..."));
    settingsMenu->addSeparator();
    editorAction = settingsMenu->addAction(tr("Edit mupen64plus.cfg..."));
    settingsMenu->addSeparator();
    configInputAction = settingsMenu->addAction(tr("Configure &input..."));
    pluginsAction = settingsMenu->addAction(tr("Plugins..."));
    configureAction = settingsMenu->addAction(tr("&Configure..."));
    configureAction->setIcon(QIcon::fromTheme("preferences-other"));

    configureGameAction->setEnabled(false);

    menuBar->addMenu(settingsMenu);

    connect(pluginsAction, SIGNAL(triggered()), this, SLOT(openPlugins()));
    connect(configInputAction, SIGNAL(triggered()), this, SLOT(openInputConfig()));
    connect(editorAction, SIGNAL(triggered()), this, SLOT(openEditor()));
    connect(configureGameAction, SIGNAL(triggered()), this, SLOT(openGameSettings()));
    connect(configureAction, SIGNAL(triggered()), this, SLOT(openSettings()));


    // View
    viewMenu = new QMenu(tr("&View"), this);
    layoutMenu = viewMenu->addMenu(tr("&Layout"));
    layoutGroup = new QActionGroup(this);
    logAction = viewMenu->addAction(tr("View Log..."));

    QList<QStringList> layouts;
    layouts << (QStringList() << tr("None")       << "none")
            << (QStringList() << tr("Table View") << "table")
            << (QStringList() << tr("Grid View")  << "grid")
            << (QStringList() << tr("List View")  << "list");

    QString layoutValue = SETTINGS.value("View/layout", "table").toString();

    foreach (QStringList layoutName, layouts) {
        QAction *layoutItem = layoutMenu->addAction(layoutName.at(0));
        layoutItem->setData(layoutName.at(1));
        layoutItem->setCheckable(true);
        layoutGroup->addAction(layoutItem);

        // Only enable layout changes when emulator is not running
        menuEnable << layoutItem;

        if (layoutValue == layoutName.at(1)) {
            layoutItem->setChecked(true);
        }
    }

    viewMenu->addSeparator();

#if QT_VERSION >= 0x050000
    // OSX El Capitan adds it's own full-screen option
    if (QSysInfo::macVersion() < QSysInfo::MV_ELCAPITAN
            || QSysInfo::macVersion() == QSysInfo::MV_None) {
        fullScreenAction = viewMenu->addAction(tr("&Full-screen"));
    } else {
        fullScreenAction = new QAction(this);
    }
#else
    fullScreenAction = viewMenu->addAction(tr("&Full-screen"));
#endif
    fullScreenAction->setCheckable(true);

    if (SETTINGS.value("View/fullscreen", "") == "true") {
        fullScreenAction->setChecked(true);
    }

    menuBar->addMenu(viewMenu);

    connect(layoutGroup, SIGNAL(triggered(QAction*)), this, SLOT(updateLayoutSetting()));
    connect(fullScreenAction, SIGNAL(triggered()), this, SLOT(updateFullScreenMode()));
    connect(logAction, SIGNAL(triggered()), this, SLOT(openLog()));


    // Help
    helpMenu = new QMenu(tr("&Help"), this);
    aboutAction = helpMenu->addAction(tr("&About the GUI"));
    aboutAction->setIcon(QIcon::fromTheme("help-about"));
    menuBar->addMenu(helpMenu);

    connect(aboutAction, SIGNAL(triggered()), this, SLOT(openAboutGui()));


    // List of actions that are enabled only when emulator is not running
    menuEnable << startAction
               << openAction
               << refreshAction
               << downloadAction
               << pluginsAction
               << deleteAction
               << configureAction
               << configureGameAction
               << editorAction;

    // List of actions that are disabled when emulator is not running
    menuDisable << stopAction
                << resumeAction
                << pauseAction
                << frameAction
                << resetAction
                << saveStateAction
                << loadStateAction;

    // List of actions that are only active when a ROM is selected
    menuRomSelected << startAction
                    << deleteAction
                    << downloadAction
                    << configureGameAction;
}


void MainWindow::createRomView()
{
    // Create empty view
    emptyView = new QScrollArea(this);
    emptyView->setStyleSheet("QScrollArea { border: none; }");
    emptyView->setBackgroundRole(QPalette::Base);
    emptyView->setAutoFillBackground(true);
    emptyView->setHidden(true);

    emptyLayout = new QGridLayout(emptyView);

    emptyIcon = new QLabel(emptyView);
    emptyIcon->setPixmap(QPixmap(":/images/"+AppNameLower+".png"));

    emptyLayout->addWidget(emptyIcon, 1, 1);
    emptyLayout->setColumnStretch(0, 1);
    emptyLayout->setColumnStretch(2, 1);
    emptyLayout->setRowStretch(0, 1);
    emptyLayout->setRowStretch(2, 1);

    emptyView->setLayout(emptyLayout);


    // Create table view
    tableView = new TableView(this);
    connect(tableView, SIGNAL(clicked(QModelIndex)), this, SLOT(enableButtons()));
    connect(tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(launchRomFromTable()));
    connect(tableView, SIGNAL(tableActive()), this, SLOT(enableButtons()));
    connect(tableView, SIGNAL(enterPressed()), this, SLOT(launchRomFromTable()));


    // Create grid view
    gridView = new GridView(this);
    connect(gridView, SIGNAL(gridItemSelected(bool)), this, SLOT(toggleMenus(bool)));


    // Create list view
    listView = new ListView(this);
    connect(listView, SIGNAL(listItemSelected(bool)), this, SLOT(toggleMenus(bool)));


    // Create disabled view
    disabledView = new QWidget(this);
    disabledView->setHidden(true);
    disabledView->setDisabled(true);

    disabledLayout = new QVBoxLayout(disabledView);
    disabledLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    disabledView->setLayout(disabledLayout);

    QString disabledText = QString("Add a directory containing ROMs under ")
                         + "Settings->Configure->Paths.";
    disabledLabel = new QLabel(disabledText, disabledView);
    disabledLabel->setWordWrap(true);
    disabledLabel->setAlignment(Qt::AlignCenter);
    disabledLayout->addWidget(disabledLabel);


    showActiveView();
}


void MainWindow::disableButtons()
{
    toggleMenus(false);
}


void MainWindow::disableViews(bool imageUpdated)
{
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    // Save position in current layout
    if (visibleLayout == "table") {
        tableView->saveTablePosition();
    } else if (visibleLayout == "grid") {
        gridView->saveGridPosition();
    } else if (visibleLayout == "list") {
        listView->saveListPosition();
    }

    resetLayouts(imageUpdated);
    tableView->clear();

    tableView->setEnabled(false);
    gridView->setEnabled(false);
    listView->setEnabled(false);

    foreach (QAction *next, menuRomSelected) {
        next->setEnabled(false);
    }
}


void MainWindow::enableButtons()
{
    toggleMenus(true);
}


void MainWindow::enableViews(int romCount, bool cached)
{
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    // Else no ROMs, so leave views disabled
    if (romCount != 0) {
        QStringList tableVisible = SETTINGS.value("Table/columns", "Filename|Size").toString().split("|");

        if (tableVisible.join("") != "") {
            tableView->setEnabled(true);
        }

        gridView->setEnabled(true);
        listView->setEnabled(true);

        if (visibleLayout == "table") {
            tableView->setFocus();
        } else if (visibleLayout == "grid") {
            gridView->setFocus();
        } else if (visibleLayout == "list") {
            listView->setFocus();
        }

        // Check if disabled view is showing. If it is, re-enabled the selected view
        if (!disabledView->isHidden()) {
            disabledView->setHidden(true);
            showActiveView();
        }

        if (cached) {
            QTimer *timer = new QTimer(this);
            timer->setSingleShot(true);
            timer->setInterval(0);
            timer->start();

            if (visibleLayout == "table") {
                connect(timer, SIGNAL(timeout()), tableView, SLOT(setTablePosition()));
            } else if (visibleLayout == "grid") {
                connect(timer, SIGNAL(timeout()), gridView, SLOT(setGridPosition()));
            } else if (visibleLayout == "list") {
                connect(timer, SIGNAL(timeout()), listView, SLOT(setListPosition()));
            }
        }
    } else {
        if (visibleLayout != "none") {
            tableView->setHidden(true);
            gridView->setHidden(true);
            listView->setHidden(true);
            disabledView->setHidden(false);
        }
    }
}


bool MainWindow::eventFilter(QObject*, QEvent *event)
{
    // Show menu bar if mouse is at top of screen in full-screen mode
    if (event->type() == QEvent::HoverMove && isFullScreen()) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

        // x and y axis are reversed in Qt4
#if QT_VERSION >= 0x050000
        int mousePos = mouseEvent->pos().y();
#else
        int mousePos = mouseEvent->pos().x();
#endif

        if (mousePos < 5) {
            showMenuBar(true);
        }
        if (mousePos > 30) {
            showMenuBar(false);
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        // Exit fullscreen mode if Esc key is pressed
        if (keyEvent->key() == Qt::Key_Escape && isFullScreen()) {
            updateFullScreenMode();
        }
    }

#if QT_VERSION >= 0x050000
    // OSX El Capitan adds it's own full-screen option, so handle the event change here
    if (QSysInfo::macVersion() >= QSysInfo::MV_ELCAPITAN && QSysInfo::macVersion() != QSysInfo::MV_None) {
        if (event->type() == QEvent::WindowStateChange) {
            QWindowStateChangeEvent *windowEvent = static_cast<QWindowStateChangeEvent*>(event);

            if (windowEvent->oldState() == Qt::WindowNoState) {
                SETTINGS.setValue("View/fullscreen", true);
                tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            } else {
                SETTINGS.setValue("View/fullscreen", "");
                tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            }
        }
    }
#endif

    return false;
}


QString MainWindow::getCurrentRomInfoFromView(QString infoName)
{
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    if (visibleLayout == "table") {
        return tableView->getCurrentRomInfo(infoName);
    } else if (visibleLayout == "grid" && gridView->hasSelectedRom()) {
        return gridView->getCurrentRomInfo(infoName);
    } else if (visibleLayout == "list" && listView->hasSelectedRom()) {
        return listView->getCurrentRomInfo(infoName);
    }

    return "";
}


void MainWindow::launchRomFromMenu()
{
    QString visibleLayout = layoutGroup->checkedAction()->data().toString();

    if (visibleLayout == "table") {
        launchRomFromTable();
    } else if (visibleLayout == "grid") {
        launchRomFromWidget(gridView->getCurrentRomWidget());
    } else if (visibleLayout == "list") {
        launchRomFromWidget(listView->getCurrentRomWidget());
    }
}


void MainWindow::launchRomFromTable()
{
    QString romFileName = tableView->getCurrentRomInfo("fileName");
    QString romDirName = tableView->getCurrentRomInfo("dirName");
    QString zipFileName = tableView->getCurrentRomInfo("zipFile");
    if (zipFileName == "") {
        QString path = QDir(romDirName).absoluteFilePath(romFileName);
        emulation.startGame(path, zipFileName);
    } else {
        QString zipPath = QDir(romDirName).absoluteFilePath(zipFileName);
        emulation.startGame(romFileName, zipPath);
    }
}


void MainWindow::launchRomFromWidget(QWidget *current)
{
    QString romFileName = current->property("fileName").toString();
    QString romDirName = current->property("directory").toString();
    QString zipFileName = current->property("zipFile").toString();
    if (zipFileName == "") {
        QString path = QDir(romDirName).absoluteFilePath(romFileName);
        emulation.startGame(path, zipFileName);
    } else {
        QString zipPath = QDir(romDirName).absoluteFilePath(zipFileName);
        emulation.startGame(romFileName, zipPath);
    }
}


void MainWindow::launchRomFromZip()
{
    QString fileName = zipList->currentItem()->text();
    zipDialog->close();

    emulation.startGame(fileName, openPath);
}


void MainWindow::openAboutGui()
{
    AboutGuiDialog aboutGuiDialog(this);
    aboutGuiDialog.exec();
}


void MainWindow::openDeleteDialog()
{
    scraper = new TheGamesDBScraper(this);
    scraper->deleteGameInfo(getCurrentRomInfoFromView("fileName"), getCurrentRomInfoFromView("romMD5"));
    delete scraper;

    romCollection->cachedRoms();
}


void MainWindow::openDownloader()
{
    DownloadDialog downloadDialog(getCurrentRomInfoFromView("fileName"),
                                  getCurrentRomInfoFromView("search"),
                                  getCurrentRomInfoFromView("romMD5"),
                                  this);
    downloadDialog.exec();

    romCollection->cachedRoms();
}


void MainWindow::openPlugins()
{
    openSettings(3);
}


void MainWindow::openInputConfig()
{
    if (getCurrentInputPlugin() != "") {
        InputDialog().exec();
    } else {
        SHOW_I(tr("Go to settings and select an input plugin first"));
    }
}


void MainWindow::openEditor()
{
    QString configPath = SETTINGS.value("Paths/config", "").toString();
    QDir configDir = QDir(configPath);
    QString configFile = configDir.absoluteFilePath("mupen64plus.cfg");
    QFile config(configFile);

    if (configPath == "" || !config.exists()) {
        QMessageBox::information(this, tr("Not Found"), QString(tr("Editor requires config directory to be "))
                                 + tr("set to a directory with mupen64plus.cfg.") + "<br /><br />"
                                 + tr("See here for the default config location:") + "<br />"
                                 + "<a href=\"http://mupen64plus.org/wiki/index.php?title=FileLocations\">"
                                 + "http://mupen64plus.org/wiki/index.php?title=FileLocations</a>");
    } else {
        ConfigEditor configEditor(configFile, this);
        configEditor.exec();
    }
}


void MainWindow::openGameSettings()
{
    GameSettingsDialog gameSettingsDialog(getCurrentRomInfoFromView("fileName"), this);
    gameSettingsDialog.exec();
}


void MainWindow::openLog()
{
    LogDialog logDialog(this);
    logDialog.exec();
}


void MainWindow::openSettings()
{
    openSettings(0);
}


void MainWindow::openSettings(int tab)
{
    QString tableImageBefore = SETTINGS.value("Table/imagesize", "Medium").toString();
    QString columnsBefore = SETTINGS.value("Table/columns", "Filename|Size").toString();
    QString downloadBefore = SETTINGS.value("Other/downloadinfo", "").toString();

    SettingsDialog settingsDialog(this, tab);
    settingsDialog.exec();

    QString tableImageAfter = SETTINGS.value("Table/imagesize", "Medium").toString();
    QString columnsAfter = SETTINGS.value("Table/columns", "Filename|Size").toString();
    QString downloadAfter = SETTINGS.value("Other/downloadinfo", "").toString();

    // Reset columns widths if user has selected different columns to display
    if (columnsBefore != columnsAfter) {
        SETTINGS.setValue("Table/width", "");
        tableView->setColumnCount(3);
        tableView->setHeaderLabels(QStringList(""));
    }

    QStringList romSave = SETTINGS.value("Paths/roms","").toString().split("|");
    if (romCollection->romPaths != romSave) {
        romCollection->updatePaths(romSave);
        romCollection->addRoms();
    } else if (downloadBefore == "" && downloadAfter == "true") {
        romCollection->addRoms();
    } else {
        if (tableImageBefore != tableImageAfter) {
            romCollection->cachedRoms(true);
        } else {
            romCollection->cachedRoms(false);
        }
    }

    gridView->setGridBackground();
    listView->setListBackground();
    toggleMenus(true);
}


void MainWindow::openRom()
{
    QString filter = "N64 ROMs (";
    foreach (QString type, romCollection->getFileTypes(true)) {
        filter += type + " ";
    }
    filter += ");;" + tr("All Files") + " (*)";

#if QT_VERSION >= 0x050000
    QString searchPath = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).first();
#else
    QString searchPath = QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
#endif
    if (romCollection->romPaths.count() > 0) {
        searchPath = romCollection->romPaths.at(0);
    }

    openPath = QFileDialog::getOpenFileName(this, tr("Open ROM File"), searchPath, filter);
    if (openPath != "") {
        if (QFileInfo(openPath).suffix() == "zip") {
            QStringList zippedFiles = getZippedFiles(openPath);

            QString last;
            int count = 0;

            foreach (QString file, zippedFiles) {
                QString ext = file.right(4).toLower();

                if (romCollection->getFileTypes().contains("*" + ext)) {
                    last = file;
                    count++;
                }
            }

            if (count == 0) {
                QMessageBox::information(this, tr("No ROMs"), tr("No ROMs found in ZIP file."));
            } else if (count == 1) {
                emulation.startGame(last, openPath);
            } else {
                // More than one ROM in zip file, so let user select
                openZipDialog(zippedFiles);
            }
        } else {
            emulation.startGame(openPath);
        }
    }
}


void MainWindow::openZipDialog(QStringList zippedFiles)
{
    zipDialog = new QDialog(this);
    zipDialog->setWindowTitle(tr("Select ROM"));
    zipDialog->setMinimumSize(200, 150);
    zipDialog->resize(300, 150);

    zipLayout = new QGridLayout(zipDialog);
    zipLayout->setContentsMargins(5, 10, 5, 10);

    zipList = new QListWidget(zipDialog);
    foreach (QString file, zippedFiles) {
        QString ext = file.right(4);

        if (romCollection->getFileTypes().contains("*" + ext)) {
            zipList->addItem(file);
        }
    }
    zipList->setCurrentRow(0);

    zipButtonBox = new QDialogButtonBox(Qt::Horizontal, zipDialog);
    zipButtonBox->addButton(tr("Launch"), QDialogButtonBox::AcceptRole);
    zipButtonBox->addButton(QDialogButtonBox::Cancel);

    zipLayout->addWidget(zipList, 0, 0);
    zipLayout->addWidget(zipButtonBox, 1, 0);

    connect(zipList, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(launchRomFromZip()));
    connect(zipButtonBox, SIGNAL(accepted()), this, SLOT(launchRomFromZip()));
    connect(zipButtonBox, SIGNAL(rejected()), zipDialog, SLOT(close()));

    zipDialog->setLayout(zipLayout);

    zipDialog->exec();
}


void MainWindow::resetLayouts(bool imageUpdated)
{
    tableView->resetView(imageUpdated);
    gridView->resetView();
    listView->resetView();
}


void MainWindow::showActiveView()
{
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    if (visibleLayout == "table") {
        tableView->setHidden(false);
    } else if (visibleLayout == "grid") {
        gridView->setHidden(false);
    } else if (visibleLayout == "list") {
        listView->setHidden(false);
    } else {
        emptyView->setHidden(false);
    }
}


void MainWindow::showMenuBar(bool mouseAtTop)
{
    menuBar->setHidden(!mouseAtTop);
}



void MainWindow::showRomMenu(const QPoint &pos)
{
    QMenu *contextMenu = new QMenu(this);

    QAction *contextStartAction = contextMenu->addAction(tr("&Start"));
    contextStartAction->setIcon(QIcon::fromTheme("media-playback-start"));
    contextMenu->addSeparator();
    QAction *contextConfigureGameAction = contextMenu->addAction(tr("Configure &Game..."));

    connect(contextStartAction, SIGNAL(triggered()), this, SLOT(launchRomFromMenu()));
    connect(contextConfigureGameAction, SIGNAL(triggered()), this, SLOT(openGameSettings()));

    if (SETTINGS.value("Other/downloadinfo", "").toString() == "true") {
        contextMenu->addSeparator();
        QAction *contextDownloadAction = contextMenu->addAction(tr("&Download/Update Info..."));
        QAction *contextDeleteAction = contextMenu->addAction(tr("D&elete Current Info..."));

        connect(contextDownloadAction, SIGNAL(triggered()), this, SLOT(openDownloader()));
        connect(contextDeleteAction, SIGNAL(triggered()), this, SLOT(openDeleteDialog()));
    }


    QWidget *activeWidget = new QWidget(this);
    QString visibleLayout = SETTINGS.value("View/layout", "table").toString();

    if (visibleLayout == "table") {
        activeWidget = tableView->viewport();
    } else if (visibleLayout == "grid") {
        activeWidget = gridView->getCurrentRomWidget();
    } else if (visibleLayout == "list") {
        activeWidget = listView->getCurrentRomWidget();
    }

    contextMenu->exec(activeWidget->mapToGlobal(pos));
}


void MainWindow::stopEmulator()
{
    emulation.stopGame();
}


void MainWindow::showCheats()
{
    CheatDialog().exec();
}


void MainWindow::toggleMenus(bool active)
{
    foreach (QAction *next, menuEnable) {
        next->setEnabled(active);
    }

    foreach (QAction *next, menuDisable) {
        next->setEnabled(!active);
    }

    tableView->setEnabled(active);
    gridView->setEnabled(active);
    listView->setEnabled(active);

    if (!tableView->hasSelectedRom() &&
        !gridView->hasSelectedRom() &&
        !listView->hasSelectedRom()
    ) {
        foreach (QAction *next, menuRomSelected) {
            next->setEnabled(false);
        }
    }

    if (SETTINGS.value("Other/downloadinfo", "").toString() == "") {
        downloadAction->setEnabled(false);
        deleteAction->setEnabled(false);
    }
}


void MainWindow::updateFullScreenMode()
{
    if (isFullScreen()) {
        fullScreenAction->setChecked(false);
        SETTINGS.setValue("View/fullscreen", "");

        menuBar->setHidden(false);
        tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        showNormal();
    } else {
        fullScreenAction->setChecked(true);
        SETTINGS.setValue("View/fullscreen", true);

        menuBar->setHidden(true);
        tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        showFullScreen();
    }
}


void MainWindow::updateLayoutSetting()
{
    QString visibleLayout = layoutGroup->checkedAction()->data().toString();
    SETTINGS.setValue("View/layout", visibleLayout);

    emptyView->setHidden(true);
    tableView->setHidden(true);
    gridView->setHidden(true);
    listView->setHidden(true);
    disabledView->setHidden(true);

    int romCount = romCollection->cachedRoms();

    if (romCount > 0 || visibleLayout == "none") {
        showActiveView();
    }

    // View was updated so no ROM will be selected. Update menu items accordingly
    foreach (QAction *next, menuRomSelected) {
        next->setEnabled(false);
    }
}


void MainWindow::emulationResumed()
{
    resumeAction->setVisible(false);
    pauseAction->setVisible(true);
}


void MainWindow::emulationPaused()
{
    resumeAction->setVisible(true);
    pauseAction->setVisible(false);
}

void MainWindow::toggleFullscreen()
{
    showFullScreen();
}
