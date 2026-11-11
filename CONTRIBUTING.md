# For Contributors

**This document is for developers who want to build, modify or extend the plugin.**

The project lives at <https://github.com/michal-ruzicka/TCNewPlus>. Bug
reports and patches are welcome via the project's
[issue tracker](https://github.com/michal-ruzicka/TCNewPlus/issues) and
[pull requests](https://github.com/michal-ruzicka/TCNewPlus/pulls).

## Prerequisites

- [**Visual Studio 2026 Build Tools**](https://visualstudio.microsoft.com/)
  with the components listed in [`.vsconfig`](.vsconfig): the MSVC toolset,
  the Windows SDK and CMake. For a build that matches the CI build byte for
  byte, use the pinned release, 18.12.0 (see
  [Reproducible Builds](#reproducible-builds)). Installing with
  `vs_buildtools.exe --config .vsconfig` (as
  `setup-build-environment-example.cmd` and the CI workflow do) selects
  exactly those. The full Visual Studio IDE works too, but is not required.

No .NET SDK, no extra runtime — the project is pure C++ / Win32,
linked against the static MSVC runtime.

## Repo Layout

| Path | Description |
|---|---|
| `.github/` | GitHub Actions CI workflow (`workflows/build.yml`) |
| `build\` | CMake out-of-source build trees (gitignored) |
| `dist\` | Released ZIP bundles (gitignored) |
| `src\` | C++ sources, INI/INF/manifest, `CMakeLists.txt` |
| `templates\` | Sample template set bundled in every release ZIP |
| `.gitattributes` | Line-ending normalization rules |
| `.gitignore` | Excludes `build\` and `dist\` from version control |
| `.vsconfig` | Visual Studio component selection; pins the MSVC toolset and Windows SDK versions for the CI workflow and `setup-build-environment-example.cmd` |
| `build.cmd` | Top-level build driver — the only file outside `src\` and `templates\` that the build touches |
| `build-fingerprint.ps1` | Describes one build for comparing two of them (see [When the hashes differ](#when-the-hashes-differ)); called by `build.cmd` |
| `CHANGELOG.md` | Release notes in Keep a Changelog format; bundled in every release ZIP |
| `CLAUDE.md` | Project notes for Claude Code; bundled in every release ZIP |
| `CONTRIBUTING.md` | Developer documentation (this file); bundled in every release ZIP |
| `LICENSE.md` | Apache License 2.0; bundled in every release ZIP |
| `README.md` | End-user documentation; bundled in every release ZIP |
| `setup-build-environment-example.cmd` | One-shot script: downloads sources from GitHub, installs Visual Studio Build Tools (CMake ships with them via the `VC.CMake.Project` component in `.vsconfig`), and runs `build.cmd`; designed for VMs and Windows Sandbox |

## Build

From any command prompt where `vswhere.exe` can be found:

```cmd
build.cmd
```

The script locates Visual Studio Build Tools via `vswhere` and activates the
compiler environment itself — no Developer Command Prompt is needed. It drives
CMake (NMake Makefiles generator) for both bitnesses (x86 and x64) using the
sources under `src\`, **runs the test executable for each bitness** (a failing
test fails the build), and packages the artifacts into
`dist\TCNewPlus.v<version>.zip` — the same auto-install bundle described
in *Installation*. The version is read from `src\pluginst.inf` as the single
source of truth; CMake generates the `version.h` behind the DLL's version
resource from it.

To set up a build environment from scratch on a fresh Windows installation
(VM, [Windows Sandbox](https://learn.microsoft.com/windows/security/application-security/application-isolation/windows-sandbox/),
or similar), `setup-build-environment-example.cmd` in the repo root downloads
the sources, installs the required toolchain, and runs `build.cmd`
automatically. Open it in a text editor before running — it is intentionally
readable so you can see and trust every step.

The same `build.cmd` is what the GitHub Actions CI workflow runs on every
push and pull request. The workflow uploads the resulting ZIP as a
downloadable artifact but does **not** publish GitHub Releases — that step
is done locally after GPG-signing the artifact (see *Release process*
below).

All actions in the workflow are pinned to a full commit SHA (required by
the repository's *Require actions to be pinned to a full-length commit SHA*
policy). Dependabot is configured to open monthly PRs that bump those SHAs
when upstream actions release new versions — do not update the SHAs by
hand.

### Tests

`src\TCNewPlusTest.cpp` builds into `TCNewPlusTest.exe` (per bitness, not
shipped in the ZIP) and contains two layers:

1. **Unit tests** of the naming rules in `NameSubstitution.cpp` — prefix
   stripping, every date/time token, `$PARENT_FOLDER_NAME`, environment
   variables, invalid-character sanitisation — against a fixed time
   snapshot.
2. **End-to-end tests** that load the built plugin DLL with `LoadLibrary`
   and drive it through the WFX API exactly like Total Commander does:
   `FsInitW` → `FsFindFirstW`/`FsFindNextW`/`FsFindClose` → `FsGetFileW`,
   against a temporary template tree. Covers listing, prefix/variable
   substitution, display-name collisions, copying, overwrite handling, the
   read-only refusal of move operations, custom-icon extraction, in-content
   variable expansion (`[FileContents] ReplaceContentVariables`), and the per-user INI
   precedence over the plugin-dir INI.

Run manually after a build:

```cmd
build\x64\TCNewPlusTest.exe build\x64\TCNewPlus.wfx64
build\x86\TCNewPlusTest.exe build\x86\TCNewPlus.wfx
```

### Reproducible Builds

Every build from the same source commit produces byte-identical binaries,
regardless of when or where the build runs. Users can therefore independently
verify that a published ZIP was built from the published source code without
having to trust the build infrastructure.

#### What makes the build deterministic

Four independent sources of non-determinism are eliminated:

| Source | Mechanism |
|---|---|
| PE `TimeDateStamp` field in `.dll` / `.exe` | Linker flag `/BREPRO` zeros it |
| Non-deterministic hash in MSVC `.obj` files | Compiler flags `/Brepro /experimental:deterministic` |
| Absolute source paths in debug info | Compiler flag `/pathmap:<src-dir>=./` strips the machine-local prefix |
| File modification times in ZIP entries | Normalized to `release-date=` read from `src\pluginst.inf` |

Four additional parameters are pinned so that "same source" also means "same
compiler output":

| Parameter | Pinned to | Files that must match |
|---|---|---|
| Visual Studio release | `18.12.0` | `build.cmd` (`EXPECTED_VS`), `.github\workflows\build.yml` (`VS_BUILDTOOLS_VERSION`, `_URL`, `_SHA256`), `setup-build-environment-example.cmd` (`VS_BUILDTOOLS_URL`, `_SHA256`) |
| Exact `cl.exe` version | `19.51.36263` | `build.cmd` (`EXPECTED_CL`) |
| MSVC toolset (major.minor) | `14.51` | `build.cmd` (`CMAKE_T`), `.vsconfig` (also used by the CI workflow) |
| Windows SDK | `10.0.26100.0` | `build.cmd` (`CMAKE_SDK`), `.vsconfig` (also used by the CI workflow) |

The Visual Studio release matters on its own: an update can service the
toolset libraries (`libcmt.lib`, `libcpmt.lib`, `libvcruntime.lib`) and the
Windows SDK (`libucrt.lib`, headers) without changing the `cl.exe` version or
the folder names. The CI workflow therefore installs Build Tools with the
fixed-version bootstrapper of the pinned release (its SHA-256 is checked),
not the latest one. Locally, keep Visual Studio at that release, or install
the pinned Build Tools next to it with the same bootstrapper and point
`build.cmd` at them:

```
set TCNP_VSINSTALL=C:\BuildTools
build.cmd
```

Without `TCNP_VSINSTALL`, `build.cmd` uses the newest Visual Studio that
`vswhere` reports. It warns when its version (`VSCMD_VER`, shown in the
Developer Command Prompt banner) is not `EXPECTED_VS`.

`build.cmd` checks `cl.exe` against `EXPECTED_CL` and prints a warning if
they differ — the build continues regardless. The check exists so that when
a local and a CI build produce different hashes, the first thing to look at
in both logs is the `cl.exe` version line. Two builds from the same toolset
major.minor but different `cl.exe` patch versions will produce different
binaries; this happens when VS patches land at different times on local
machines and CI runner images. Update `EXPECTED_CL` (with a matching VS
update on both sides) when you want to re-establish bit-for-bit parity.

The toolset is selected by passing `-vcvars_ver=%CMAKE_T%` to `VsDevCmd.bat`
before invoking CMake. The SDK is enforced by CMake:

```
cmake ... -DCMAKE_SYSTEM_VERSION=10.0.26100.0
```

If the SDK is missing, CMake fails with a descriptive error rather than
silently falling back to a different version.

#### Verifying a build

The independent reference point is the SHA-256 of the ZIP itself, not files
inside it — `sha256sums.sha256` is bundled inside the same archive, so it is only
as trustworthy as the ZIP it came from.

To verify a release:

1. Run `build.cmd` locally on the release commit. The script prints the SHA-256
   of the produced ZIP at the end of its output.
2. Compare that hash against the SHA-256 printed in the *Build and test (x86 + x64)*
   step of the [GitHub Actions run](https://github.com/michal-ruzicka/TCNewPlus/actions)
   for the same commit.
3. Optionally, compare against the published release ZIP's SHA-256:
   ```
   certutil -hashfile dist\TCNewPlus.vX.Y.Z.zip SHA256
   ```

If all three match, the released ZIP is identical to what the source builds.

`sha256sums.sha256` inside the ZIP is useful as a convenience check after
extraction — to confirm that an individual file was not corrupted during
download or extraction — but it is not an independent trust anchor.

#### When the hashes differ

After each bitness, `build.cmd` runs `build-fingerprint.ps1`, which writes
`build\fingerprint-x64.txt` and `build\fingerprint-x86.txt` and prints them
into the log; the CI workflow also uploads them as the
`TCNewPlus-fingerprints` artifact. A fingerprint lists everything that
decides the bytes of the plugin DLL, with machine-specific folders replaced by
placeholders (`<VS>`, `<VCTools>`, `<WindowsSdkDir>`, `<src>`, `<build>`), so
two builds of the same commit should have identical fingerprints apart from
the lines marked *informational*. Compare a local one with the one from CI:

```
fc build\fingerprint-x64.txt %USERPROFILE%\Downloads\TCNewPlus-fingerprints\fingerprint-x64.txt
```

Read the differences from the top; the first section that differs is usually
the cause:

| Section | What a difference means |
|---|---|
| `[toolchain]` | Another Visual Studio, MSVC toolset or Windows SDK build. The same version number with another hash of `cl.exe`, `c1xx.dll`, `c2.dll` or `link.exe` means a serviced toolset. Install the same Visual Studio version on both sides; a Visual Studio update can update the Windows SDK too. |
| `[libraries]` | The static C/C++ runtime (`libcmt.lib`, `libcpmt.lib` and `libvcruntime.lib` from the toolset, `libucrt.lib` from the Windows SDK) or an import library differs, and their code is linked in. `libucrt.lib` changes with the SDK revision (10.0.26100.*x*), which the SDK folder name `10.0.26100.0` does not show; the SDK is installed machine-wide, so another product (or a newer Visual Studio instance) can update it under a pinned Visual Studio. |
| `[options]` | CMake generated other compiler, resource compiler or linker options: another CMake version, or a changed `CMakeLists.txt`. |
| `[objects]` | The toolchain and options are the same but an object file is not: the headers differ (another SDK revision) or the sources do. |
| `[binary]` | Only the linked result differs: the linker or a library. The section hashes narrow it down (`.text` code, `.rdata` constants and the import table, `.rsrc` the resources). The `rich:` lines (the Rich header) list, per tool build, how many linked-in objects it produced: a library built by another compiler shows up as another `build` number. |

#### Upgrading the pinned toolchain

When you adopt a new MSVC toolset or Windows SDK version, update **both
locations** listed below to keep them in sync. A mismatch causes either a
build failure (CMake cannot find the requested toolset) or silent divergence
between local and CI builds.

##### Visual Studio release

Take the new release's *Build Tools* fixed-version bootstrapper from the
[Visual Studio 2026 release history](https://learn.microsoft.com/visualstudio/releases/2026/release-history).
The long hexadecimal folder in its URL is the SHA-256 of the file; check it
after downloading (`certutil -hashfile vs_BuildTools.exe SHA256`), and check
that the file's *Product version* (Properties, Details) is the release.
Update, in one commit:

1. **`build.cmd`** — `set EXPECTED_VS=18.XX.Y`
2. **`.github\workflows\build.yml`** — `VS_BUILDTOOLS_VERSION`,
   `VS_BUILDTOOLS_URL`, `VS_BUILDTOOLS_SHA256`
3. **`setup-build-environment-example.cmd`** — `VS_BUILDTOOLS_URL`,
   `VS_BUILDTOOLS_SHA256`

Then update your local Visual Studio to the same release. A new release can
bring a new `cl.exe`, so check the toolset below as well.

##### MSVC toolset

Identify the installed toolset version:

```
cl.exe /Bv
```

The path in the output contains the toolset directory, e.g.:
`...\VC\Tools\MSVC\`**`14.51`**`.36231\bin\...`

Use the first two numbers (`14.XX`) as the toolset token and the full
three-part `cl.exe` version (e.g. `19.51.36244`) as the exact compiler version.
Update:

1. **`build.cmd`** — the three pinned variables at the top of the file:
   ```
   set EXPECTED_CL=19.51.XXXXX
   set CMAKE_T=14.XX
   set CMAKE_SDK=10.0.XXXXX.X
   ```
2. **`.vsconfig`** — the `VC.Tools` component entry (this also covers the
   CI workflow, which installs the toolchain from `.vsconfig`):
   ```json
   "Microsoft.VisualStudio.Component.VC.14.XX.x86.x64"
   ```

##### Windows SDK

The SDK version is a five-digit build number, visible in VS Installer under
the installed *Windows 11 SDK* component (e.g. `10.0.`**`26100`**`.0`).
Update:

1. **`build.cmd`** — the `CMAKE_SDK` variable at the top of the file, which
   both `cmake` configure lines pass on:
   ```
   set CMAKE_SDK=10.0.NNNNN.0
   ```
2. **`.vsconfig`** — the SDK component entry (this also covers the
   CI workflow, which installs the toolchain from `.vsconfig`):
   ```json
   "Microsoft.VisualStudio.Component.Windows11SDK.NNNNN"
   ```

##### Visual Studio major version

Each major Visual Studio release increments the product version number in the
installation path (`\17\` for VS 2022, `\18\` for VS 2026, …). The build
uses the NMake Makefiles generator driven by `VsDevCmd.bat`, so no CMake
generator flag needs updating. What does change:

1. **`.vsconfig`** — the toolset and SDK component IDs change with each major
   VS release; update those using the guidance above.
2. **The pinned release** — a new major version has its own release history
   page; pin one of its releases as described in *Visual Studio release*
   above. `build.cmd` finds the installation through `vswhere`, which does not
   depend on the major version.

CMake comes with the Build Tools (`VC.CMake.Project` in `.vsconfig`), so a
new Visual Studio version also brings a matching CMake.

##### After any toolchain change

Delete the stale CMake build trees so CMake picks up the new selection on the
next run:

```
rmdir /S /Q build\x64 build\x86
build.cmd
```

## Architecture

A single artifact: the plugin DLL (`TCNewPlus.wfx` 32-bit /
`TCNewPlus.wfx64` 64-bit), a Total Commander **File System plugin** (WFX).
No helper processes, no COM — plain Win32 file APIs.

```
Total Commander process
└── TCNewPlus.wfx[64]
    ├── FsFindFirstW / FsFindNextW   list the real templates folder,
    │                                apply the New+ naming rules to every
    │                                entry (NameSubstitution.cpp), record
    │                                displayed-name -> real-path mappings
    ├── FsGetFileW                   stream the real template file into the
    │                                target TC created under the displayed
    │                                name; stamp it with the current time
    └── FsExecuteFileW               "open" -> FS_EXEC_YOURSELF (TC downloads
                                     to temp), "properties" -> info box with
                                     the real template path
```

The design hinges on one observation: **a WFX plugin controls the names TC
displays, and the displayed name is the name TC uses when copying out.**
So the New+ substitution does not happen "on creation" — it happens at
listing time, and `FsGetFileW` only has to map the displayed name back to
the template file, which is what the session-wide `g_virtualToReal` map is
for. The map only grows during a TC session, so a copy started after the
clock ticked past the listing minute still resolves to the right template.
If TC asks for a path that was never listed in this session (e.g. a queued
background transfer), `ResolveRealPath` re-enumerates the ancestor
directories on demand.

**Read-only by omission:** a WFX plugin declares which operations it
supports by which functions it exports. `FsPutFile`, `FsMkDir`,
`FsDeleteFile`, `FsRemoveDir` and `FsRenMovFile` are deliberately not
exported, so Total Commander itself disables creating, deleting and
renaming inside the plugin. The one write-ish path that remains reachable —
`F6` move, which arrives as `FsGetFileW` with `FS_COPYFLAGS_MOVE` — is
refused with `FS_FILE_NOTSUPPORTED` because completing it would require
deleting the template.

The naming rules live in `NameSubstitution.cpp` with no plugin or
filesystem dependencies, and are linked into both the plugin DLL and
`TCNewPlusTest.exe` — that is what makes them unit-testable without TC.

### Known Limitations

- **Panel sort order is TC's, not the prefix order** (when
  `HideStartingDigits=1`). See *Differences from PowerToys New+* in
  [README.md](README.md#differences-from-powertoys-new).
- **Display names are snapshots.** A name listed at 14:59 and copied at
  15:01 keeps the 14:59 substitution — by design, since the displayed
  name must equal the created name.
- **Icons** are the real template files' shell icons
  (`FsExtractCustomIconW` + `SHGetFileInfo`); the plugin's own Network
  Neighborhood icon is the first icon resource in the DLL (`TCNewPlus.rc`
  / `TCNewPlus.ico`).

## Diagnostics

User-facing troubleshooting lives in [README.md](README.md#troubleshooting);
the developer-level notes live here.

**Inspect the mapping and the effective config.** `Alt+Enter` (Properties)
on any virtual entry shows the virtual name, the real template path it maps
to, **which INI file is actually in effect** (whether it exists), and the
parsed value of every setting — the fastest way to see why a particular
entry looks the way it does, and in particular to catch a per-user
`%APPDATA%\GHISLER\TCNewPlus.ini` silently shadowing the plugin-dir INI so an
edit to the wrong file appears to do nothing.

**Config not taking effect?** `TCNewPlus.ini` is looked up first in
the folder of Total Commander's `wincmd.ini`, then at
`%APPDATA%\GHISLER\TCNewPlus.ini` (per-user override) and finally next to the
plugin DLL (check TC's *Configuration → Options → Plugins → File system
plugins → Configure* for the installed path); the first file that exists
wins entirely — keys are not merged. The chosen file is re-read on every
root listing, so `Ctrl+R` in the plugin panel applies edits. A stale
per-user INI silently shadowing one next to the plugin is the usual culprit.

**Reproduce outside TC.** `TCNewPlusTest.exe <path-to-wfx>` exercises the
full listing/copy pipeline against a temporary template tree without
involving Total Commander; extend it with a failing case first when fixing
a bug.

## Signing Policy

All commits merged into `main` must be signed. Tags `v*` must also be
signed. The repository's branch and tag protection rules enforce this
server-side — unsigned commits and tags are rejected on push.

Two distinct signing mechanisms are in use:

| What | Method | Key type |
|------|--------|----------|
| Git commits and annotated tags | SSH key signing | Your SSH key |
| Release ZIP files | GPG detached signature | Separate GPG key |

### Setting up SSH commit signing

You can reuse the same SSH key you already use to authenticate to GitHub.

**1. Register the key as a signing key on GitHub.**

Go to **Settings → SSH and GPG keys → New signing key** and paste your
public key. This is a separate entry from the authentication key, but
both entries can use the same key material.

**2. Configure Git for this repository.**

```
git config gpg.format ssh
git config user.signingkey "~/.ssh/id_ed25519.pub"
git config commit.gpgsign true
```

Replace `id_ed25519.pub` with your actual public key filename if
different. Omitting `--global` scopes these settings to this repository
only.

**3. Verify.**

```
git commit --allow-empty -m "test signing"
git log -1 --show-signature
```

Git should report `Good "git" signature` with your key fingerprint.

The same `gpg.format = ssh` setting is picked up by `git tag -s`, so
the release tagging step in *Release Process* below also uses your SSH
key automatically — no separate GPG key is needed for tags.

### GPG signing of release ZIPs

The distributable ZIP is signed with a GPG key (not the SSH key) to
produce a detached `.asc` signature that end users can verify without
having to trust GitHub's infrastructure. The signing key fingerprint is
`489C 5EC8 0FD6 2BE8 9E59  B4F7 19C1 3E8C E0F5 DB61` (available on
<https://keys.openpgp.org/>). The exact signing steps are in the
*Release Process* section below.

## Release Process

Releases are built and signed locally; no private key ever leaves the
developer's machine.

1. On a feature branch, set `version=X.Y.Z` (dropping the `-devel` suffix)
   and `release-date=` in `src\pluginst.inf`, and turn the `[Unreleased]`
   section of `CHANGELOG.md` into `## [vX.Y.Z] – YYYY-MM-DD` (and update the
   link references at its end), in a final
   "Bump plugin version to vX.Y.Z" commit. Development after a release
   continues as `X.Y.Z-devel` of the next version.
2. Open a PR, get it reviewed, and merge into `main`.
3. On `main`, tag and push:
   ```
   git fetch && git checkout main && git pull
   git tag -s vX.Y.Z -m "Release X.Y.Z"
   git push origin vX.Y.Z
   ```
4. Run `build.cmd` locally to produce `dist\TCNewPlus.vX.Y.Z.zip`.
5. GPG-sign the ZIP:
   ```
   gpg --detach-sign --armor dist\TCNewPlus.vX.Y.Z.zip
   ```
   This creates `dist\TCNewPlus.vX.Y.Z.zip.asc`.
6. On the GitHub repository page, go to **Releases → Draft a new release**,
   select the `vX.Y.Z` tag, paste the CHANGELOG entry as the description,
   and attach both files (`.zip` and `.zip.asc`).

The CI workflow also produces the ZIP as a downloadable Actions artifact,
but that copy is unsigned and is intended for testing PRs only.

## License

This project is licensed under the Apache License 2.0 — see
[LICENSE.md](LICENSE.md) for the full text. Source files carry SPDX
identifiers (`SPDX-License-Identifier: Apache-2.0`). Release notes are in
[CHANGELOG.md](CHANGELOG.md).
