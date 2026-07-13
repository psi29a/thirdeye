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
    void installFromInstaller();
    void downloadFromInternetArchive();
    void showGetGameDialog();
    void play();
    void validatePath();
    void checkForUpdate();  // async GitHub releases/latest probe

private:
    void loadSettings();
    void saveSettings() const;
    QString engineBinaryPath() const;
    // Unpacks the game data from a GOG .sh installer (or plain .zip) into
    // destRoot; returns the folder containing EYE.RES, or "" + *err set.
    QString extractGameData(const QString& archivePath, const QString& destRoot,
                            QString* err);

    QLineEdit*   m_pathEdit    {};
    QLabel*      m_pathStatus  {};
    QComboBox*   m_scale       {};
    QCheckBox*   m_skipIntro   {};
    QCheckBox*   m_skipMenu    {};
    QCheckBox*   m_nosound     {};
    QCheckBox*   m_debug       {};
    QPushButton* m_upgradeBtn  {};
    QPushButton* m_playBtn     {};

    // Filled by checkForUpdate when a newer GitHub release exists; the
    // Upgrade button opens it in the browser.
    QString      m_latestReleaseUrl;
};
