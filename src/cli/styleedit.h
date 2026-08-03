#pragma once

#include <QString>

// Rewrites one `Lo.style` literal in a QML document, addressed by where the
// item holding it is declared.
//
// Separate from lspdocument.h so the LSP-facing API keeps its shape, but built
// on the same QQmlJS parse: the inspector must never be able to write a file
// that `loom lint` would then reject, and sharing the parser is what makes that
// true by construction rather than by agreement.
namespace loom::styleedit {

struct Result {
    bool ok = false;
    //! Why the edit was refused, ready to send back over an Error frame.
    QString error;
    //! The whole document with the one literal replaced. Only set when ok.
    QString updated;
};

// `line` and `column` are 1-based and address the *item's* declaration, which
// is what QQmlData reports for a live object. Refuses when:
//
//   - nothing is declared there, or it has no `Lo.style` binding;
//   - the binding is not a single string literal -- a ternary or a
//     concatenation cannot be rewritten, because the running scene knows only
//     the evaluated result and cannot say which branch produced it. Guessing
//     would write a silent bug into the user's source;
//   - the literal no longer says `oldStyle`, meaning the file changed under the
//     running scene;
//   - `newStyle` contains a class the compiler does not recognise.
Result apply(
    const QString &text, int line, int column, const QString &oldStyle,
    const QString &newStyle);

// The same, reading and writing a file. The write is atomic (QSaveFile), so a
// crash mid-edit cannot leave the user with half a document.
Result applyToFile(
    const QString &path, int line, int column, const QString &oldStyle,
    const QString &newStyle);

} // namespace loom::styleedit
