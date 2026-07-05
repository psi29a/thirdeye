#include "MainWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr auto GAME_FILE = "EYE.RES";
constexpr auto GOG_URL =
    "https://www.gog.com/game/forgotten_realms_the_archives_collection_two";
constexpr auto ARCHIVE_URL =
    "https://archive.org/details/eye-of-the-beholder-3";
} // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(tr("Thirdeye Launcher"));

    // Path picker
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("Path to your Eye of the Beholder III folder"));
    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browsePath);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browseBtn);
    m_pathStatus = new QLabel(this);

    // Video group
    m_scale = new QComboBox(this);
    for (int s : {1, 2, 3, 4, 5}) m_scale->addItem(QString("%1×").arg(s), s);
    auto* videoBox = new QGroupBox(tr("Video"), this);
    auto* videoForm = new QFormLayout(videoBox);
    videoForm->addRow(tr("Scale:"), m_scale);

    // Boot group
    m_skipIntro = new QCheckBox(tr("Skip intro cinematic"), this);
    m_skipMenu  = new QCheckBox(tr("Skip title menu (load first save or chargen)"), this);
    m_nosound   = new QCheckBox(tr("Disable sound"), this);
    m_debug     = new QCheckBox(tr("VM debug trace (verbose stdout)"), this);
    auto* bootBox = new QGroupBox(tr("Boot"), this);
    auto* bootLayout = new QVBoxLayout(bootBox);
    bootLayout->addWidget(m_skipIntro);
    bootLayout->addWidget(m_skipMenu);
    bootLayout->addWidget(m_nosound);
    bootLayout->addWidget(m_debug);

    // Bottom buttons
    auto* whereBtn = new QPushButton(tr("Where do I get the game?"), this);
    connect(whereBtn, &QPushButton::clicked, this, &MainWindow::showGetGameDialog);
    m_playBtn = new QPushButton(tr("Play ▶"), this);
    m_playBtn->setDefault(true);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::play);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(whereBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_playBtn);

    // Root layout
    auto* main = new QVBoxLayout(this);
    main->addWidget(new QLabel(tr("Game folder:"), this));
    main->addLayout(pathRow);
    main->addWidget(m_pathStatus);
    main->addWidget(videoBox);
    main->addWidget(bootBox);
    main->addStretch(1);
    main->addLayout(buttons);

    loadSettings();

    // Persist any change
    connect(m_pathEdit, &QLineEdit::textChanged, this, &MainWindow::validatePath);
    connect(m_pathEdit, &QLineEdit::textChanged, this, [this] { saveSettings(); });
    connect(m_scale, &QComboBox::currentIndexChanged, this, [this](int) { saveSettings(); });
    for (auto* cb : {m_skipIntro, m_skipMenu, m_nosound, m_debug})
        connect(cb, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });

    validatePath();
    resize(520, 380);
}

void MainWindow::browsePath() {
    const QString dir = QFileDialog::getExistingDirectory(this,
        tr("Locate your Eye of the Beholder III folder"), m_pathEdit->text());
    if (!dir.isEmpty()) m_pathEdit->setText(dir);
}

void MainWindow::showGetGameDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Getting the game"));
    auto* v = new QVBoxLayout(&dlg);

    v->addWidget(new QLabel(tr(
        "Thirdeye ships no game data — you must own Eye of the Beholder III."), &dlg));
    v->addSpacing(6);

    v->addWidget(new QLabel(tr(
        "<b>Buy on GOG</b> — bundled in \"Forgotten Realms: The Archives, Collection 2\":"), &dlg));
    auto* buy = new QPushButton(tr("Open GOG page"), &dlg);
    QObject::connect(buy, &QPushButton::clicked,
                     [] { QDesktopServices::openUrl(QUrl(GOG_URL)); });
    v->addWidget(buy);

    v->addSpacing(8);
    v->addWidget(new QLabel(tr(
        "<b>Internet Archive</b> (abandonware — not an official source, clicking is your call):"), &dlg));
    auto* ia = new QPushButton(tr("Open Internet Archive page"), &dlg);
    QObject::connect(ia, &QPushButton::clicked,
                     [] { QDesktopServices::openUrl(QUrl(ARCHIVE_URL)); });
    v->addWidget(ia);

    v->addSpacing(6);
    v->addWidget(new QLabel(tr(
        "Once extracted, point the launcher at the folder containing EYE.RES."), &dlg));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttonBox);

    dlg.exec();
}

void MainWindow::validatePath() {
    const QString dir = m_pathEdit->text();
    if (dir.isEmpty()) {
        m_pathStatus->setText(tr("<span style='color:#a00'>Pick your game folder.</span>"));
        m_playBtn->setEnabled(false);
        return;
    }
    // ponytail: bare exists check; add strict case check if Linux users report
    // false positives after a case-preserving copy.
    const bool found = QFileInfo::exists(QDir(dir).filePath(GAME_FILE));
    if (found) {
        m_pathStatus->setText(tr("<span style='color:#080'>✓ Found %1</span>").arg(GAME_FILE));
        m_playBtn->setEnabled(true);
    } else {
        m_pathStatus->setText(tr(
            "<span style='color:#a00'>%1 not found in this folder.</span>").arg(GAME_FILE));
        m_playBtn->setEnabled(false);
    }
}

QString MainWindow::engineBinaryPath() const {
    // Engine + launcher install side-by-side on every platform: macOS puts both
    // in thirdeye.app/Contents/MacOS/, Linux/Windows install both to the same
    // bin dir. See top-level CMakeLists.txt.
    QString exe = QCoreApplication::applicationDirPath() + "/thirdeye";
#ifdef Q_OS_WIN
    exe += ".exe";
#endif
    return exe;
}

void MainWindow::play() {
    const QString exe = engineBinaryPath();
    if (!QFileInfo::exists(exe)) {
        QMessageBox::critical(this, tr("Engine not found"),
            tr("Could not find the thirdeye engine binary at:\n%1").arg(exe));
        return;
    }
    QStringList args;
    args << m_pathEdit->text();
    args << QString("--scale=%1").arg(m_scale->currentData().toInt());
    if (m_skipIntro->isChecked()) args << "--skip-intro";
    if (m_skipMenu->isChecked())  args << "--skip-menu";
    if (m_nosound->isChecked())   args << "--nosound";
    if (m_debug->isChecked())     args << "--debug";

    saveSettings();
    if (!QProcess::startDetached(exe, args)) {
        QMessageBox::critical(this, tr("Launch failed"),
            tr("Failed to start:\n%1 %2").arg(exe, args.join(" ")));
        return;
    }
    QApplication::quit();
}

void MainWindow::loadSettings() {
    QSettings s;
    m_pathEdit->setText(s.value("gamePath").toString());
    const int scale = s.value("scale", 3).toInt();
    int idx = m_scale->findData(scale);
    if (idx < 0) idx = m_scale->findData(3);
    m_scale->setCurrentIndex(idx);
    m_skipIntro->setChecked(s.value("skipIntro", true).toBool());
    m_skipMenu->setChecked(s.value("skipMenu", false).toBool());
    m_nosound->setChecked(s.value("nosound", false).toBool());
    m_debug->setChecked(s.value("debug", false).toBool());
}

void MainWindow::saveSettings() const {
    QSettings s;
    s.setValue("gamePath", m_pathEdit->text());
    s.setValue("scale", m_scale->currentData().toInt());
    s.setValue("skipIntro", m_skipIntro->isChecked());
    s.setValue("skipMenu", m_skipMenu->isChecked());
    s.setValue("nosound", m_nosound->isChecked());
    s.setValue("debug", m_debug->isChecked());
}
