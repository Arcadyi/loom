#pragma once

#include "lspdocument.h"
#include "styleworkspace.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace lsp {

class StyleIntelligence final {
public:
    explicit StyleIntelligence(StyleWorkspace *workspace);

    QJsonObject completion(
        const QString &filePath, const Document &document, qsizetype offset,
        PositionEncoding encoding, bool *inStyle = nullptr);
    QJsonArray diagnostics(
        const QString &filePath, const Document &document, PositionEncoding encoding);
    //! An LSP Location for the design-file declaration a class names, or null
    //! when the offset is not inside a class or nothing declares it.
    QJsonValue definition(
        const QString &filePath, const Document &document, qsizetype offset,
        PositionEncoding encoding);
    QJsonValue hover(
        const QString &filePath, const Document &document, qsizetype offset,
        PositionEncoding encoding);
    QJsonArray
    colors(const QString &filePath, const Document &document, PositionEncoding encoding);
    QJsonArray codeActions(const QString &uri, const QJsonArray &diagnostics) const;

private:
    StyleWorkspace *m_workspace = nullptr;
};

} // namespace lsp
