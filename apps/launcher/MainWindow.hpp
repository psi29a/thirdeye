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

private:
    void loadSettings();
    void saveSettings() const;
    QString engineBinaryPath() const;

    // Music-config resolution mirrors the engine's search order in
    // apps/thirdeye/sound/sound.cpp so the launcher shows the user the same
    // cfg the engine will use at Play-time.
    QString findMusicCfg() const;
    QString appDataMusicCfg() const;

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
    QPushButton* m_playBtn     {};

    // User-picked or launcher-downloaded cfg path; empty = fall back to
    // engine's search. Persisted as `musicCfg` in QSettings, passed on Play
    // as --wildmidi-cfg=.
    QString      m_musicCfg;
};
