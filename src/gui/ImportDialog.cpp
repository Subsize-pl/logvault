#include "ImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ImportDialog::ImportDialog(const QStringList& paths, const QStringList& sourceNames, QWidget* parent)
    : QDialog(parent), m_paths(paths) {
    setWindowTitle(tr("Import Files"));

    auto* rows = new QWidget;
    auto* form = new QFormLayout(rows);
    for (const QString& path : paths) {
        const QFileInfo file(path);
        auto* label = new QLabel(file.fileName());
        label->setToolTip(QDir::toNativeSeparators(path));
        auto* source = new QComboBox;
        source->setEditable(true);
        source->setInsertPolicy(QComboBox::NoInsert);
        source->setMinimumContentsLength(24);
        source->addItems(sourceNames);
        source->setCurrentText(file.completeBaseName());
        form->addRow(label, source);
        m_sources.append(source);
    }
    // Many files scroll instead of making the dialog taller than the screen.
    auto* scroll = new QScrollArea;
    scroll->setWidget(rows);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("The records of each file go to this source:")));
    layout->addWidget(scroll);
    layout->addWidget(buttons);
}

QVector<ImportFile> ImportDialog::files() const {
    QVector<ImportFile> files;
    files.reserve(m_paths.size());
    for (qsizetype i = 0; i < m_paths.size(); ++i) {
        // A blank name is allowed: the importer uses the file name then.
        files.append({m_paths[i], m_sources[i]->currentText().trimmed()});
    }
    return files;
}
