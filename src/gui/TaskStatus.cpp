#include "TaskStatus.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace {

constexpr int kProgressWidth = 160;

} // namespace

TaskStatus::TaskStatus(QWidget* parent)
    : QWidget(parent), m_label(new QLabel), m_progress(new QProgressBar),
      m_cancel(new QPushButton(tr("Cancel"))) {
    m_progress->setRange(0, 100);
    m_progress->setMaximumWidth(kProgressWidth);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);
    layout->addWidget(m_progress);
    layout->addWidget(m_cancel);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancelling = true;
        m_cancel->setEnabled(false);
        m_label->setText(tr("Cancelling..."));
        emit cancelRequested();
    });
    hide();
}

void TaskStatus::start(const QString& text, bool showsProgress, bool cancellable) {
    m_cancelling = false;
    m_label->setText(text);
    m_progress->setValue(0);
    m_progress->setVisible(showsProgress);
    m_cancel->setEnabled(true);
    m_cancel->setVisible(cancellable);
    show();
}

void TaskStatus::setText(const QString& text) {
    if (!m_cancelling) {
        m_label->setText(text);
    }
}

void TaskStatus::setProgress(int percent) { m_progress->setValue(percent); }

void TaskStatus::finish() { hide(); }
