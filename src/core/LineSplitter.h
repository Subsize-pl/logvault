#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QVector>

#include <optional>

/// Splits a byte stream into lines, with the same rules for TCP clients and imported files: '\n' ends a
/// line and a '\r' before it is dropped; blank lines are skipped. A line longer than kMaxLineBytes is cut to
/// that length (never inside a UTF-8 character) and flagged, and the rest of it is skipped up to the next
/// '\n'. Memory stays bounded: at most kMaxLineBytes + 1 bytes of an unfinished line are kept.
class LineSplitter {
public:
    static constexpr qsizetype kMaxLineBytes = 64 * 1024;

    struct Line {
        QByteArray text; // without the line break
        bool truncated = false;
    };

    /// Adds the next bytes of the stream and returns the lines they complete.
    QVector<Line> append(QByteArrayView data);

    /// Ends the stream: returns the unfinished last line, if there is one, and resets the splitter.
    std::optional<Line> finish();

private:
    void addPiece(QByteArrayView piece, QVector<Line>& lines);
    void endLine(QVector<Line>& lines);

    QByteArray m_partial;    // the line received so far
    bool m_skipping = false; // the line was already passed on cut; drop bytes up to the next '\n'
};
