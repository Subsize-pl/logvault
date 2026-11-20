#pragma once

#include "FileImporter.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QComboBox;

/// Asks which source the records of each imported file go to (DESIGN.md section 5.3): the file name without
/// its extension at first; any existing source can be picked or another name typed.
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    ImportDialog(const QStringList& paths, const QStringList& sourceNames, QWidget* parent = nullptr);

    /// The files with the source names as entered.
    QVector<ImportFile> files() const;

private:
    QStringList m_paths;
    QVector<QComboBox*> m_sources; // one per path
};
