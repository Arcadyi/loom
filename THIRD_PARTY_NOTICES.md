# Third-party notices

loom release archives may include dynamically linked Qt 6 Core and Qt 6 Network
runtime libraries. Qt is copyright The Qt Company Ltd. and other contributors
and is not licensed under loom's Apache-2.0 license.

The exact terms available to a distributor depend on how their Qt build was
obtained. Open-source Qt builds are commonly offered under LGPL-3.0-only,
GPL-2.0-only, or GPL-3.0-only terms, while commercial Qt builds use the
distributor's commercial agreement. loom does not change or replace those terms.

When the source Qt installation provides a `LICENSES` directory, loom's CMake
packaging installs it under `share/doc/loom/third-party/qt`. Distribution
maintainers remain responsible for supplying the corresponding source offer,
replacement mechanism, notices, and any other obligations required by their
chosen Qt license.

Other system libraries are not copied into the loom Linux archive. They are
resolved from the target operating system.
