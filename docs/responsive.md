# Responsive breakpoints

Tailwind's mobile-first model: an unprefixed utility always applies; a
prefixed one applies at that window width **and up**.

| Prefix | Min window width |
| --- | --- |
| `sm:` | 640 px |
| `md:` | 768 px |
| `lg:` | 1024 px |
| `xl:` | 1280 px |

```qml
Rectangle {
    // 64px wide by default, 160 from sm up, 256 from lg up.
    Lo.style: "w-16 sm:w-40 lg:w-64 bg-surface md:shadow-md"
}
```

The driver is the width of the **window the item lives in** (tracked through
reparenting and window changes), not the screen or the parent item. An item
outside any window styles at the base tier. There is deliberately no global
"current breakpoint" property: windows differ, so responsiveness is per-item.

Thresholds are config-overridable ([configuration.md](configuration.md)); the
values are exposed as `Loom.breakpoint.sm…xl` for custom layout logic.

For sizes that should track the *parent* rather than the window, use
`w-full` / `h-full`.
