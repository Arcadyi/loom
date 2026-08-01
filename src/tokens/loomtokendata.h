#pragma once

// Single source of truth for every design token Loom ships. Each table is an
// X-macro expanded into (a) the Q_PROPERTY blocks and READ accessors of the
// typed token groups, (b) the registry's storage, and (c) the utility-string
// lookup tables, so the typed API and the `Lo.style` grammar cannot drift.
//
// Palette values are the Tailwind CSS v3 default palette.

// X(cppName, "registry-key", "color")
#define LOOM_PALETTE_COLORS(X)                                                           \
    X(slate50, "slate-50", "#f8fafc")                                                    \
    X(slate100, "slate-100", "#f1f5f9")                                                  \
    X(slate200, "slate-200", "#e2e8f0")                                                  \
    X(slate300, "slate-300", "#cbd5e1")                                                  \
    X(slate400, "slate-400", "#94a3b8")                                                  \
    X(slate500, "slate-500", "#64748b")                                                  \
    X(slate600, "slate-600", "#475569")                                                  \
    X(slate700, "slate-700", "#334155")                                                  \
    X(slate800, "slate-800", "#1e293b")                                                  \
    X(slate900, "slate-900", "#0f172a")                                                  \
    X(slate950, "slate-950", "#020617")                                                  \
    X(gray50, "gray-50", "#f9fafb")                                                      \
    X(gray100, "gray-100", "#f3f4f6")                                                    \
    X(gray200, "gray-200", "#e5e7eb")                                                    \
    X(gray300, "gray-300", "#d1d5db")                                                    \
    X(gray400, "gray-400", "#9ca3af")                                                    \
    X(gray500, "gray-500", "#6b7280")                                                    \
    X(gray600, "gray-600", "#4b5563")                                                    \
    X(gray700, "gray-700", "#374151")                                                    \
    X(gray800, "gray-800", "#1f2937")                                                    \
    X(gray900, "gray-900", "#111827")                                                    \
    X(gray950, "gray-950", "#030712")                                                    \
    X(zinc50, "zinc-50", "#fafafa")                                                      \
    X(zinc100, "zinc-100", "#f4f4f5")                                                    \
    X(zinc200, "zinc-200", "#e4e4e7")                                                    \
    X(zinc300, "zinc-300", "#d4d4d8")                                                    \
    X(zinc400, "zinc-400", "#a1a1aa")                                                    \
    X(zinc500, "zinc-500", "#71717a")                                                    \
    X(zinc600, "zinc-600", "#52525b")                                                    \
    X(zinc700, "zinc-700", "#3f3f46")                                                    \
    X(zinc800, "zinc-800", "#27272a")                                                    \
    X(zinc900, "zinc-900", "#18181b")                                                    \
    X(zinc950, "zinc-950", "#09090b")                                                    \
    X(neutral50, "neutral-50", "#fafafa")                                                \
    X(neutral100, "neutral-100", "#f5f5f5")                                              \
    X(neutral200, "neutral-200", "#e5e5e5")                                              \
    X(neutral300, "neutral-300", "#d4d4d4")                                              \
    X(neutral400, "neutral-400", "#a3a3a3")                                              \
    X(neutral500, "neutral-500", "#737373")                                              \
    X(neutral600, "neutral-600", "#525252")                                              \
    X(neutral700, "neutral-700", "#404040")                                              \
    X(neutral800, "neutral-800", "#262626")                                              \
    X(neutral900, "neutral-900", "#171717")                                              \
    X(neutral950, "neutral-950", "#0a0a0a")                                              \
    X(stone50, "stone-50", "#fafaf9")                                                    \
    X(stone100, "stone-100", "#f5f5f4")                                                  \
    X(stone200, "stone-200", "#e7e5e4")                                                  \
    X(stone300, "stone-300", "#d6d3d1")                                                  \
    X(stone400, "stone-400", "#a8a29e")                                                  \
    X(stone500, "stone-500", "#78716c")                                                  \
    X(stone600, "stone-600", "#57534e")                                                  \
    X(stone700, "stone-700", "#44403c")                                                  \
    X(stone800, "stone-800", "#292524")                                                  \
    X(stone900, "stone-900", "#1c1917")                                                  \
    X(stone950, "stone-950", "#0c0a09")                                                  \
    X(red50, "red-50", "#fef2f2")                                                        \
    X(red100, "red-100", "#fee2e2")                                                      \
    X(red200, "red-200", "#fecaca")                                                      \
    X(red300, "red-300", "#fca5a5")                                                      \
    X(red400, "red-400", "#f87171")                                                      \
    X(red500, "red-500", "#ef4444")                                                      \
    X(red600, "red-600", "#dc2626")                                                      \
    X(red700, "red-700", "#b91c1c")                                                      \
    X(red800, "red-800", "#991b1b")                                                      \
    X(red900, "red-900", "#7f1d1d")                                                      \
    X(red950, "red-950", "#450a0a")                                                      \
    X(orange50, "orange-50", "#fff7ed")                                                  \
    X(orange100, "orange-100", "#ffedd5")                                                \
    X(orange200, "orange-200", "#fed7aa")                                                \
    X(orange300, "orange-300", "#fdba74")                                                \
    X(orange400, "orange-400", "#fb923c")                                                \
    X(orange500, "orange-500", "#f97316")                                                \
    X(orange600, "orange-600", "#ea580c")                                                \
    X(orange700, "orange-700", "#c2410c")                                                \
    X(orange800, "orange-800", "#9a3412")                                                \
    X(orange900, "orange-900", "#7c2d12")                                                \
    X(orange950, "orange-950", "#431407")                                                \
    X(amber50, "amber-50", "#fffbeb")                                                    \
    X(amber100, "amber-100", "#fef3c7")                                                  \
    X(amber200, "amber-200", "#fde68a")                                                  \
    X(amber300, "amber-300", "#fcd34d")                                                  \
    X(amber400, "amber-400", "#fbbf24")                                                  \
    X(amber500, "amber-500", "#f59e0b")                                                  \
    X(amber600, "amber-600", "#d97706")                                                  \
    X(amber700, "amber-700", "#b45309")                                                  \
    X(amber800, "amber-800", "#92400e")                                                  \
    X(amber900, "amber-900", "#78350f")                                                  \
    X(amber950, "amber-950", "#451a03")                                                  \
    X(yellow50, "yellow-50", "#fefce8")                                                  \
    X(yellow100, "yellow-100", "#fef9c3")                                                \
    X(yellow200, "yellow-200", "#fef08a")                                                \
    X(yellow300, "yellow-300", "#fde047")                                                \
    X(yellow400, "yellow-400", "#facc15")                                                \
    X(yellow500, "yellow-500", "#eab308")                                                \
    X(yellow600, "yellow-600", "#ca8a04")                                                \
    X(yellow700, "yellow-700", "#a16207")                                                \
    X(yellow800, "yellow-800", "#854d0e")                                                \
    X(yellow900, "yellow-900", "#713f12")                                                \
    X(yellow950, "yellow-950", "#422006")                                                \
    X(lime50, "lime-50", "#f7fee7")                                                      \
    X(lime100, "lime-100", "#ecfccb")                                                    \
    X(lime200, "lime-200", "#d9f99d")                                                    \
    X(lime300, "lime-300", "#bef264")                                                    \
    X(lime400, "lime-400", "#a3e635")                                                    \
    X(lime500, "lime-500", "#84cc16")                                                    \
    X(lime600, "lime-600", "#65a30d")                                                    \
    X(lime700, "lime-700", "#4d7c0f")                                                    \
    X(lime800, "lime-800", "#3f6212")                                                    \
    X(lime900, "lime-900", "#365314")                                                    \
    X(lime950, "lime-950", "#1a2e05")                                                    \
    X(green50, "green-50", "#f0fdf4")                                                    \
    X(green100, "green-100", "#dcfce7")                                                  \
    X(green200, "green-200", "#bbf7d0")                                                  \
    X(green300, "green-300", "#86efac")                                                  \
    X(green400, "green-400", "#4ade80")                                                  \
    X(green500, "green-500", "#22c55e")                                                  \
    X(green600, "green-600", "#16a34a")                                                  \
    X(green700, "green-700", "#15803d")                                                  \
    X(green800, "green-800", "#166534")                                                  \
    X(green900, "green-900", "#14532d")                                                  \
    X(green950, "green-950", "#052e16")                                                  \
    X(emerald50, "emerald-50", "#ecfdf5")                                                \
    X(emerald100, "emerald-100", "#d1fae5")                                              \
    X(emerald200, "emerald-200", "#a7f3d0")                                              \
    X(emerald300, "emerald-300", "#6ee7b7")                                              \
    X(emerald400, "emerald-400", "#34d399")                                              \
    X(emerald500, "emerald-500", "#10b981")                                              \
    X(emerald600, "emerald-600", "#059669")                                              \
    X(emerald700, "emerald-700", "#047857")                                              \
    X(emerald800, "emerald-800", "#065f46")                                              \
    X(emerald900, "emerald-900", "#064e3b")                                              \
    X(emerald950, "emerald-950", "#022c22")                                              \
    X(teal50, "teal-50", "#f0fdfa")                                                      \
    X(teal100, "teal-100", "#ccfbf1")                                                    \
    X(teal200, "teal-200", "#99f6e4")                                                    \
    X(teal300, "teal-300", "#5eead4")                                                    \
    X(teal400, "teal-400", "#2dd4bf")                                                    \
    X(teal500, "teal-500", "#14b8a6")                                                    \
    X(teal600, "teal-600", "#0d9488")                                                    \
    X(teal700, "teal-700", "#0f766e")                                                    \
    X(teal800, "teal-800", "#115e59")                                                    \
    X(teal900, "teal-900", "#134e4a")                                                    \
    X(teal950, "teal-950", "#042f2e")                                                    \
    X(cyan50, "cyan-50", "#ecfeff")                                                      \
    X(cyan100, "cyan-100", "#cffafe")                                                    \
    X(cyan200, "cyan-200", "#a5f3fc")                                                    \
    X(cyan300, "cyan-300", "#67e8f9")                                                    \
    X(cyan400, "cyan-400", "#22d3ee")                                                    \
    X(cyan500, "cyan-500", "#06b6d4")                                                    \
    X(cyan600, "cyan-600", "#0891b2")                                                    \
    X(cyan700, "cyan-700", "#0e7490")                                                    \
    X(cyan800, "cyan-800", "#155e75")                                                    \
    X(cyan900, "cyan-900", "#164e63")                                                    \
    X(cyan950, "cyan-950", "#083344")                                                    \
    X(sky50, "sky-50", "#f0f9ff")                                                        \
    X(sky100, "sky-100", "#e0f2fe")                                                      \
    X(sky200, "sky-200", "#bae6fd")                                                      \
    X(sky300, "sky-300", "#7dd3fc")                                                      \
    X(sky400, "sky-400", "#38bdf8")                                                      \
    X(sky500, "sky-500", "#0ea5e9")                                                      \
    X(sky600, "sky-600", "#0284c7")                                                      \
    X(sky700, "sky-700", "#0369a1")                                                      \
    X(sky800, "sky-800", "#075985")                                                      \
    X(sky900, "sky-900", "#0c4a6e")                                                      \
    X(sky950, "sky-950", "#082f49")                                                      \
    X(blue50, "blue-50", "#eff6ff")                                                      \
    X(blue100, "blue-100", "#dbeafe")                                                    \
    X(blue200, "blue-200", "#bfdbfe")                                                    \
    X(blue300, "blue-300", "#93c5fd")                                                    \
    X(blue400, "blue-400", "#60a5fa")                                                    \
    X(blue500, "blue-500", "#3b82f6")                                                    \
    X(blue600, "blue-600", "#2563eb")                                                    \
    X(blue700, "blue-700", "#1d4ed8")                                                    \
    X(blue800, "blue-800", "#1e40af")                                                    \
    X(blue900, "blue-900", "#1e3a8a")                                                    \
    X(blue950, "blue-950", "#172554")                                                    \
    X(indigo50, "indigo-50", "#eef2ff")                                                  \
    X(indigo100, "indigo-100", "#e0e7ff")                                                \
    X(indigo200, "indigo-200", "#c7d2fe")                                                \
    X(indigo300, "indigo-300", "#a5b4fc")                                                \
    X(indigo400, "indigo-400", "#818cf8")                                                \
    X(indigo500, "indigo-500", "#6366f1")                                                \
    X(indigo600, "indigo-600", "#4f46e5")                                                \
    X(indigo700, "indigo-700", "#4338ca")                                                \
    X(indigo800, "indigo-800", "#3730a3")                                                \
    X(indigo900, "indigo-900", "#312e81")                                                \
    X(indigo950, "indigo-950", "#1e1b4b")                                                \
    X(violet50, "violet-50", "#f5f3ff")                                                  \
    X(violet100, "violet-100", "#ede9fe")                                                \
    X(violet200, "violet-200", "#ddd6fe")                                                \
    X(violet300, "violet-300", "#c4b5fd")                                                \
    X(violet400, "violet-400", "#a78bfa")                                                \
    X(violet500, "violet-500", "#8b5cf6")                                                \
    X(violet600, "violet-600", "#7c3aed")                                                \
    X(violet700, "violet-700", "#6d28d9")                                                \
    X(violet800, "violet-800", "#5b21b6")                                                \
    X(violet900, "violet-900", "#4c1d95")                                                \
    X(violet950, "violet-950", "#2e1065")                                                \
    X(purple50, "purple-50", "#faf5ff")                                                  \
    X(purple100, "purple-100", "#f3e8ff")                                                \
    X(purple200, "purple-200", "#e9d5ff")                                                \
    X(purple300, "purple-300", "#d8b4fe")                                                \
    X(purple400, "purple-400", "#c084fc")                                                \
    X(purple500, "purple-500", "#a855f7")                                                \
    X(purple600, "purple-600", "#9333ea")                                                \
    X(purple700, "purple-700", "#7e22ce")                                                \
    X(purple800, "purple-800", "#6b21a8")                                                \
    X(purple900, "purple-900", "#581c87")                                                \
    X(purple950, "purple-950", "#3b0764")                                                \
    X(fuchsia50, "fuchsia-50", "#fdf4ff")                                                \
    X(fuchsia100, "fuchsia-100", "#fae8ff")                                              \
    X(fuchsia200, "fuchsia-200", "#f5d0fe")                                              \
    X(fuchsia300, "fuchsia-300", "#f0abfc")                                              \
    X(fuchsia400, "fuchsia-400", "#e879f9")                                              \
    X(fuchsia500, "fuchsia-500", "#d946ef")                                              \
    X(fuchsia600, "fuchsia-600", "#c026d3")                                              \
    X(fuchsia700, "fuchsia-700", "#a21caf")                                              \
    X(fuchsia800, "fuchsia-800", "#86198f")                                              \
    X(fuchsia900, "fuchsia-900", "#701a75")                                              \
    X(fuchsia950, "fuchsia-950", "#4a044e")                                              \
    X(pink50, "pink-50", "#fdf2f8")                                                      \
    X(pink100, "pink-100", "#fce7f3")                                                    \
    X(pink200, "pink-200", "#fbcfe8")                                                    \
    X(pink300, "pink-300", "#f9a8d4")                                                    \
    X(pink400, "pink-400", "#f472b6")                                                    \
    X(pink500, "pink-500", "#ec4899")                                                    \
    X(pink600, "pink-600", "#db2777")                                                    \
    X(pink700, "pink-700", "#be185d")                                                    \
    X(pink800, "pink-800", "#9d174d")                                                    \
    X(pink900, "pink-900", "#831843")                                                    \
    X(pink950, "pink-950", "#500724")                                                    \
    X(rose50, "rose-50", "#fff1f2")                                                      \
    X(rose100, "rose-100", "#ffe4e6")                                                    \
    X(rose200, "rose-200", "#fecdd3")                                                    \
    X(rose300, "rose-300", "#fda4af")                                                    \
    X(rose400, "rose-400", "#fb7185")                                                    \
    X(rose500, "rose-500", "#f43f5e")                                                    \
    X(rose600, "rose-600", "#e11d48")                                                    \
    X(rose700, "rose-700", "#be123c")                                                    \
    X(rose800, "rose-800", "#9f1239")                                                    \
    X(rose900, "rose-900", "#881337")                                                    \
    X(rose950, "rose-950", "#4c0519")                                                    \
    X(white, "white", "#ffffff")                                                         \
    X(black, "black", "#000000")                                                         \
    X(transparent, "transparent", "transparent")

// Semantic, theme-dependent colors. Values are palette keys or color literals.
// X(cppName, "registry-key", "light value", "dark value")
#define LOOM_SEMANTIC_COLORS(X)                                                          \
    X(background, "background", "slate-50", "slate-950")                                 \
    X(surface, "surface", "white", "slate-900")                                          \
    X(surfaceAlt, "surface-alt", "slate-100", "slate-800")                               \
    X(outline, "outline", "slate-200", "slate-700")                                      \
    X(foreground, "foreground", "slate-900", "slate-50")                                 \
    X(muted, "muted", "slate-500", "slate-400")                                          \
    X(faint, "faint", "slate-400", "slate-600")                                          \
    X(accent, "accent", "blue-600", "blue-500")                                          \
    X(accentHover, "accent-hover", "blue-700", "blue-400")                               \
    X(onAccent, "on-accent", "white", "white")                                           \
    X(danger, "danger", "red-600", "red-500")                                            \
    X(onDanger, "on-danger", "white", "white")                                           \
    X(success, "success", "green-600", "green-500")                                      \
    X(warning, "warning", "amber-500", "amber-400")

// Spacing scale: Tailwind key -> key * 4 px. X(cppName, "registry-key", px)
#define LOOM_SPACE_TOKENS(X)                                                             \
    X(s0, "0", 0)                                                                        \
    X(s0_5, "0.5", 2)                                                                    \
    X(s1, "1", 4)                                                                        \
    X(s1_5, "1.5", 6)                                                                    \
    X(s2, "2", 8)                                                                        \
    X(s2_5, "2.5", 10)                                                                   \
    X(s3, "3", 12)                                                                       \
    X(s3_5, "3.5", 14)                                                                   \
    X(s4, "4", 16)                                                                       \
    X(s5, "5", 20)                                                                       \
    X(s6, "6", 24)                                                                       \
    X(s7, "7", 28)                                                                       \
    X(s8, "8", 32)                                                                       \
    X(s9, "9", 36)                                                                       \
    X(s10, "10", 40)                                                                     \
    X(s11, "11", 44)                                                                     \
    X(s12, "12", 48)                                                                     \
    X(s14, "14", 56)                                                                     \
    X(s16, "16", 64)                                                                     \
    X(s20, "20", 80)                                                                     \
    X(s24, "24", 96)                                                                     \
    X(s28, "28", 112)                                                                    \
    X(s32, "32", 128)                                                                    \
    X(s36, "36", 144)                                                                    \
    X(s40, "40", 160)                                                                    \
    X(s44, "44", 176)                                                                    \
    X(s48, "48", 192)                                                                    \
    X(s52, "52", 208)                                                                    \
    X(s56, "56", 224)                                                                    \
    X(s60, "60", 240)                                                                    \
    X(s64, "64", 256)                                                                    \
    X(s72, "72", 288)                                                                    \
    X(s80, "80", 320)                                                                    \
    X(s96, "96", 384)

// Type scale. "2xl" is not a valid identifier, so the typed side uses x2l.
// X(cppName, "registry-key", sizePx, lineHeightPx)
#define LOOM_TEXT_SIZES(X)                                                               \
    X(xs, "xs", 12, 16)                                                                  \
    X(sm, "sm", 14, 20)                                                                  \
    X(base, "base", 16, 24)                                                              \
    X(lg, "lg", 18, 28)                                                                  \
    X(xl, "xl", 20, 28)                                                                  \
    X(x2l, "2xl", 24, 32)                                                                \
    X(x3l, "3xl", 30, 36)                                                                \
    X(x4l, "4xl", 36, 40)

// X(cppName, "registry-key", weight)
#define LOOM_FONT_WEIGHTS(X)                                                             \
    X(thin, "thin", 100)                                                                 \
    X(extralight, "extralight", 200)                                                     \
    X(light, "light", 300)                                                               \
    X(normal, "normal", 400)                                                             \
    X(medium, "medium", 500)                                                             \
    X(semibold, "semibold", 600)                                                         \
    X(bold, "bold", 700)                                                                 \
    X(extrabold, "extrabold", 800)                                                       \
    X(black, "black", 900)

// Letter spacing in em (multiplied by the font size at apply time).
// X(cppName, "registry-key", em)
#define LOOM_TRACKING_TOKENS(X)                                                          \
    X(trackingTighter, "tighter", -0.05)                                                 \
    X(trackingTight, "tight", -0.025)                                                    \
    X(trackingNormal, "normal", 0.0)                                                     \
    X(trackingWide, "wide", 0.025)                                                       \
    X(trackingWider, "wider", 0.05)

// X(cppName, "registry-key", px). "base" is the bare `rounded` utility.
#define LOOM_RADIUS_TOKENS(X)                                                            \
    X(none, "none", 0)                                                                   \
    X(sm, "sm", 2)                                                                       \
    X(base, "base", 4)                                                                   \
    X(md, "md", 6)                                                                       \
    X(lg, "lg", 8)                                                                       \
    X(xl, "xl", 12)                                                                      \
    X(x2l, "2xl", 16)                                                                    \
    X(x3l, "3xl", 24)                                                                    \
    X(full, "full", 9999)

// Single-layer approximations of the Tailwind box shadows.
// X(cppName, "registry-key", offsetY, blur, spread, alphaPercent)
#define LOOM_SHADOW_TOKENS(X)                                                            \
    X(none, "none", 0, 0, 0, 0)                                                          \
    X(sm, "sm", 1, 2, 0, 5)                                                              \
    X(base, "base", 1, 3, 0, 10)                                                         \
    X(md, "md", 4, 6, -1, 10)                                                            \
    X(lg, "lg", 10, 15, -3, 10)                                                          \
    X(xl, "xl", 20, 25, -5, 10)                                                          \
    X(x2l, "2xl", 25, 50, -12, 25)

// X(cppName, "registry-key", value 0..1)
#define LOOM_OPACITY_TOKENS(X)                                                           \
    X(o0, "0", 0.0)                                                                      \
    X(o5, "5", 0.05)                                                                     \
    X(o10, "10", 0.1)                                                                    \
    X(o15, "15", 0.15)                                                                   \
    X(o20, "20", 0.2)                                                                    \
    X(o25, "25", 0.25)                                                                   \
    X(o30, "30", 0.3)                                                                    \
    X(o35, "35", 0.35)                                                                   \
    X(o40, "40", 0.4)                                                                    \
    X(o45, "45", 0.45)                                                                   \
    X(o50, "50", 0.5)                                                                    \
    X(o55, "55", 0.55)                                                                   \
    X(o60, "60", 0.6)                                                                    \
    X(o65, "65", 0.65)                                                                   \
    X(o70, "70", 0.7)                                                                    \
    X(o75, "75", 0.75)                                                                   \
    X(o80, "80", 0.8)                                                                    \
    X(o85, "85", 0.85)                                                                   \
    X(o90, "90", 0.9)                                                                    \
    X(o95, "95", 0.95)                                                                   \
    X(o100, "100", 1.0)

// X(cppName, "registry-key", ms)
#define LOOM_DURATION_TOKENS(X)                                                          \
    X(d75, "75", 75)                                                                     \
    X(d100, "100", 100)                                                                  \
    X(d150, "150", 150)                                                                  \
    X(d200, "200", 200)                                                                  \
    X(d300, "300", 300)                                                                  \
    X(d500, "500", 500)                                                                  \
    X(d700, "700", 700)                                                                  \
    X(d1000, "1000", 1000)

// CSS transition-timing-function curves as cubic beziers.
// X(cppName, "registry-key", x1, y1, x2, y2)
#define LOOM_EASING_TOKENS(X)                                                            \
    X(linear, "linear", 0.0, 0.0, 1.0, 1.0)                                              \
    X(easeIn, "in", 0.4, 0.0, 1.0, 1.0)                                                  \
    X(easeOut, "out", 0.0, 0.0, 0.2, 1.0)                                                \
    X(easeInOut, "in-out", 0.4, 0.0, 0.2, 1.0)

// Mobile-first min-width breakpoints, px. X(cppName, "registry-key", px)
#define LOOM_BREAKPOINT_TOKENS(X)                                                        \
    X(sm, "sm", 640)                                                                     \
    X(md, "md", 768)                                                                     \
    X(lg, "lg", 1024)                                                                    \
    X(xl, "xl", 1280)                                                                    \
    X(x2l, "2xl", 1536)
