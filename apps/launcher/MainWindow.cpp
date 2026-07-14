#include "MainWindow.hpp"
#include "config.hpp"
#include "unarj.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QProgressDialog>
#include <QtEndian>
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
#include <QStandardPaths>
#include <QUrl>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <miniz.h>

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
constexpr auto ARCHIVE_DOWNLOAD_BASE =
    "https://archive.org/download/eye-of-the-beholder-3/";

// Warn before extracting into a data dir that already holds a game install --
// files present in both the archive and the target get overwritten (SAVEGAME's
// _00/_01.BIN saves, SAVEGAME.DIR, the game's own .RES/.EXE/.DLL). Loose files
// the archive doesn't touch (.TMP live state, custom saves) stay. Returns
// true if the user consented (or the dir doesn't exist yet).
bool confirmDestructiveInstall(QWidget* parent, const QString& destRoot) {
    if (!QFileInfo::exists(destRoot + QStringLiteral("/") + GAME_FILE))
        return true;
    const QMessageBox::StandardButton pick = QMessageBox::warning(
        parent, MainWindow::tr("Overwrite existing install?"),
        MainWindow::tr(
            "A game install is already present at:\n%1\n\n"
            "Installing again will overwrite the shipped save slots "
            "(SAVEGAME/_00.BIN / _01.BIN) with the pristine Quick Start Party.\n\n"
            "Continue?").arg(destRoot),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    return pick == QMessageBox::Yes;
}

// Offset of the actual ZIP inside a file that may have data prepended (GOG's
// MojoSetup .sh stub): the delta between where the central directory really
// sits and where the end-of-central-directory record claims it is. Zero for
// plain .zips. miniz doesn't compensate for prepended data on its own.
qsizetype zipStartOffset(const QByteArray& bytes) {
    const auto* b = reinterpret_cast<const uchar*>(bytes.constData());
    for (qsizetype i = bytes.size() - 22; i >= 0; --i) {
        if (b[i] == 'P' && b[i + 1] == 'K' && b[i + 2] == 5 && b[i + 3] == 6) {
            const quint32 cdSize = qFromLittleEndian<quint32>(b + i + 12);
            const quint32 cdOfs  = qFromLittleEndian<quint32>(b + i + 16);
            const qsizetype start = i - qsizetype(cdSize) - qsizetype(cdOfs);
            return start > 0 ? start : 0;
        }
    }
    return 0;
}
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

    auto* installBtn =
        new QPushButton(tr("Extract game data from a GOG installer…"), this);
    connect(installBtn, &QPushButton::clicked,
            this, &MainWindow::installFromInstaller);
    auto* iaBtn = new QPushButton(
        tr("Download the game from the Internet Archive…"), this);
    connect(iaBtn, &QPushButton::clicked,
            this, &MainWindow::downloadFromInternetArchive);

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
    content->addWidget(new QLabel(tr("Game Installation:"), this));
    content->addLayout(pathRow);
    content->addWidget(m_pathStatus);
    content->addWidget(installBtn);
    content->addSpacing(8);
    content->addWidget(iaBtn);
    content->addSpacing(16);
    content->addWidget(videoBox);
    content->addSpacing(16);
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

void MainWindow::installFromInstaller() {
    const QString archive = QFileDialog::getOpenFileName(this,
        tr("Pick the installer you downloaded from GOG"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tr("GOG offline installer or zip (*.sh *.zip);;All files (*)"));
    if (archive.isEmpty())
        return;

    const QString destRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/eob3");
    if (!confirmDestructiveInstall(this, destRoot))
        return;

    // ~50 MB of small files; finishes in well under a second. No progress UI.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const QString gameDir = extractGameData(archive, destRoot, &err);
    QApplication::restoreOverrideCursor();

    if (gameDir.isEmpty()) {
        QMessageBox::critical(this, tr("Could not extract the installer"), err);
        return;
    }
    m_pathEdit->setText(gameDir);
    saveSettings();
    QMessageBox::information(this, tr("Game data installed"),
        tr("Game data extracted to:\n%1\n\nThe launcher now points there — hit Play.")
            .arg(gameDir));
}

QString MainWindow::extractGameData(const QString& archivePath,
                                    const QString& destRoot, QString* err) {
    // ponytail: whole archive slurped into RAM (the GOG installers are
    // 26–110 MB) — sidesteps char*-path encoding portability in miniz's
    // file API; switch to mz_zip_reader_init_cfile if it ever hurts.
    QFile in(archivePath);
    if (!in.open(QIODevice::ReadOnly)) {
        *err = tr("Can't read %1:\n%2").arg(archivePath, in.errorString());
        return {};
    }
    const QByteArray bytes = in.readAll();
    const qsizetype zipStart = zipStartOffset(bytes);

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.constData() + zipStart,
                                size_t(bytes.size() - zipStart), 0)) {
        *err = tr("This file isn't an installer the launcher can open.\n\n"
                  "Download GOG's *Linux* offline installer (the .sh file) — "
                  "the launcher can extract it on every platform — or a plain "
                  ".zip of the game folder.");
        return {};
    }

    // GOG's MojoSetup .sh keeps the game under data/noarch/data/; when that
    // prefix exists extract only it (drops DOSBox + GOG support files).
    const QString gogPrefix = QStringLiteral("data/noarch/data/");
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool isGogSh = false;
    mz_zip_archive_file_stat st;
    for (mz_uint i = 0; i < n && !isGogSh; ++i)
        isGogSh = mz_zip_reader_file_stat(&zip, i, &st)
                  && QString::fromUtf8(st.m_filename).startsWith(gogPrefix);

    int extracted = 0;
    for (mz_uint i = 0; i < n; ++i) {
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
            continue;
        QString name = QString::fromUtf8(st.m_filename);
        if (isGogSh) {
            if (!name.startsWith(gogPrefix))
                continue;
            name.remove(0, gogPrefix.size());
        }
        // Trust boundary: refuse paths that escape destRoot (zip-slip).
        // (SAVEGAME/ is deliberately kept — it's GOG's quick-start party.)
        if (name.startsWith(QLatin1Char('/'))
            || name.contains(QStringLiteral("..")))
            continue;

        size_t sz = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!data) {
            *err = tr("Failed to extract %1 — corrupt download?").arg(name);
            mz_zip_reader_end(&zip);
            return {};
        }
        const QString outPath = destRoot + QLatin1Char('/') + name;
        QDir().mkpath(QFileInfo(outPath).path());
        QFile out(outPath);
        const bool ok = out.open(QIODevice::WriteOnly)
            && out.write(static_cast<const char*>(data), qint64(sz))
                   == qint64(sz);
        mz_free(data);
        if (!ok) {
            *err = tr("Can't write %1:\n%2").arg(outPath, out.errorString());
            mz_zip_reader_end(&zip);
            return {};
        }
        ++extracted;
    }
    mz_zip_reader_end(&zip);

    QDirIterator it(destRoot, {QString::fromLatin1(GAME_FILE)}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext())
        return QFileInfo(it.next()).absolutePath();
    *err = tr("Extracted %1 file(s) to %2, but no %3 was inside — is this the "
              "Eye of the Beholder III installer?")
               .arg(extracted).arg(destRoot, QLatin1String(GAME_FILE));
    return {};
}

void MainWindow::downloadFromInternetArchive() {
    // Consent first — this is an abandonware mirror, not an official source,
    // and no download happens until the user explicitly owns that choice.
    QDialog consent(this);
    consent.setWindowTitle(tr("Download from the Internet Archive"));
    auto* v = new QVBoxLayout(&consent);
    auto* warn = new QLabel(tr(
        "<b>This is not an official source.</b><br><br>"
        "The Internet Archive hosts an abandonware upload of the original "
        "Eye of the Beholder III floppy disks. The upload could change or "
        "disappear at any time, and downloading it does not make you an "
        "owner of the game — you should own a real copy.<br><br>"
        "If you'd rather buy it, GOG sells it (inside \"Forgotten Realms: "
        "The Archives, Collection Two\"):"), &consent);
    warn->setWordWrap(true);
    v->addWidget(warn);
    auto* gogBtn = new QPushButton(tr("Open the GOG page instead"), &consent);
    connect(gogBtn, &QPushButton::clicked,
            [] { QDesktopServices::openUrl(QUrl(GOG_URL)); });
    v->addWidget(gogBtn);
    auto* bb = new QDialogButtonBox(&consent);
    auto* acceptBtn = bb->addButton(
        tr("I accept the responsibility — download"),
        QDialogButtonBox::AcceptRole);
    bb->addButton(QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &consent, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &consent, &QDialog::reject);
    acceptBtn->setDefault(false);
    v->addSpacing(8);
    v->addWidget(bb);
    if (consent.exec() != QDialog::Accepted)
        return;

    const QString destRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/eob3");
    if (!confirmDestructiveInstall(this, destRoot))
        return;

    // Fetch the four disk zips (≈4.5 MB total), sequentially with progress.
    QProgressDialog progress(tr("Downloading disk 1 of 4…"), tr("Cancel"),
                             0, 400, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QNetworkAccessManager nam;
    QHash<QString, QByteArray> arjVolumes;  // "DATA1.ARJ" … "DATA6.ARJ"
    for (int disk = 1; disk <= 4; ++disk) {
        progress.setLabelText(tr("Downloading disk %1 of 4…").arg(disk));
        QNetworkRequest req{QUrl(QStringLiteral("%1EOB3_Disk%2.zip")
                                     .arg(ARCHIVE_DOWNLOAD_BASE).arg(disk))};
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("thirdeye-launcher"));
        req.setTransferTimeout(std::chrono::minutes(5));
        QNetworkReply* reply = nam.get(req);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(reply, &QNetworkReply::downloadProgress, this,
                [&](qint64 got, qint64 total) {
                    if (total > 0)
                        progress.setValue((disk - 1) * 100
                                          + int(got * 100 / total));
                });
        connect(&progress, &QProgressDialog::canceled,
                reply, &QNetworkReply::abort);
        loop.exec();
        reply->deleteLater();
        if (progress.wasCanceled())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(this, tr("Download failed"),
                tr("Could not download disk %1:\n%2\n\nThe Internet Archive "
                   "upload may have moved or vanished — this is why GOG is "
                   "the reliable option.")
                    .arg(disk).arg(reply->errorString()));
            return;
        }
        const QByteArray zipBytes = reply->readAll();

        // Pull the DATA*.ARJ volumes out of the disk zip, in memory.
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_mem(&zip,
                zipBytes.constData() + zipStartOffset(zipBytes),
                size_t(zipBytes.size() - zipStartOffset(zipBytes)), 0)) {
            QMessageBox::critical(this, tr("Download failed"),
                tr("Disk %1 isn't a readable zip — the upload may have "
                   "changed.").arg(disk));
            return;
        }
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
                continue;
            const QString name = QString::fromUtf8(st.m_filename);
            if (!name.startsWith(QStringLiteral("DATA"))
                || !name.endsWith(QStringLiteral(".ARJ")))
                continue;
            size_t sz = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
            if (!data) {
                mz_zip_reader_end(&zip);
                QMessageBox::critical(this, tr("Download failed"),
                    tr("Failed to extract %1 from disk %2.")
                        .arg(name).arg(disk));
                return;
            }
            arjVolumes.insert(name,
                QByteArray(static_cast<const char*>(data), qsizetype(sz)));
            mz_free(data);
        }
        mz_zip_reader_end(&zip);
    }
    progress.setValue(400);

    // Decompress the six-volume ARJ set. Per INSTALL.NFO on disk 1:
    // DATA1-4 → game root, DATA5 → SAVEGAME/, DATA6 → CHARGEN/.
    std::vector<std::vector<uint8_t>> vols;
    for (int i = 1; i <= 6; ++i) {
        const QByteArray a =
            arjVolumes.value(QStringLiteral("DATA%1.ARJ").arg(i));
        if (a.isEmpty()) {
            QMessageBox::critical(this, tr("Extraction failed"),
                tr("DATA%1.ARJ was missing from the downloaded disks — the "
                   "upload may have changed.").arg(i));
            return;
        }
        vols.emplace_back(a.begin(), a.end());
    }
    std::vector<ArjEntry> entries;
    std::string arjErr;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = unarjExtract(vols, entries, arjErr);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Extraction failed"),
            QString::fromStdString(arjErr));
        return;
    }

    for (const ArjEntry& e : entries) {
        const QString name = QString::fromLatin1(e.name);
        // Trust boundary: plain filenames only — no separators, no escapes.
        if (name.isEmpty() || name.contains(QLatin1Char('/'))
            || name.contains(QLatin1Char('\\'))
            || name.contains(QStringLiteral("..")))
            continue;
        const QString sub = e.volume == 4 ? QStringLiteral("SAVEGAME/")
                          : e.volume == 5 ? QStringLiteral("CHARGEN/")
                                          : QString();
        const QString outPath = destRoot + QLatin1Char('/') + sub + name;
        QDir().mkpath(QFileInfo(outPath).path());
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly)
            || out.write(reinterpret_cast<const char*>(e.data.data()),
                         qint64(e.data.size()))
                   != qint64(e.data.size())) {
            QMessageBox::critical(this, tr("Extraction failed"),
                tr("Can't write %1:\n%2").arg(outPath, out.errorString()));
            return;
        }
    }

    m_pathEdit->setText(destRoot);
    saveSettings();
    QMessageBox::information(this, tr("Game data installed"),
        tr("Downloaded and extracted %n file(s) to:\n%1\n\n"
           "The launcher now points there — hit Play.", nullptr,
           int(entries.size())).arg(destRoot));
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
        "From GOG, download the <b>Linux offline installer</b> (the .sh file) — "
        "whatever OS you're on — then use \"Extract game data from a GOG "
        "installer…\" and the launcher does the rest."), &dlg));

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
