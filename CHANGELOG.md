# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

First working release.

### Added

- **Total Commander File System plugin (`TCNewPlus.wfx` / `TCNewPlus.wfx64`)**
  that exposes a [PowerToys New+](https://learn.microsoft.com/windows/powertoys/newplus)
  style template folder as a read-only virtual file system under
  `\\\TCNewPlus`. Copying a template out with `F5` creates a new file or
  folder under the substituted name — the Total Commander equivalent of
  the New+ Explorer context menu.
- **New+ naming rules applied at listing time:**
  - sorting prefixes (leading digits, spaces and dots) hidden from the
    displayed and created names (`HideStartingDigits`, default on);
  - date/time variables (`$YYYY`, `$YY`, `$Y`, `$MMMM`, `$MMM`, `$MM`,
    `$M`, `$DDDD`, `$DDD`, `$DD`, `$D`, `$hh`, `$h`, `$mm`, `$m`, `$ss`,
    `$s`, `$fff`, `$ff`, `$f`), `$PARENT_FOLDER_NAME` in template
    subfolders, and `%ENVIRONMENT_VARIABLE%` expansion, with
    filesystem-invalid characters in substituted values replaced by
    spaces (`ReplaceVariables`, default on);
  - colliding display names disambiguated with an ` (2)` suffix.
- **Read-only by design** — no write entry points are exported, so TC
  disables delete/rename/upload inside the virtual file system; `F6`
  (move) is refused explicitly.
- **Optional variable expansion inside file contents** (a TCNewPlus
  extension beyond New+, which only substitutes in names). Off by default
  and opt-in per file extension via `ExpandInFileExtensions` (e.g.
  `.md,.txt`), so binary files are never touched. Content uses the same
  variables as names but without filename-character sanitisation (a
  `%USERPROFILE%` path keeps its `:`/`\`); listed files are treated as
  UTF-8 (BOM preserved), and anything that is not valid UTF-8 is copied
  verbatim.
- **Icons.** The plugin shows an icon next to its name in Network
  Neighborhood (embedded icon resource), and every entry under
  `\\\TCNewPlus` shows the real template file's shell icon via
  `FsExtractCustomIconW` (a `.docx` template shows the Word icon, a folder
  template a folder, …).
- **`Alt+Enter` (Properties)** on a virtual entry shows the real template
  path on disk and the active templates folder.
- **Configuration via `TCNewPlus.ini`** (`TemplatesPath`,
  `ReplaceVariables`, `HideStartingDigits`), re-read on every root listing.
  Looked up first at `%APPDATA%\GHISLER\TCNewPlus.ini` (per-user override)
  and then next to the plugin DLL — the first file that exists wins
  entirely, mirroring TCOfficeView. Defaults to the bundled sample
  template set in the `Templates` folder next to the DLL; can be pointed
  at the PowerToys New+ templates folder to share one collection.
- **Sample template set** bundled in the release ZIP.
- **Test suite (`TCNewPlusTest.exe`)** — unit tests of the naming rules
  plus end-to-end tests driving the built DLL through the WFX API; run
  for both bitnesses by `build.cmd` and the CI workflow.
- **Reproducible build pipeline** mirroring TCOfficeView: pinned MSVC
  toolchain (`cl.exe 19.51.36246`, toolset `14.51`, SDK `10.0.26100.0`),
  `/Brepro` + `/BREPRO` + `/pathmap`, normalized timestamps and line
  endings in the ZIP, `sha256sums.sha256` manifest, GitHub Actions CI
  with SHA-pinned actions, and `setup-build-environment-example.cmd` for
  clean-room verification builds.
