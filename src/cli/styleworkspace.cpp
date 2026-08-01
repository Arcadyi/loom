#include "styleworkspace.h"

#include "projectmanifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <loom/loom.h>

#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

namespace lsp {

namespace {

QString number(qreal value)
{
    return QString::number(value, 'g', 6);
}

QString colorName(const QColor &color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb)
                                : color.name(QColor::HexArgb);
}

QString effectName(LoomUtility utility)
{
    switch (utility) {
    case LoomUtility::BgColor:
        return QStringLiteral("background color");
    case LoomUtility::TextColor:
        return QStringLiteral("text color");
    case LoomUtility::TextSize:
        return QStringLiteral("text size");
    case LoomUtility::FontWeight:
        return QStringLiteral("font weight");
    case LoomUtility::FontFamily:
        return QStringLiteral("font family");
    case LoomUtility::Italic:
        return QStringLiteral("italic");
    case LoomUtility::Underline:
        return QStringLiteral("underline");
    case LoomUtility::LineThrough:
        return QStringLiteral("line through");
    case LoomUtility::Tracking:
        return QStringLiteral("letter spacing");
    case LoomUtility::TextAlignment:
        return QStringLiteral("text alignment");
    case LoomUtility::TextElide:
        return QStringLiteral("text overflow");
    case LoomUtility::TextMaximumLines:
        return QStringLiteral("maximum lines");
    case LoomUtility::TextCapitalization:
        return QStringLiteral("capitalization");
    case LoomUtility::TextWrapMode:
        return QStringLiteral("text wrapping");
    case LoomUtility::LineHeight:
        return QStringLiteral("line height");
    case LoomUtility::PaddingTop:
        return QStringLiteral("top padding");
    case LoomUtility::PaddingRight:
        return QStringLiteral("right padding");
    case LoomUtility::PaddingBottom:
        return QStringLiteral("bottom padding");
    case LoomUtility::PaddingLeft:
        return QStringLiteral("left padding");
    case LoomUtility::MarginTop:
        return QStringLiteral("top margin");
    case LoomUtility::MarginRight:
        return QStringLiteral("right margin");
    case LoomUtility::MarginBottom:
        return QStringLiteral("bottom margin");
    case LoomUtility::MarginLeft:
        return QStringLiteral("left margin");
    case LoomUtility::Gap:
        return QStringLiteral("gap");
    case LoomUtility::Width:
        return QStringLiteral("width");
    case LoomUtility::Height:
        return QStringLiteral("height");
    case LoomUtility::WidthFull:
        return QStringLiteral("width");
    case LoomUtility::HeightFull:
        return QStringLiteral("height");
    case LoomUtility::Radius:
        return QStringLiteral("corner radius");
    case LoomUtility::RadiusTopLeft:
        return QStringLiteral("top-left radius");
    case LoomUtility::RadiusTopRight:
        return QStringLiteral("top-right radius");
    case LoomUtility::RadiusBottomRight:
        return QStringLiteral("bottom-right radius");
    case LoomUtility::RadiusBottomLeft:
        return QStringLiteral("bottom-left radius");
    case LoomUtility::BorderWidth:
        return QStringLiteral("border width");
    case LoomUtility::BorderColor:
        return QStringLiteral("border color");
    case LoomUtility::Opacity:
        return QStringLiteral("opacity");
    case LoomUtility::Visible:
        return QStringLiteral("visible");
    case LoomUtility::Clip:
        return QStringLiteral("clip");
    case LoomUtility::ZOrder:
        return QStringLiteral("z order");
    case LoomUtility::Rotation:
        return QStringLiteral("rotation");
    case LoomUtility::Scale:
        return QStringLiteral("scale");
    case LoomUtility::TransformOrigin:
        return QStringLiteral("transform origin");
    case LoomUtility::CursorShape:
        return QStringLiteral("cursor shape");
    case LoomUtility::TranslateX:
        return QStringLiteral("horizontal translation");
    case LoomUtility::TranslateY:
        return QStringLiteral("vertical translation");
    case LoomUtility::AnchorFill:
        return QStringLiteral("fill parent");
    case LoomUtility::AnchorFillX:
        return QStringLiteral("fill parent horizontally");
    case LoomUtility::AnchorFillY:
        return QStringLiteral("fill parent vertically");
    case LoomUtility::AnchorCenter:
        return QStringLiteral("center in parent");
    case LoomUtility::AnchorCenterX:
        return QStringLiteral("center horizontally");
    case LoomUtility::AnchorCenterY:
        return QStringLiteral("center vertically");
    case LoomUtility::AnchorPinTop:
        return QStringLiteral("pin top");
    case LoomUtility::AnchorPinRight:
        return QStringLiteral("pin right");
    case LoomUtility::AnchorPinBottom:
        return QStringLiteral("pin bottom");
    case LoomUtility::AnchorPinLeft:
        return QStringLiteral("pin left");
    case LoomUtility::LayoutAlignment:
        return QStringLiteral("layout alignment");
    case LoomUtility::LayoutMinWidth:
        return QStringLiteral("minimum width");
    case LoomUtility::LayoutMaxWidth:
        return QStringLiteral("maximum width");
    case LoomUtility::LayoutMinHeight:
        return QStringLiteral("minimum height");
    case LoomUtility::LayoutMaxHeight:
        return QStringLiteral("maximum height");
    case LoomUtility::LayoutColumnSpan:
        return QStringLiteral("column span");
    case LoomUtility::LayoutRowSpan:
        return QStringLiteral("row span");
    case LoomUtility::AspectRatio:
        return QStringLiteral("aspect ratio");
    case LoomUtility::Shadow:
        return QStringLiteral("shadow");
    case LoomUtility::RingWidth:
        return QStringLiteral("ring width");
    case LoomUtility::RingColor:
        return QStringLiteral("ring color");
    case LoomUtility::GradientDirection:
        return QStringLiteral("gradient direction");
    case LoomUtility::GradientFrom:
        return QStringLiteral("gradient start color");
    case LoomUtility::GradientVia:
        return QStringLiteral("gradient middle color");
    case LoomUtility::GradientTo:
        return QStringLiteral("gradient end color");
    case LoomUtility::FilterBlur:
        return QStringLiteral("blur");
    case LoomUtility::FilterBrightness:
        return QStringLiteral("brightness");
    case LoomUtility::FilterContrast:
        return QStringLiteral("contrast");
    case LoomUtility::FilterSaturation:
        return QStringLiteral("saturation");
    case LoomUtility::TransitionMode:
        return QStringLiteral("transition properties");
    case LoomUtility::TransitionDuration:
        return QStringLiteral("transition duration");
    case LoomUtility::TransitionEase:
        return QStringLiteral("transition easing");
    }
    return QStringLiteral("utility");
}

QString ruleValue(const LoomStyleRule &rule, QColor *preview)
{
    const auto *registry = LoomTokenRegistry::instance();
    switch (rule.utility) {
    case LoomUtility::BgColor:
    case LoomUtility::TextColor:
    case LoomUtility::BorderColor:
    case LoomUtility::RingColor:
    case LoomUtility::GradientFrom:
    case LoomUtility::GradientVia:
    case LoomUtility::GradientTo: {
        QColor color =
            rule.arbitrary ? QColor::fromString(rule.key) : registry->color(rule.key);
        color.setAlphaF(color.alphaF() * qreal(rule.alphaPercent) / 100.0);
        if (preview && !preview->isValid())
            *preview = color;
        return colorName(color);
    }
    case LoomUtility::TextSize: {
        const auto style = rule.arbitrary
            ? LoomTextStyle{rule.literal, rule.literal * 1.5}
            : registry->textSize(rule.key);
        return QStringLiteral("%1px, line height %2px")
            .arg(number(style.size), number(style.lineHeight));
    }
    case LoomUtility::FontWeight:
        return QString::number(registry->fontWeight(rule.key));
    case LoomUtility::FontFamily:
        return rule.arbitrary ? rule.key
                              : registry->fontFamily(rule.key).join(QStringLiteral(", "));
    case LoomUtility::Tracking:
        return number(rule.arbitrary ? rule.literal : registry->tracking(rule.key))
            + QStringLiteral("em");
    case LoomUtility::LineHeight:
        return number(
                   rule.arbitrary ? rule.literal
                                  : registry->textSize(rule.key).lineHeight)
            + QStringLiteral("px");
    case LoomUtility::PaddingTop:
    case LoomUtility::PaddingRight:
    case LoomUtility::PaddingBottom:
    case LoomUtility::PaddingLeft:
    case LoomUtility::MarginTop:
    case LoomUtility::MarginRight:
    case LoomUtility::MarginBottom:
    case LoomUtility::MarginLeft:
    case LoomUtility::Gap:
    case LoomUtility::Width:
    case LoomUtility::Height:
    case LoomUtility::TranslateX:
    case LoomUtility::TranslateY:
    case LoomUtility::LayoutMinWidth:
    case LoomUtility::LayoutMaxWidth:
    case LoomUtility::LayoutMinHeight:
    case LoomUtility::LayoutMaxHeight:
        if (rule.fraction > 0)
            return number(rule.fraction * 100) + QStringLiteral("% of parent");
        return number(rule.arbitrary ? rule.literal : registry->space(rule.key))
            + QStringLiteral("px");
    case LoomUtility::Radius:
    case LoomUtility::RadiusTopLeft:
    case LoomUtility::RadiusTopRight:
    case LoomUtility::RadiusBottomRight:
    case LoomUtility::RadiusBottomLeft:
        return number(rule.arbitrary ? rule.literal : registry->radius(rule.key))
            + QStringLiteral("px");
    case LoomUtility::BorderWidth:
        return number(rule.literal) + QStringLiteral("px");
    case LoomUtility::Opacity:
        return number(rule.arbitrary ? rule.literal : registry->opacityValue(rule.key));
    case LoomUtility::WidthFull:
    case LoomUtility::HeightFull:
        return QStringLiteral("100% of parent");
    case LoomUtility::Visible:
    case LoomUtility::Italic:
    case LoomUtility::Underline:
    case LoomUtility::LineThrough:
    case LoomUtility::Clip:
        return rule.flag ? QStringLiteral("true") : QStringLiteral("false");
    case LoomUtility::TextAlignment:
    case LoomUtility::TextElide:
    case LoomUtility::TextMaximumLines:
    case LoomUtility::TextCapitalization:
    case LoomUtility::TextWrapMode:
    case LoomUtility::ZOrder:
    case LoomUtility::Rotation:
    case LoomUtility::Scale:
    case LoomUtility::TransformOrigin:
    case LoomUtility::CursorShape:
    case LoomUtility::GradientDirection:
        return number(rule.literal);
    case LoomUtility::LayoutColumnSpan:
    case LoomUtility::LayoutRowSpan:
        return QString::number(int(rule.literal));
    case LoomUtility::AspectRatio:
        return number(rule.literal);
    case LoomUtility::Shadow: {
        const auto shadow = registry->shadow(rule.key);
        return QStringLiteral("%1px %2px, blur %3px, spread %4px, %5")
            .arg(
                number(shadow.offsetX), number(shadow.offsetY), number(shadow.blur),
                number(shadow.spread), colorName(shadow.color));
    }
    case LoomUtility::RingWidth:
    case LoomUtility::FilterBlur:
        return number(rule.literal) + QStringLiteral("px");
    case LoomUtility::FilterBrightness:
    case LoomUtility::FilterContrast:
    case LoomUtility::FilterSaturation:
        return number(rule.literal) + QLatin1Char('%');
    case LoomUtility::TransitionDuration:
        return QString::number(registry->duration(rule.key)) + QStringLiteral("ms");
    case LoomUtility::TransitionEase:
        return rule.key;
    case LoomUtility::TransitionMode: {
        static const QStringList modes{
            QStringLiteral("none"), QStringLiteral("colors and opacity"),
            QStringLiteral("colors"), QStringLiteral("opacity"),
            QStringLiteral("all Loom-managed properties")};
        const int index = int(rule.literal);
        return index >= 0 && index < modes.size() ? modes.at(index) : QString();
    }
    case LoomUtility::LayoutAlignment:
        return QStringLiteral("Qt alignment %1").arg(int(rule.literal));
    case LoomUtility::AnchorFill:
    case LoomUtility::AnchorFillX:
    case LoomUtility::AnchorFillY:
    case LoomUtility::AnchorCenter:
    case LoomUtility::AnchorCenterX:
    case LoomUtility::AnchorCenterY:
    case LoomUtility::AnchorPinTop:
    case LoomUtility::AnchorPinRight:
    case LoomUtility::AnchorPinBottom:
    case LoomUtility::AnchorPinLeft:
        return QStringLiteral("true");
    }
    return {};
}

int damerauLevenshtein(const QString &left, const QString &right)
{
    QVector<QVector<int>> distance(left.size() + 1, QVector<int>(right.size() + 1));
    for (qsizetype i = 0; i <= left.size(); ++i)
        distance[i][0] = int(i);
    for (qsizetype j = 0; j <= right.size(); ++j)
        distance[0][j] = int(j);
    for (qsizetype i = 1; i <= left.size(); ++i) {
        for (qsizetype j = 1; j <= right.size(); ++j) {
            const int cost = left.at(i - 1) == right.at(j - 1) ? 0 : 1;
            distance[i][j] = std::min(
                {distance[i - 1][j] + 1, distance[i][j - 1] + 1,
                 distance[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && left.at(i - 1) == right.at(j - 2)
                && left.at(i - 2) == right.at(j - 1)) {
                distance[i][j] = std::min(distance[i][j], distance[i - 2][j - 2] + 1);
            }
        }
    }
    return distance.last().last();
}

} // namespace

StyleWorkspace::StyleWorkspace(QObject *parent)
    : QObject(parent)
{
    connect(
        &m_watcher, &QFileSystemWatcher::fileChanged, this, &StyleWorkspace::markDirty);
    connect(
        &m_watcher, &QFileSystemWatcher::directoryChanged, this,
        &StyleWorkspace::markDirty);
}

QString StyleWorkspace::projectKey(const QString &filePath) const
{
    const QString directory = QFileInfo(filePath).absolutePath();
    const QString manifest = findManifest(directory);
    return manifest.isEmpty() ? QStringLiteral("<defaults>") : manifest;
}

StyleWorkspace::ProjectState &StyleWorkspace::stateFor(const QString &key)
{
    auto &state = m_projects[key];
    if (key != QStringLiteral("<defaults>"))
        state.manifestPath = key;
    return state;
}

void StyleWorkspace::watchPath(const QString &path)
{
    if (path.isEmpty())
        return;
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    const QString directory = QFileInfo(path).absolutePath();
    if (QFileInfo(directory).isDir() && !m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
}

void StyleWorkspace::activate(ProjectState &state, const QString &key)
{
    auto *registry = LoomTokenRegistry::instance();
    registry->resetToDefaults();
    LoomStyleCompiler::clearCache();

    if (key == QStringLiteral("<defaults>")) {
        state.dirty = false;
        m_activeKey = key;
        return;
    }

    ProjectManifest manifest;
    QString error;
    if (!ProjectManifest::load(state.manifestPath, manifest, &error)) {
        if (!state.warned)
            emit warning(
                QStringLiteral("could not load %1: %2").arg(state.manifestPath, error));
        state.warned = true;
        state.dirty = false;
        m_activeKey = key;
        watchPath(state.manifestPath);
        return;
    }

    state.designPath = manifest.resolvedDesignPath(state.manifestPath);
    watchPath(state.manifestPath);
    watchPath(state.designPath);

    if (!state.designPath.isEmpty()) {
        QFile design(state.designPath);
        QByteArray bytes;
        if (design.open(QIODevice::ReadOnly))
            bytes = design.readAll();
        bool loaded = !bytes.isEmpty() && loom::reloadConfigData(bytes, state.designPath);
        QByteArray appliedBytes;
        if (loaded) {
            state.lastValidDesign = bytes;
            appliedBytes = bytes;
            state.warned = false;
        } else if (!state.lastValidDesign.isEmpty()) {
            loaded = loom::reloadConfigData(state.lastValidDesign, state.designPath);
            if (loaded)
                appliedBytes = state.lastValidDesign;
        }
        if (loaded) {
            const QString configuredTheme = QJsonDocument::fromJson(appliedBytes)
                                                .object()
                                                .value(QStringLiteral("theme"))
                                                .toObject()
                                                .value(QStringLiteral("default"))
                                                .toString();
            if (!configuredTheme.isEmpty()
                && registry->themeNames().contains(configuredTheme)) {
                registry->setTheme(configuredTheme);
            }
        }
        if (!loaded && !state.warned) {
            emit warning(QStringLiteral("could not load design tokens %1; using %2")
                             .arg(
                                 state.designPath,
                                 state.lastValidDesign.isEmpty()
                                     ? QStringLiteral("built-in tokens")
                                     : QStringLiteral("the last valid version")));
            state.warned = true;
        }
    }
    state.dirty = false;
    m_activeKey = key;
}

bool StyleWorkspace::activateForFile(const QString &filePath)
{
    const QString key = projectKey(filePath);
    auto &state = stateFor(key);
    if (m_activeKey != key || state.dirty)
        activate(state, key);
    return true;
}

loom::StyleCatalogue StyleWorkspace::catalogue() const
{
    return loom::styleCatalogue();
}

QString StyleWorkspace::arbitraryValuePolicy() const
{
    return LoomTokenRegistry::instance()->arbitraryValuePolicy();
}

ClassMetadata StyleWorkspace::metadata(const QString &klass) const
{
    ClassMetadata metadata;
    if (!loom::unknownStyleClasses(klass).isEmpty())
        return metadata;

    const auto compiled = LoomStyleCompiler::compile(klass);
    if (compiled->rules.isEmpty())
        return metadata;

    metadata.valid = true;
    QStringList details;
    QSet<QString> seen;
    for (const auto &rule : compiled->rules) {
        const QString effect = effectName(rule.utility);
        const QString value = ruleValue(rule, &metadata.color);
        const QString line =
            value.isEmpty() ? effect : effect + QStringLiteral(": ") + value;
        if (!seen.contains(line)) {
            seen.insert(line);
            details.append(line);
        }
    }

    const QStringList segments = klass.split(QLatin1Char(':'));
    QStringList conditions;
    const auto *registry = LoomTokenRegistry::instance();
    for (qsizetype index = 0; index + 1 < segments.size(); ++index) {
        const QString variant = segments.at(index);
        if (registry->breakpointKeys().contains(variant)) {
            conditions.append(QStringLiteral("%1 >= %2px")
                                  .arg(variant)
                                  .arg(registry->breakpoint(variant)));
        } else {
            conditions.append(variant);
        }
    }

    metadata.detail = details.join(QStringLiteral("; "));
    QString markdown = QStringLiteral("`%1`\n\n").arg(klass);
    if (!conditions.isEmpty())
        markdown += QStringLiteral("Applies when **%1**.\n\n")
                        .arg(conditions.join(QStringLiteral(" + ")));
    for (const auto &detail : details)
        markdown += QStringLiteral("- %1\n").arg(detail);
    if (metadata.color.isValid()) {
        markdown += QStringLiteral("\nColor preview uses the **%1** theme.")
                        .arg(registry->theme());
    }
    metadata.markdown = markdown;
    return metadata;
}

QStringList StyleWorkspace::replacements(const QString &unknown) const
{
    const auto current = catalogue();
    QString prefix;
    QString base = unknown;
    const qsizetype colon = unknown.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString variantPrefix = unknown.left(colon);
        bool variantsValid = true;
        for (const auto &variant : variantPrefix.split(QLatin1Char(':'))) {
            if (!current.variants.contains(variant)) {
                variantsValid = false;
                break;
            }
        }
        if (variantsValid) {
            prefix = unknown.left(colon + 1);
            base = unknown.mid(colon + 1);
        }
    }

    struct Match {
        QString value;
        int distance = 0;
    };
    QList<Match> matches;
    for (const auto &candidateBase : current.classes) {
        const QString candidate = prefix + candidateBase;
        const int distance = damerauLevenshtein(unknown, candidate);
        if (distance <= 3 && distance * 4 <= candidate.size())
            matches.append({candidate, distance});
    }

    // If the utility is valid but one variant is misspelled, compare the whole
    // token against every one-variant form as well.
    if (colon >= 0 && loom::unknownStyleClasses(base).isEmpty()) {
        for (const auto &variant : current.variants) {
            const QString candidate = variant + QLatin1Char(':') + base;
            const int distance = damerauLevenshtein(unknown, candidate);
            if (distance <= 3 && distance * 4 <= candidate.size())
                matches.append({candidate, distance});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match &left, const Match &right) {
        if (left.distance != right.distance)
            return left.distance < right.distance;
        return left.value < right.value;
    });
    QStringList result;
    for (const auto &match : matches) {
        if (!result.contains(match.value))
            result.append(match.value);
        if (result.size() == 3)
            break;
    }
    return result;
}

void StyleWorkspace::markDirty(const QString &changedPath)
{
    for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
        if (it->manifestPath == changedPath || it->designPath == changedPath
            || QFileInfo(it->manifestPath).absolutePath() == changedPath
            || QFileInfo(it->designPath).absolutePath() == changedPath) {
            it->dirty = true;
        }
    }
    if (QFileInfo::exists(changedPath))
        watchPath(changedPath);
    emit vocabularyChanged();
}

} // namespace lsp
