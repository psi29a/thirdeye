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

private:
    void loadSettings();
    void saveSettings() const;
    QString engineBinaryPath() const;

    QLineEdit*   m_pathEdit  {};
    QLabel*      m_pathStatus{};
    QComboBox*   m_scale     {};
    QCheckBox*   m_skipIntro {};
    QCheckBox*   m_skipMenu  {};
    QCheckBox*   m_nosound   {};
    QCheckBox*   m_debug     {};
    QPushButton* m_playBtn   {};
};
