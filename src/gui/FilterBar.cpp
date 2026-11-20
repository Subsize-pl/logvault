#include "FilterBar.h"

#include "Format.h"
#include "PeriodDialog.h"
#include "Theme.h"

#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStringListModel>
#include <QToolButton>

#include <tuple>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kMinute = 60'000;
constexpr qint64 kHour = 60 * kMinute;
constexpr qint64 kDay = 24 * kHour;

// In order of severity, with records of unknown level last.
constexpr std::array kButtonOrder = {Level::Trace, Level::Debug, Level::Info,   Level::Warn,
                                     Level::Error, Level::Fatal, Level::Unknown};

// Off: the level's color as text and border, made readable on the window; on: filled with it.
QString levelButtonStyle(Level level) {
    const QColor color = Theme::levelColor(level);
    const QColor text = Theme::levelTextColor(level, QGuiApplication::palette().color(QPalette::Window));
    return QStringLiteral(
               "QToolButton { color: %1; border: 1px solid %1; border-radius: 3px; padding: 2px 6px; "
               "font-weight: bold; } QToolButton:checked { color: %2; background-color: %3; border-color: "
               "%3; }")
        .arg(text.name(), Theme::textColorOn(color).name(), color.name());
}

} // namespace

FilterBar::FilterBar(QWidget* parent) : QWidget(parent) {
    createPeriodBox();
    createSearchLine();
    auto* searchButton = new QPushButton(tr("Search"));
    connect(searchButton, &QPushButton::clicked, this, &FilterBar::submitSearch);

    auto* periodRow = new QHBoxLayout;
    periodRow->addWidget(m_period);
    periodRow->addStretch();
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_searchLine, 1);
    searchRow->addWidget(searchButton);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Levels"), createLevelButtons());
    form->addRow(tr("Period"), periodRow);
    form->addRow(tr("Search"), searchRow);
}

void FilterBar::fillRequest(SearchRequest& request, qint64 now) const {
    request.levels.clear();
    for (int value = 0; value < kLevelCount; ++value) {
        if (m_levelButtons[static_cast<std::size_t>(value)]->isChecked()) {
            request.levels.append(levelFromValue(value));
        }
    }
    std::tie(request.from, request.to) = range(currentPeriod(), now);
    request.text = m_searchLine->text().trimmed();
}

void FilterBar::showOnlyLevel(Level level) {
    for (std::size_t i = 0; i < m_levelButtons.size(); ++i) {
        m_levelButtons[i]->setChecked(i == static_cast<std::size_t>(level));
    }
    emit searchRequested();
}

void FilterBar::hideLevel(Level level) {
    m_levelButtons[static_cast<std::size_t>(level)]->setChecked(false);
    emit searchRequested();
}

void FilterBar::zoomToPeriod(qint64 from, qint64 to) {
    if (from >= to || (currentPeriod() == Period::Custom && from == m_customFrom && to == m_customTo)) {
        return;
    }
    m_zoomHistory.append(periodState());
    if (m_zoomHistory.size() > kZoomHistorySize) {
        m_zoomHistory.removeFirst();
    }
    applyCustomPeriod(from, to);
    emit searchRequested();
}

bool FilterBar::restorePreviousPeriod() {
    if (m_zoomHistory.isEmpty()) {
        return false;
    }
    const PeriodState previous = m_zoomHistory.takeLast();
    m_customFrom = previous.customFrom;
    m_customTo = previous.customTo;
    m_period->setItemText(m_period->findData(static_cast<int>(Period::Custom)), previous.customText);
    m_periodIndex = previous.index;
    m_period->setCurrentIndex(m_periodIndex);
    emit searchRequested();
    return true;
}

void FilterBar::addSearchTerm(const QString& term) {
    const QString text = m_searchLine->text().trimmed();
    m_searchLine->setText(text.isEmpty() ? term : text + u' ' + term);
    submitSearch();
}

void FilterBar::focusSearchLine() {
    m_searchLine->setFocus(Qt::ShortcutFocusReason);
    m_searchLine->selectAll();
}

QWidget* FilterBar::createLevelButtons() {
    auto* buttons = new QWidget;
    auto* row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    for (const Level level : kButtonOrder) {
        auto* button = new QToolButton;
        if (level == Level::Unknown) {
            button->setText(u"?"_s);
            button->setToolTip(tr("Show or hide records of unknown level. Ctrl+click shows only them."));
        } else {
            button->setText(levelName(level));
            button->setToolTip(
                tr("Show or hide %1 records. Ctrl+click shows only them.").arg(levelName(level)));
        }
        button->setCheckable(true);
        button->setChecked(true);
        button->setStyleSheet(levelButtonStyle(level));
        connect(button, &QToolButton::clicked, this, [this, level] { levelClicked(level); });
        m_levelButtons[static_cast<std::size_t>(level)] = button;
        row->addWidget(button);
    }
    row->addStretch();
    return buttons;
}

void FilterBar::createPeriodBox() {
    m_period = new QComboBox;
    m_period->addItem(tr("Last 15 minutes"), static_cast<int>(Period::Last15Minutes));
    m_period->addItem(tr("Last hour"), static_cast<int>(Period::LastHour));
    m_period->addItem(tr("Last 24 hours"), static_cast<int>(Period::Last24Hours));
    m_period->addItem(tr("Last 7 days"), static_cast<int>(Period::Last7Days));
    m_period->addItem(tr("All time"), static_cast<int>(Period::AllTime));
    m_period->addItem(tr("Custom range..."), static_cast<int>(Period::Custom));
    m_period->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_periodIndex = m_period->findData(static_cast<int>(Period::AllTime));
    m_period->setCurrentIndex(m_periodIndex);
    // activated() comes for every choice, even of the current item, so a custom range can be picked again
    // to change it.
    connect(m_period, &QComboBox::activated, this, &FilterBar::periodActivated);
}

void FilterBar::createSearchLine() {
    m_searchLine = new QLineEdit;
    m_searchLine->setPlaceholderText(
        tr("Words, \"phrases\", field:value, field:prefix*, field:*, -excluded"));
    m_searchLine->setClearButtonEnabled(true);
    m_history = new QStringListModel(this);
    auto* completer = new QCompleter(m_history, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_searchLine->setCompleter(completer);

    connect(m_searchLine, &QLineEdit::returnPressed, this, &FilterBar::submitSearch);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, &FilterBar::submitSearch);
    connect(m_searchLine, &QLineEdit::textChanged, this, [this](const QString& text) {
        // Emptying the line (say, with its clear button) shows every record again without pressing Enter.
        if (text.isEmpty() && !m_submittedText.isEmpty()) {
            submitSearch();
        }
    });
}

void FilterBar::levelClicked(Level level) {
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        showOnlyLevel(level);
    } else {
        emit searchRequested();
    }
}

void FilterBar::periodActivated(int index) {
    if (static_cast<Period>(m_period->itemData(index).toInt()) == Period::Custom && !chooseCustomPeriod()) {
        m_period->setCurrentIndex(m_periodIndex);
        return;
    }
    m_periodIndex = index;
    // A period the user picks starts over: Esc never goes back past it.
    m_zoomHistory.clear();
    emit searchRequested();
}

bool FilterBar::chooseCustomPeriod() {
    // The dialog starts from the period in effect; an open end becomes now, an open start an hour earlier.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto [from, to] = range(currentPeriod(), now);
    const qint64 end = to > 0 ? to : now;
    PeriodDialog dialog(from > 0 ? from : end - kHour, end, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    applyCustomPeriod(dialog.from(), dialog.to());
    return true;
}

void FilterBar::applyCustomPeriod(qint64 from, qint64 to) {
    m_customFrom = from;
    m_customTo = to;
    m_periodIndex = m_period->findData(static_cast<int>(Period::Custom));
    m_period->setItemText(m_periodIndex, tr("%1 to %2").arg(Format::dateTime(from), Format::dateTime(to)));
    m_period->setCurrentIndex(m_periodIndex);
}

std::pair<qint64, qint64> FilterBar::range(Period period, qint64 now) const {
    // A relative period ends now (the end is exclusive), so its histogram covers the whole period even when
    // the newest records are older.
    switch (period) {
    case Period::Last15Minutes:
        return {now - 15 * kMinute, now + 1};
    case Period::LastHour:
        return {now - kHour, now + 1};
    case Period::Last24Hours:
        return {now - kDay, now + 1};
    case Period::Last7Days:
        return {now - 7 * kDay, now + 1};
    case Period::AllTime:
        return {0, 0};
    case Period::Custom:
        return {m_customFrom, m_customTo};
    }
    return {0, 0};
}

FilterBar::Period FilterBar::currentPeriod() const {
    return static_cast<Period>(m_period->itemData(m_periodIndex).toInt());
}

FilterBar::PeriodState FilterBar::periodState() const {
    return {m_periodIndex, m_customFrom, m_customTo,
            m_period->itemText(m_period->findData(static_cast<int>(Period::Custom)))};
}

void FilterBar::submitSearch() {
    m_submittedText = m_searchLine->text().trimmed();
    if (!m_submittedText.isEmpty()) {
        remember(m_submittedText);
    }
    emit searchRequested();
}

void FilterBar::remember(const QString& text) {
    QStringList history = m_history->stringList();
    history.removeAll(text);
    history.prepend(text);
    if (history.size() > kHistorySize) {
        history.resize(kHistorySize);
    }
    m_history->setStringList(history);
}
