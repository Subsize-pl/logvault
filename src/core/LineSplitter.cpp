#include "LineSplitter.h"

#include <algorithm>
#include <utility>

namespace {

bool isBlank(QByteArrayView text) {
    return std::all_of(text.begin(), text.end(),
                       [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; });
}

// The length of the longest prefix of at most maxBytes that does not end inside a UTF-8 character.
qsizetype utf8PrefixLength(QByteArrayView text, qsizetype maxBytes) {
    qsizetype end = maxBytes;
    // A continuation byte (10xxxxxx) at the cut means its character started before the cut. A character has
    // at most three continuation bytes, so invalid input cannot move the cut further back.
    for (int i = 0; i < 3 && end > 0 && (static_cast<uchar>(text[end]) & 0xC0) == 0x80; ++i) {
        --end;
    }
    return end;
}

// Adds a finished line to lines, cut to the limit if it is longer; blank lines carry no data and are skipped.
void addLine(QByteArray text, QVector<LineSplitter::Line>& lines) {
    bool truncated = false;
    if (text.size() > LineSplitter::kMaxLineBytes) {
        text.truncate(utf8PrefixLength(text, LineSplitter::kMaxLineBytes));
        truncated = true;
    }
    if (!isBlank(text)) {
        lines.append({std::move(text), truncated});
    }
}

} // namespace

QVector<LineSplitter::Line> LineSplitter::append(QByteArrayView data) {
    QVector<Line> lines;
    // A line may arrive split across several calls, so the unfinished tail is kept until its '\n' comes.
    while (!data.isEmpty()) {
        const qsizetype newline = data.indexOf('\n');
        if (newline < 0) {
            addPiece(data, lines);
            break;
        }
        addPiece(data.first(newline), lines);
        endLine(lines);
        data = data.sliced(newline + 1);
    }
    return lines;
}

std::optional<LineSplitter::Line> LineSplitter::finish() {
    QVector<Line> lines;
    endLine(lines);
    if (lines.isEmpty()) {
        return std::nullopt;
    }
    return lines.first();
}

void LineSplitter::addPiece(QByteArrayView piece, QVector<Line>& lines) {
    if (m_skipping) {
        return;
    }
    // One byte over the limit is kept: it may be the '\r' of a line exactly kMaxLineBytes long.
    const qsizetype room = kMaxLineBytes + 1 - m_partial.size();
    if (piece.size() <= room) {
        m_partial.append(piece);
        return;
    }
    // Longer than the limit whatever comes next: pass the start on now and drop the rest of the line.
    m_partial.append(piece.first(room));
    addLine(std::exchange(m_partial, {}), lines);
    m_skipping = true;
}

void LineSplitter::endLine(QVector<Line>& lines) {
    if (m_skipping) {
        m_skipping = false;
        return;
    }
    if (m_partial.endsWith('\r')) {
        m_partial.chop(1);
    }
    addLine(std::exchange(m_partial, {}), lines);
}
