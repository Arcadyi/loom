#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace lsp {

enum class PositionEncoding { Utf8, Utf16, Utf32 };

struct SourceRange {
    qsizetype start = 0;
    qsizetype end = 0;
};

struct StyleLiteral {
    SourceRange content;
};

struct StyleToken {
    SourceRange range;
    QString text;
};

class Document {
public:
    void open(QString text, int version);
    bool applyChanges(
        const QJsonArray &changes, int version, PositionEncoding encoding,
        QString *error = nullptr);

    QString text() const;
    int version() const;
    QVector<StyleLiteral> styleLiterals() const;
    QVector<StyleToken> styleTokens() const;
    bool styleTokenAt(qsizetype offset, StyleToken *token) const;

    qsizetype offsetAt(
        int line, int character, PositionEncoding encoding, bool *ok = nullptr) const;
    QJsonObject positionAt(qsizetype offset, PositionEncoding encoding) const;
    QJsonObject rangeObject(const SourceRange &range, PositionEncoding encoding) const;

private:
    QString m_text;
    int m_version = 0;
};

} // namespace lsp
