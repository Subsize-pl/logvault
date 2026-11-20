#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

/// A background task in the status bar (DESIGN.md section 2.2): what it does, its progress and a Cancel
/// button. Hidden while the task is not running.
class TaskStatus : public QWidget {
    Q_OBJECT

public:
    explicit TaskStatus(QWidget* parent = nullptr);

    /// Shows the task; the progress bar and the Cancel button only when it has them.
    void start(const QString& text, bool showsProgress, bool cancellable);
    /// Ignored after Cancel was clicked, so "Cancelling..." stays until the task ends.
    void setText(const QString& text);
    void setProgress(int percent);
    void finish();

signals:
    /// Cancel was clicked. The button is disabled then: the task stops when it can.
    void cancelRequested();

private:
    QLabel* m_label;
    QProgressBar* m_progress;
    QPushButton* m_cancel;
    bool m_cancelling = false;
};
