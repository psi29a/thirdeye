#include "MainWindow.hpp"
#include "config.hpp"

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
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QVersionNumber>

namespace {
// Fixed window width; the hero banner is scaled to exactly this so it sits
// flush against both sides.
constexpr int WINDOW_W = 520;

// GitHub "latest release" endpoint for the update check. Tags are named
// thirdeye-X.Y.Z; stripVersion() below reduces either form to X.Y.Z.
constexpr auto RELEASES_API_URL =
    "https://api.github.com/repos/psi29a/thirdeye/releases/latest";

QVersionNumber stripVersion(QString v) {
    // "thirdeye-0.87.0" / "v0.87.0" / "0.87.0" -> QVersionNumber(0,87,0)
    static const QRegularExpression prefix(
        QStringLiteral("^[A-Za-z-]*"));
    v.remove(prefix);
    return QVersionNumber::fromString(v);
}
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

    // Bottom buttons
    auto* whereBtn = new QPushButton(tr("Where do I get the game?"), this);
    connect(whereBtn, &QPushButton::clicked, this, &MainWindow::showGetGameDialog);
    m_playBtn = new QPushButton(tr("Play ▶"), this);
    m_playBtn->setDefault(true);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::play);

    // Version label: the tag on release builds; version-dev (commit) on
    // dev/CI builds. Baked at configure time (config.hpp).
    QString version = QStringLiteral(LAUNCHER_GIT_TAG);
    if (version.isEmpty()) {
        version = QStringLiteral(LAUNCHER_VERSION "-dev");
        if (*LAUNCHER_GIT_COMMIT)
            version += QStringLiteral(" (" LAUNCHER_GIT_COMMIT ")");
    }
    auto* versionLabel = new QLabel(version, this);
    versionLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    m_upgradeBtn = new QPushButton(tr("Upgrade"), this);
    m_upgradeBtn->setEnabled(false);
    m_upgradeBtn->setToolTip(tr("Checking for updates…"));
    connect(m_upgradeBtn, &QPushButton::clicked, this, [this] {
        if (!m_latestReleaseUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_latestReleaseUrl));
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(whereBtn);
    buttons->addStretch(1);
    buttons->addWidget(versionLabel);
    buttons->addSpacing(24);
    buttons->addWidget(m_upgradeBtn);
    buttons->addSpacing(8);
    buttons->addWidget(m_playBtn);

    // Hero banner: the eye + gold "Thirdeye" plaque (baked from
    // resources/images/hero.svg at 2x; DPR 2 keeps it crisp on retina).
    // Flush against the window's top and sides: the root layout has zero
    // margins, and the width is locked below so the banner always spans
    // edge to edge.
    auto* hero = new QLabel(this);
    QPixmap heroPm(QStringLiteral(":/hero.png")); // 2000x340 physical
    heroPm = heroPm.scaledToWidth(2 * WINDOW_W, Qt::SmoothTransformation);
    heroPm.setDevicePixelRatio(2.0);
    hero->setPixmap(heroPm);

    // Root layout: margin-free so the banner touches the window edges; the
    // actual controls live in an inner layout that restores the margins.
    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(hero);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(11, 8, 11, 11);
    main->addLayout(content);
    content->addWidget(new QLabel(tr("Game folder:"), this));
    content->addLayout(pathRow);
    content->addWidget(m_pathStatus);
    content->addWidget(videoBox);
    content->addWidget(bootBox);
    content->addStretch(1);
    content->addLayout(buttons);

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
    checkForUpdate();
    // Lock the width so the flush hero banner always spans edge to edge;
    // height stays user-resizable (the stretch above the buttons absorbs it).
    setFixedWidth(WINDOW_W);
    resize(WINDOW_W, 560);
}

void MainWindow::checkForUpdate() {
    // Fire-and-forget probe of GitHub's latest-release endpoint. Failure of
    // any kind (offline, rate-limited, no releases yet) just leaves the
    // Upgrade button greyed out with an explanatory tooltip — never a dialog.
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QString::fromLatin1(RELEASES_API_URL))};
    // GitHub's API rejects requests without a User-Agent.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("thirdeye-launcher"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // Cap a stalled connect/download so the Upgrade button doesn't sit at
    // "Checking for updates…" forever on a bad network. On timeout the reply
    // aborts with OperationCanceledError, which the existing error branch
    // below already surfaces via the tooltip.
    req.setTransferTimeout(std::chrono::seconds(15));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_upgradeBtn->setToolTip(
                tr("Could not check for updates: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString latestTag = obj.value(QStringLiteral("tag_name")).toString();
        const QString url       = obj.value(QStringLiteral("html_url")).toString();
        const QVersionNumber latest  = stripVersion(latestTag);
        // Compare against the running build: the exact tag on release
        // builds, the baked base version on dev/CI builds.
        const QVersionNumber current = stripVersion(
            *LAUNCHER_GIT_TAG ? QStringLiteral(LAUNCHER_GIT_TAG)
                              : QStringLiteral(LAUNCHER_VERSION));
        if (latest.isNull() || current.isNull()) {
            m_upgradeBtn->setToolTip(
                tr("Could not parse release versions (latest: %1)").arg(latestTag));
            return;
        }
        if (latest > current) {
            m_latestReleaseUrl = url;
            m_upgradeBtn->setEnabled(true);
            m_upgradeBtn->setText(tr("Upgrade to %1").arg(latestTag));
            m_upgradeBtn->setToolTip(
                tr("A newer release is available — opens the download page."));
        } else {
            m_upgradeBtn->setToolTip(tr("You're up to date (latest: %1).")
                                         .arg(latestTag));
        }
    });
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
