#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <loom/loomcatalogue.h>

namespace lsp {

struct ClassMetadata {
    bool valid = false;
    QString detail;
    QString markdown;
    QColor color;
};

// Activates the token registry belonging to the Loom project nearest a source
// file. The registry is process-wide, so activations are serialized on the LSP
// event loop; cached last-known-good design bytes make switching between
// workspaces deterministic even while one design file is temporarily invalid.
class StyleWorkspace final : public QObject {
    Q_OBJECT

public:
    explicit StyleWorkspace(QObject *parent = nullptr);

    bool activateForFile(const QString &filePath);
    loom::StyleCatalogue catalogue() const;
    ClassMetadata metadata(const QString &klass) const;

    //! Where a class's token, recipe or state is declared in the design file.
    struct Definition {
        QString path;
        int line = 0;   // 0-based, LSP's convention
        int column = 0; // 0-based
        bool valid() const { return !path.isEmpty(); }
    };
    Definition definition(const QString &klass) const;
    QStringList replacements(const QString &unknown) const;
    QString arbitraryValuePolicy() const;

signals:
    void vocabularyChanged();
    void warning(const QString &message);

private:
    struct ProjectState {
        QString manifestPath;
        QString designPath;
        QByteArray lastValidDesign;
        bool dirty = true;
        bool warned = false;
    };

    QString projectKey(const QString &filePath) const;
    ProjectState &stateFor(const QString &key);
    void activate(ProjectState &state, const QString &key);
    void watchPath(const QString &path);
    void markDirty(const QString &changedPath);

    QFileSystemWatcher m_watcher;
    QHash<QString, ProjectState> m_projects;
    QString m_activeKey;
};

} // namespace lsp
