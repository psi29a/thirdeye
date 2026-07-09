#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void browsePath();
    void showGetGameDialog();
    void play();
    void validatePath();

    void browseMusicCfg();
    void setupOpl3Music();  // download SF2 + run unsf; wired next round
    void validateMusic();
    void checkForUpdate();  // async GitHub releases/latest probe

private:
    void loadSettings();
    void saveSettings() const;
    QString engineBinaryPath() const;

    // Resolution delegates to Files::findWildmidiCfg (components/files/
    // wildmidicfg.hpp) — the same code the engine runs at Play-time, so the
    // launcher can never drift from what the engine will actually use.
    QString findMusicCfg() const;

    QLineEdit*   m_pathEdit    {};
    QLabel*      m_pathStatus  {};
    QComboBox*   m_scale       {};
    QCheckBox*   m_skipIntro   {};
    QCheckBox*   m_skipMenu    {};
    QCheckBox*   m_nosound     {};
    QCheckBox*   m_debug       {};
    QLabel*      m_musicStatus {};
    QPushButton* m_musicSetup  {};
    QPushButton* m_musicBrowse {};
    QPushButton* m_upgradeBtn  {};
    QPushButton* m_playBtn     {};

    // Filled by checkForUpdate when a newer GitHub release exists; the
    // Upgrade button opens it in the browser.
    QString      m_latestReleaseUrl;

    // User-picked or launcher-downloaded cfg path; empty = fall back to
    // engine's search. Persisted as `musicCfg` in QSettings, passed on Play
    // as --wildmidi-cfg=.
    QString      m_musicCfg;
};
