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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include <components/files/wildmidicfg.hpp>

extern "C" {
#include <wildmidi_lib.h>
}

namespace {
// Latest release of Mindwerks/opl3-soundfont (as of 2026). Bump when they
// tag a new version.
constexpr auto OPL3_URL =
    "https://github.com/Mindwerks/opl3-soundfont/releases/download/1.0/"
    "OPL-3_FM_128M.sf2";
}

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

    // Music group — OPL-3 setup / browse for existing cfg / status.
    m_musicStatus = new QLabel(this);
    m_musicStatus->setWordWrap(true);
    m_musicSetup  = new QPushButton(
        tr("Set up authentic OPL-3 music (recommended)"), this);
    m_musicSetup->setToolTip(tr(
        "Downloads the OPL-3 FM synthesis soundfont (~135 MB) from Mindwerks. "
        "OPL-3 is the SoundBlaster 16 / AdLib chipset EOB3 was scored for — "
        "this is the authentic 90s sound. WildMIDI renders the .sf2 directly."));
    m_musicBrowse = new QPushButton(tr("Browse for existing soundfont or config…"), this);
    m_musicBrowse->setToolTip(tr(
        "Point at your own .sf2 soundfont or an existing wildmidi.cfg "
        "(e.g. freepats, timidity). Only if you already have one — "
        "otherwise use the OPL-3 button above."));
    connect(m_musicSetup,  &QPushButton::clicked, this, &MainWindow::setupOpl3Music);
    connect(m_musicBrowse, &QPushButton::clicked, this, &MainWindow::browseMusicCfg);
    auto* musicBox = new QGroupBox(tr("Music"), this);
    auto* musicLayout = new QVBoxLayout(musicBox);
    musicLayout->addWidget(m_musicStatus);
    musicLayout->addWidget(m_musicSetup);
    musicLayout->addWidget(m_musicBrowse);

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
    main->addWidget(musicBox);
    main->addStretch(1);
    main->addLayout(buttons);

    loadSettings();

    // Persist any change
    connect(m_pathEdit, &QLineEdit::textChanged, this, &MainWindow::validatePath);
    // Persist on focus-leave/Enter, not per keystroke — typing a long path by
    // hand shouldn't hit QSettings I/O once per character.
    connect(m_pathEdit, &QLineEdit::editingFinished, this, [this] { saveSettings(); });
    connect(m_scale, &QComboBox::currentIndexChanged, this, [this](int) { saveSettings(); });
    for (auto* cb : {m_skipIntro, m_skipMenu, m_nosound, m_debug})
        connect(cb, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });

    validatePath();
    validateMusic();
    resize(520, 480);
}

void MainWindow::browsePath() {
    const QString dir = QFileDialog::getExistingDirectory(this,
        tr("Locate your Eye of the Beholder III folder"), m_pathEdit->text());
    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
        saveSettings(); // editingFinished doesn't fire on programmatic setText
    }
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
    // Pass the *resolved* cfg (same search the status row shows), not the raw
    // stored setting — a stale m_musicCfg would override the engine's own
    // fallback search and silently kill music.
    const QString musicCfg = findMusicCfg();
    if (!musicCfg.isEmpty())
        args << QString("--wildmidi-cfg=%1").arg(musicCfg);

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
    m_musicCfg = s.value("musicCfg").toString();
}

void MainWindow::saveSettings() const {
    QSettings s;
    s.setValue("gamePath", m_pathEdit->text());
    s.setValue("scale", m_scale->currentData().toInt());
    s.setValue("skipIntro", m_skipIntro->isChecked());
    s.setValue("skipMenu", m_skipMenu->isChecked());
    s.setValue("nosound", m_nosound->isChecked());
    s.setValue("debug", m_debug->isChecked());
    s.setValue("musicCfg", m_musicCfg);
}

// -----------------------------------------------------------------------------
// Music setup
// -----------------------------------------------------------------------------

QString MainWindow::findMusicCfg() const {
    // Same search the engine runs at Play-time (components/files/wildmidicfg).
    // Pre-check our stored override so a stale path falls through to the
    // shared search instead of being trusted blindly.
    const QString override_ =
        (!m_musicCfg.isEmpty() && QFileInfo::exists(m_musicCfg))
            ? m_musicCfg : QString();
    return QString::fromStdString(
        Files::findWildmidiCfg(override_.toStdString()));
}

void MainWindow::validateMusic() {
    // Short one-line status + full path/reason in the tooltip. Rich-text word
    // wrap inside a QGroupBox is unreliable — keeping the label single-line
    // avoids the mid-path truncation we hit last time.
    const QString cfg = findMusicCfg();
    if (cfg.isEmpty()) {
        m_musicStatus->setText(
            "<span style='color:#a80'>✗ Music not set up — game will run silent.</span>");
        m_musicStatus->setToolTip(tr(
            "No wildmidi.cfg found in any of the search locations. Click "
            "\"Set up authentic OPL-3 music\" to fix, or Browse for an "
            "existing config."));
        return;
    }
    if (WildMidi_Init(cfg.toUtf8().constData(), 32072, 0) == -1) {
        m_musicStatus->setText(
            "<span style='color:#a00'>✗ Music config invalid — WildMIDI rejected it.</span>");
        m_musicStatus->setToolTip(tr(
            "Found: %1\n\nWildMIDI could not load this config (usually a "
            "missing .pat file). Set up OPL-3 to replace, or Browse for a "
            "different config.").arg(cfg));
        return;
    }
    WildMidi_Shutdown();
    m_musicStatus->setText(
        "<span style='color:#080'>✓ Music configured.</span>");
    m_musicStatus->setToolTip(cfg);
}

void MainWindow::browseMusicCfg() {
    const QString f = QFileDialog::getOpenFileName(this,
        tr("Locate soundfont or WildMIDI config"),
        m_musicCfg.isEmpty() ? QDir::homePath() : m_musicCfg,
        tr("Soundfont / config (*.sf2 *.cfg);;All files (*)"));
    if (f.isEmpty()) return;
    m_musicCfg = f;
    saveSettings();
    validateMusic();
}

void MainWindow::setupOpl3Music() {
    if (QMessageBox::question(this, tr("Set up OPL-3 music"),
            tr("This will download the OPL-3 FM soundfont (~135 MB) from "
               "Mindwerks. Continue?")) != QMessageBox::Yes) return;

    // Shared app-data dir (components/files/wildmidicfg) — the same place the
    // engine's search looks. Paths with spaces (macOS "Application Support")
    // are safe thanks to our WildMIDI quoted-paths patch.
    const QString appdata = QString::fromStdString(Files::thirdeyeAppDataDir());
    if (appdata.isEmpty()) {
        QMessageBox::critical(this, tr("No home directory"),
            tr("Could not resolve your user data directory."));
        return;
    }
    QDir().mkpath(appdata);
    const QString sf2Path = QString::fromStdString(Files::appDataOpl3Sf2());

    auto* nam   = new QNetworkAccessManager(this);
    auto* prog  = new QProgressDialog(tr("Downloading OPL-3 soundfont…"),
                                      tr("Cancel"), 0, 100, this);
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->setAutoClose(false);

    QNetworkRequest req{QUrl(OPL3_URL)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, prog,
            [prog](qint64 rec, qint64 tot) {
        if (tot <= 0) return;
        prog->setValue(int(rec * 100 / tot));
        prog->setLabelText(QObject::tr("Downloading OPL-3 soundfont…\n"
                                       "%1 / %2 MB")
                           .arg(rec / (1024*1024)).arg(tot / (1024*1024)));
    });
    connect(prog, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, nam, prog, sf2Path]() {
        prog->close(); prog->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::critical(this, tr("Download failed"),
                                      reply->errorString());
            reply->deleteLater();
            return;
        }
        // Write to a .part file then rename atomically, so an interrupted
        // save can't leave a half-written .sf2 that WildMidi_Init would
        // silently reject on the next launch.
        const QString partPath = sf2Path + ".part";
        QFile out(partPath);
        if (!out.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, tr("Write failed"),
                                  tr("Could not write:\n%1").arg(partPath));
            reply->deleteLater();
            return;
        }
        out.write(reply->readAll());
        out.close();
        reply->deleteLater();
        QFile::remove(sf2Path);            // no-op if absent
        QFile::rename(partPath, sf2Path);

        // WildMidi_Init opens the .sf2 directly (0.5.0+ TinySoundFont path).
        m_musicCfg = sf2Path;
        saveSettings();
        validateMusic();
    });
}
