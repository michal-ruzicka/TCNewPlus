# TCNewPlus — project notes for Claude

## What this is

A Total Commander **File System plugin** (`.wfx` / `.wfx64`) that brings the
[PowerToys New+](https://learn.microsoft.com/windows/powertoys/newplus)
template workflow to Total Commander. It exposes a read-only virtual file
system (`\\\TCNewPlus`) listing the user's template folder with the New+
naming rules already applied; copying an entry out with `F5` creates a new
file/folder under the substituted name.

## The core design idea

A WFX plugin has no concept of "create a new file from a template" — it only
has *listing* and *copy out*. The whole plugin rests on one observation:

> **The name a WFX plugin returns from `FsFindFirstW`/`FsFindNextW` is the
> name Total Commander uses for the target file on `F5`.**

So the New+ substitution (strip `1. ` sorting prefixes, replace `$YYYY`,
`%USERNAME%`, …) happens at **listing time**, and `FsGetFileW` only maps the
displayed name back to the real template path and streams the bytes. The
map (`g_virtualToReal`) is session-wide and only ever grows, so a copy that
starts after the clock ticked past the listing minute still resolves; if TC
asks for a never-listed path, `ResolveRealPath` re-enumerates ancestors on
demand.

Consequences worth remembering before "fixing" them:

- **Extensions are never hidden** (unlike the New+ menu) — the displayed
  name is the created filename; hiding `.docx` would create a broken file.
- **Panel sort order is TC's.** We cannot impose the New+ prefix order on
  TC's panel; with `HideStartingDigits=1` the prefixes are gone before TC
  sorts. This is documented as a difference, not a bug.
- **Displayed names are snapshots** of the listing moment. `Ctrl+R`
  re-substitutes. Do not try to re-substitute inside `FsGetFileW` — the
  target name was already fixed by TC.

## Repo layout

Markdown documentation and the build driver live at the repo root; all
build inputs (C++ sources, INI/INF/manifest, CMakeLists) live under `src\`;
the sample template set lives under `templates\`. `build.cmd` reads from
`src\` and `templates\` and writes to `build\` and `dist\` (both ignored).

```
README.md             - end-user docs (auto-installed into the ZIP)
CONTRIBUTING.md       - developer docs (auto-installed into the ZIP)
CHANGELOG.md          - Keep-a-Changelog formatted release notes
LICENSE.md            - Apache License 2.0 + copyright notice
CLAUDE.md             - this file
build.cmd             - convenience driver: x86 + x64, runs the tests,
                        stages everything, packages dist/TCNewPlus.v<version>.zip
templates/            - sample template set (shipped as Templates\ in the ZIP)
src/
  CMakeLists.txt      - builds TCNewPlus.wfx / .wfx64 and TCNewPlusTest.exe
  TCNewPlus.cpp       - plugin DLL (WFX entry points, virtual FS, copy-out)
  NameSubstitution.cpp/.h - the New+ naming rules; pure string code, no
                        filesystem/plugin dependencies (shared with the test)
  TCNewPlusTest.cpp   - unit tests + end-to-end tests through the WFX API
  TCNewPlus.def       - DLL exports (the export list IS the feature set, see below)
  TCNewPlus.rc/.ico   - Network Neighborhood plugin icon (first icon resource)
  fsplugin.h          - WFX plugin API (subset used here)
  app.manifest        - test EXE manifest (long paths)
  pluginst.inf        - TC auto-installer metadata; canonical version source
  TCNewPlus.ini       - sample config: [Templates] TemplatesPath;
                        [FileNames] ReplaceFilenameVariables, HideStartingDigits;
                        [FileContents] ReplaceContentVariables
build/                - CMake out-of-source build trees (gitignored)
dist/                 - released TCNewPlus.v<version>.zip files (gitignored)
```

## Things to keep in mind when editing

- **The export list defines capabilities.** A WFX plugin is read-only
  *because* `FsPutFile` / `FsMkDir` / `FsDeleteFile` / `FsRemoveDir` /
  `FsRenMovFile` are not exported — TC checks for the exports and disables
  the corresponding operations. Adding any of these exports silently makes
  the panel writable. `FS_COPYFLAGS_MOVE` in `FsGetFileW` is the one
  write-path that arrives anyway (F6); it is refused with
  `FS_FILE_NOTSUPPORTED`.
- **Both bitnesses must build and pass tests.** 32-bit TC users exist.
  `build.cmd` runs `TCNewPlusTest.exe` for x64 and x86 (the latter under
  WOW64).
- **ANSI wrappers stay.** Old 32-bit TC versions call only the ANSI entry
  points; every `...W` function has an `...A` twin that converts via
  `CP_ACP` and delegates.
- **Static CRT** (`MSVC_RUNTIME_LIBRARY = MultiThreaded`) so the DLL has no
  `vcruntime*.dll` dependency — TC plugins are expected to be drop-in.
- **Keep `NameSubstitution.cpp` pure.** No plugin globals, no file I/O —
  it is linked into the test EXE and unit-tested with a fixed
  `SYSTEMTIME`. Date *names* ($MMMM, $DDD, …) go through
  `GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, …)` and are locale-dependent
  by design (so are New+'s).
- **Token matching is longest-first and case-sensitive** ($DDDD before
  $DDD before $DD before $D). `$PARENT_FOLDER_NAME` must be checked before
  the date tokens. Environment variables are case-INsensitive; an
  undefined `%VAR%` stays verbatim (ExpandEnvironmentStrings semantics).
- **Name substitution sanitises, content substitution does not.**
  `ReplaceTemplateVariables(..., sanitizeInvalidChars)` replaces filename-
  invalid chars (`\/:*?"<>|`) with spaces for names (New+ behaviour) but
  must NOT for file contents (a `%USERPROFILE%` path keeps its `:`/`\`).
  `MakeDisplayName` passes true; the in-content path (`CopyExpanded`)
  passes false.
- **In-content expansion is opt-in per extension**
  (`[FileContents] ReplaceContentVariables`, empty by default — a
  comma-separated extension list, not a boolean). `CopyExpanded` in
  `FsGetFileW` reads the file as UTF-8
  (BOM preserved), expands with copy-time `GetLocalTime`, and writes UTF-8;
  a file that fails `MB_ERR_INVALID_CHARS` decoding falls back to the raw
  byte copy (`FS_RAW_FALLBACK`) so binaries are never corrupted. This is a
  TCNewPlus extension — New+ only substitutes in names.
- **Empty-directory contract:** `FsFindFirstW` must return
  `INVALID_HANDLE_VALUE` with `SetLastError(ERROR_NO_MORE_FILES)` for an
  empty dir, and `ERROR_PATH_NOT_FOUND` for a missing one — TC
  distinguishes the two.
- **Source files are UTF-8.** CMakeLists passes `/utf-8` to MSVC so
  literals containing em-dashes / accented characters don't depend on the
  system ANSI code page.
- **Config is re-read on every root listing** (`LoadConfig()` in
  `FsFindFirstW`) — cheap by design; don't cache it globally or INI edits
  stop applying on `Ctrl+R`. Lookup order mirrors TCOfficeView:
  `%APPDATA%\GHISLER\TCNewPlus.ini` (per-user) wins over
  `<plugin dir>\TCNewPlus.ini`; the first existing file is used in full, no
  per-key merge. The default `TemplatesPath` stays relative to the DLL
  directory regardless of which INI wins (that's where the samples ship).
- **The templates-folder link** (`[Templates] ShowLinkToTemplatesDir`) is an
  extra root entry added in `EnumerateVirtualDir` (root only), tracked in
  `g_linkPaths`. It is a *file* entry (not a directory) so Enter triggers
  `FsExecuteFileW`, which returns `FS_EXEC_SYMLINK` with the real templates
  path — TC then navigates there. `FsLinksToLocalFiles` is deliberately NOT
  used: per the SDK it would turn this into a "temporary panel" plugin and
  change the whole model. The label runs through `MakeDisplayName` and is
  validated with `IsValidEntryName`. The ANSI `FsExecuteFile` wrapper copies
  the redirect path back into its caller buffer (needs `MAX_PATH`).
- **The INI is parsed by hand as UTF-8, not via `GetPrivateProfileString`.**
  `ReadTextFileToWide` + `IniLookup` honour a UTF-8/UTF-16 BOM and otherwise
  assume UTF-8. Do NOT switch back to `GetPrivateProfileString*`: it treats a
  BOM-less file as ANSI (system code page) and mangles non-ASCII values like a
  `TemplatesPath` containing `Osobní`. `TemplatesPath` has trailing separators
  stripped (`StripTrailingSeparators`).

## Build prerequisites

- Visual Studio 2026 Build Tools, workload: *Desktop development with C++*.
- CMake 3.20+.
- No .NET SDK. No other toolchain.

Reproducible-build pins (EXPECTED_CL / CMAKE_T / CMAKE_SDK in `build.cmd`,
components in `.vsconfig`) follow the TCOfficeView scheme — see
[CONTRIBUTING.md](CONTRIBUTING.md#reproducible-builds) before touching them.

## Icons

Two independent mechanisms, both required (per the WFX SDK "How to define an
icon" page):

- **Plugin icon in Network Neighborhood** — TC loads the *first* icon
  resource (by index) from the plugin DLL. `TCNewPlus.rc` embeds
  `TCNewPlus.ico` (a New+ document-with-plus glyph; 16/24/32/48/256 px,
  PNG-compressed entries) under resource id `1`. No code path produces it.
- **File/folder icons inside the plugin** — `FsExtractCustomIconW` maps the
  virtual entry to its real template path and returns that file's shell
  icon via `SHGetFileInfoW` (so a `.docx` template shows the Word icon, a
  folder template a folder). Returns `FS_ICON_EXTRACTED_DESTROY` (TC calls
  `DestroyIcon`); root `\` and unresolved paths return `FS_ICON_USEDEFAULT`.

The `.ico` is a committed binary asset, not regenerated at build time, so it
does not affect reproducibility. It was generated once with GDI+ (the script
is not kept in the repo).

## Future work (not blocking)

- `FsContentGetValue` — expose the real template path as a content column.
- Optional `$DATE` / `$TIME` aggregate tokens if New+ ever adds them.
- Release workflow automation (GitHub Actions triggered by `v*` tags).
