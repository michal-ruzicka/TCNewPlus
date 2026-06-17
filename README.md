# TCNewPlus: PowerToys New+ Style File Templates for Total Commander

[![Build](https://github.com/michal-ruzicka/TCNewPlus/actions/workflows/build.yml/badge.svg)](https://github.com/michal-ruzicka/TCNewPlus/actions/workflows/build.yml)
[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-GitHub%20Sponsors-ea4aaa?style=flat&logo=githubsponsors&logoColor=white)](https://github.com/sponsors/michal-ruzicka)
[![Ko-fi](https://img.shields.io/badge/Tip-Ko--fi-FF5E5B?style=flat&logo=kofi&logoColor=white)](https://ko-fi.com/michal_ruzicka)
[![Revolut](https://img.shields.io/badge/Pay-Revolut-191C1F?style=flat&logo=revolut&logoColor=white)](https://revolut.me/ruzicka_michal)

**A [Total Commander](https://www.ghisler.com/) File System plugin (WFX)
that brings the [PowerToys New+](https://learn.microsoft.com/windows/powertoys/newplus)
workflow to Total Commander: create new files and folders from a personal
template collection, with date/time variables in the names substituted at
creation time.**

The plugin exposes a read-only virtual file system — `\\\TCNewPlus` — that
lists the contents of your templates folder. Every name is shown the way
New+ would create it: sorting prefixes (leading digits, spaces and dots)
are hidden, and variables like `$YYYY-$MM-$DD` or `%USERNAME%` are already
replaced with the current values. To create a new file or folder from a
template, simply **copy it out with `F5`** into the directory shown in the
other panel — the copy is created under the substituted name, with fresh
timestamps, exactly as if New+ had created it.

Anything works as a template: plain text files, Office documents, entire
project folder structures. Templates are ordinary files in an ordinary
folder — the same folder PowerToys New+ uses, if you want to share one
collection between Explorer's context menu and Total Commander.

> **Beyond New+:** as an optional extension that PowerToys New+ does not
> offer, TCNewPlus can also substitute variables *inside file contents*
> (not just in names) for chosen file extensions — see
> [Variable Expansion Inside File Contents](#variable-expansion-inside-file-contents).
> It is off by default, so the plugin stays a faithful New+ equivalent
> until you opt in.

**Project page:** <https://github.com/michal-ruzicka/TCNewPlus> — source
code, releases and issue tracker.

**Binary releases:** <https://github.com/michal-ruzicka/TCNewPlus/releases>

**Support:** If you find this plugin useful, consider supporting its development.

- <https://github.com/sponsors/michal-ruzicka> — GitHub Sponsors (GitHub account needed).

  [![GitHub Sponsors](https://img.shields.io/github/sponsors/michal-ruzicka)](https://github.com/sponsors/michal-ruzicka)

- <https://ko-fi.com/michal_ruzicka> — Buy me a Coffee with no specific account needed, card payment is possible.

  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/michal_ruzicka)

- <https://revolut.me/ruzicka_michal> — Donate me via Revolut, debet/credit card or Apple Pay.

  [![Revolut.Me](Revolut.Me.png)](https://revolut.me/ruzicka_michal)

---

## Installation

The recommended way is the **Total Commander auto-installer:**

1. **Download** `TCNewPlus.vX.Y.Z.zip` from the [**latest release**](https://github.com/michal-ruzicka/TCNewPlus/releases).
2. In Total Commander, **navigate to the ZIP** file and press **Enter**.
3. TC detects the bundled `pluginst.inf` and asks whether to install the
   plugin — **confirm**.
4. Open **Network Neighborhood** (`\\\` in the command line, or the `\`
   drive button → Network) and enter **TCNewPlus** to verify.

If you prefer to install manually, unzip the archive into a persistent
folder (for example `C:\Tools\TCNewPlus\`) and add `TCNewPlus.wfx`
(32-bit TC) or `TCNewPlus.wfx64` (64-bit TC) under **Configuration →
Options → Plugins → File system plugins → Configure → Add**.

### Verifying Releases

Each release ZIP is accompanied by a detached GPG signature file
(`TCNewPlus.vX.Y.Z.zip.asc`). Before installing, verify that the
archive has not been tampered with:

```
gpg --keyserver keys.openpgp.org --recv-keys 489C5EC80FD62BE89E59B4F719C13E8CE0F5DB61
gpg --verify TCNewPlus.vX.Y.Z.zip.asc TCNewPlus.vX.Y.Z.zip
```

GPG should report `Good signature from "Michal Růžička <ruzicka.mich@gmail.com>"`.
The full fingerprint of the signing key is `489C 5EC8 0FD6 2BE8 9E59  B4F7 19C1 3E8C E0F5 DB61`.

#### Reproducible Builds

Every release is built from a pinned, identical toolchain, so the same
source code always produces byte-for-byte identical binaries. This means
you are not just trusting the GPG signature — you can independently confirm
that the published binary matches the published source code by building it
yourself and comparing the SHA-256 of the ZIP against the released ZIP archive
or the [CI build log](https://github.com/michal-ruzicka/TCNewPlus/actions)
for the release commit.

Full details — how the build achieves this, and how to run a verification
build yourself — are in
[Reproducible Builds](CONTRIBUTING.md#reproducible-builds) in the developer
documentation.

## Usage

1. Open the plugin in one panel: type `cd \\\TCNewPlus` in the command
   line, or navigate to **Network Neighborhood → TCNewPlus**.
2. Open the destination folder (where the new file should be created) in
   the other panel.
3. Select a template and press **F5** (Copy), then confirm.

The new file or folder appears in the destination under the name shown in
the plugin panel — variables already substituted, sorting prefix already
removed — with fresh creation timestamps. Folder templates are copied
recursively, including variable substitution in the names of the files
inside them.

A small sample template set ships with the plugin so the panel is not
empty on first use; replace it with your own templates or point the
plugin at your PowerToys New+ folder (see
[Configuration](#configuration)).

Each entry shows the shell icon of the file type it will create (a `.docx`
template the Word icon, a folder template a folder icon, …), and the plugin
itself appears with its own icon in Network Neighborhood.

Pressing **Enter** or **F3** on a template opens a temporary copy for
inspection. **Alt+Enter** shows which real file on disk a virtual entry
comes from. The panel is **read-only**: creating, renaming and deleting
templates is intentionally not possible through the virtual file system —
manage the templates folder directly on disk instead (it is an ordinary
folder), then refresh the panel with **Ctrl+R**.

### Managing Templates

Templates live in an ordinary folder on disk (see
[Configuration](#configuration) for its location). Anything in that folder
becomes a template: files, folders (copied recursively), shortcuts. Two
naming conventions — the same ones PowerToys New+ uses — control how
templates appear:

- **Sorting prefix.** Leading digits, spaces and dots order the templates
  without appearing in the created files: `1. Letter.docx`,
  `2. Invoice.xlsx`, `10. Report.pptx`. The prefix is stripped from the
  displayed (and created) name.
- **Variables.** Anywhere in the name, the patterns below are replaced
  with current values at listing/creation time.

#### Date and Time Variables

Case-sensitive, identical to the patterns New+ and PowerRename use:

| Variable | Explanation |
| --- | --- |
| `$YYYY` | Year, full four digits |
| `$YY` | Year, last two digits (zero-padded) |
| `$Y` | Year, last digit |
| `$MMMM` | Name of the month |
| `$MMM` | Abbreviated name of the month |
| `$MM` | Month, zero-padded |
| `$M` | Month, no padding |
| `$DDDD` | Name of the day of the week |
| `$DDD` | Abbreviated name of the day of the week |
| `$DD` | Day of the month, zero-padded |
| `$D` | Day of the month, no padding |
| `$hh` | Hours, zero-padded |
| `$h` | Hours, no padding |
| `$mm` | Minutes, zero-padded |
| `$m` | Minutes, no padding |
| `$ss` | Seconds, zero-padded |
| `$s` | Seconds, no padding |
| `$fff` | Milliseconds, three digits |
| `$ff` | Milliseconds, first two digits |
| `$f` | Milliseconds, first digit |

#### Special Variables

| Variable | Explanation |
| --- | --- |
| `$PARENT_FOLDER_NAME` | The (already substituted) name of the parent template folder. Only works in files inside template subfolders, matching New+ behaviour. |

#### Environment Variables

Each `%environment_variable%` (case-insensitive) is replaced with its
value — `%USERNAME%`, `%USERDOMAIN%`, `%COMPUTERNAME%`, …

Any filesystem-invalid characters (`\ / : * ? " < > |`) in substituted
values are replaced with spaces, exactly as New+ does.

#### Example

A template file named

```
1. Meeting Notes $YYYY-$MM-$DD $hh.$mm by %USERNAME%.md
```

appears in the panel — and is created on `F5` — as

```
Meeting Notes 2026-06-13 14.30 by michal.md
```

### Configuration

Settings live in `TCNewPlus.ini`. The plugin reads it from two locations
and uses the **first one that exists** — values are not merged across
files:

1. `%APPDATA%\GHISLER\TCNewPlus.ini` — per-user override
2. `<plugin install dir>\TCNewPlus.ini` — system-wide default, shipped
   with the plugin

The shipped INI has every option commented out, so all defaults apply. To
customise just for your user account, copy that file to
`%APPDATA%\GHISLER\TCNewPlus.ini` and edit the copy; the system-wide file
is then ignored entirely. The chosen file is re-read on every listing of
the plugin root — edits take effect on the next refresh (**Ctrl+R**).
Environment variables (`%LocalAppData%`, `%UserProfile%`, …) are expanded
in any value. The INI is read as **UTF-8** (a UTF-8 or UTF-16 byte-order
mark is also honoured), so paths with accented or other non-ASCII
characters work — just save the file as UTF-8 (the default in most editors).

| Section | Key | Default | Meaning |
| --- | --- | --- | --- |
| `[Templates]` | `TemplatesPath` | `Templates` folder next to the plugin DLL | Folder whose contents appear as templates |
| `[Templates]` | `ShowLinkToTemplatesDir` | *(empty)* | Label for an extra entry that opens the real templates folder (see below) |
| `[Templates]` | `SetTimestampsToCurrentTime` | `0` | Show the current time as the entries' timestamps instead of the template files' real ones |
| `[FileNames]` | `ReplaceFilenameVariables` | `1` | Substitute the variables described above in names |
| `[FileNames]` | `HideStartingDigits` | `1` | Strip the sorting prefix (leading digits, spaces, dots) |
| `[FileContents]` | `ReplaceContentVariables` | *(empty)* | Comma-separated extensions whose **file contents** also get variables expanded (see below) |

The default templates folder is always the `Templates` folder next to the
plugin DLL, even when the per-user INI is in effect — that is where the
bundled sample set physically lives. Set `TemplatesPath` to point
elsewhere.

To share one template collection with PowerToys New+, point the plugin at
the New+ templates folder:

```ini
[Templates]
TemplatesPath=%LocalAppData%\Microsoft\PowerToys\NewPlus\Templates
```

#### Link to the Templates Folder

Like the *Open templates folder* item in the PowerToys New+ menu, the plugin
can show an extra entry in `\\\TCNewPlus` that takes you straight to the real
folder your templates live in. Set `ShowLinkToTemplatesDir` (under
`[Templates]`) to the label you want that entry to have:

```ini
[Templates]
ShowLinkToTemplatesDir=0. Open templates folder
```

Pressing **Enter** on the entry switches the panel to the actual templates
folder on disk (whatever `TemplatesPath` resolves to). The label follows the
same naming rules as templates — variables are expanded and the sorting
prefix is hidden per `HideStartingDigits`, so the `0. ` above just sorts it to
the top when prefixes are kept. The label must be a valid file name (no
`\ / : * ? " < > |`); an empty or invalid value (the default) shows no link.

The entry is a **shortcut, not a browsable folder**: use **Enter** to follow
it. Operations that open a folder *without* "executing" it (for example
"open in a new tab") do not follow the shortcut — they simply do nothing,
because there is no virtual folder to descend into.

#### Entry Timestamps

By default each entry in `\\\TCNewPlus` shows the created / modified /
accessed times of the **physical template** behind it. Set
`SetTimestampsToCurrentTime=1` (under `[Templates]`) to show the **current
time** on every entry instead — consistent with the date and time substituted
into names and contents:

```ini
[Templates]
SetTimestampsToCurrentTime=1
```

A file you create by copying a template out (`F5`) carries the **same
timestamps it is shown with** in the panel: the current time when this option
is on, or the template file's own times when it is off (the default).

#### Variable Expansion Inside File Contents

> **Extension beyond PowerToys New+.** This feature does **not** exist
> in PowerToys New+, which only substitutes variables in *filenames*.
> In-content expansion is a TCNewPlus-only addition; templates that rely on
> it will not behave the same way in the New+ Explorer menu.

TCNewPlus can additionally expand the same variables *inside the file
contents* when a template is copied out — so a Markdown template can fill
in today's date, the user name, or the project folder name in its body.

Because expanding variables inside a binary file would corrupt it, this is
**off by default** and opt-in **per file extension** — the plugin never
tries to guess whether a file is text. List the extensions to expand in
`ReplaceContentVariables`:

```ini
[FileContents]
ReplaceContentVariables=.md,.txt
```

With the above, copying a `.md` or `.txt` template runs its contents
through the same substitution as the names — except that characters
invalid in filenames are **not** replaced with spaces inside content (a
path from `%USERPROFILE%` keeps its `:` and `\`). `$PARENT_FOLDER_NAME`
expands to the name of the containing template subfolder; the date and
time are taken at copy time. Files of any other extension are copied byte
for byte.

Listed files are assumed to be **UTF-8** (a leading byte-order mark is
preserved). A file that turns out not to be valid UTF-8 is copied verbatim
rather than risk corrupting it.

### Differences from PowerToys New+

The plugin reimplements the New+ naming rules on top of Total Commander's
file-system-plugin model, which leads to a few deliberate differences:

- **File extensions are never hidden.** New+ can hide extensions in its
  menu because the menu is only a launcher; in Total Commander the
  displayed name *is* the name the copy gets, and a file without its
  extension would be broken. (TC's own "hide extension" display options
  still apply to the panel like for any other file system.)
- **Sorting is up to Total Commander.** With `HideStartingDigits=1` (the
  default) the prefixes that order the New+ menu are stripped before TC
  sorts the panel, so templates appear in alphabetical order of their
  *visible* names. Set `HideStartingDigits=0` to sort by prefix at the
  cost of the prefix ending up in the created files.
- **Names are substituted at listing time.** The timestamp in a displayed
  name is taken when the panel is listed, not when the copy starts.
  Refresh with **Ctrl+R** to re-substitute with the current time.
- **Variable replacement defaults to on.** New+ ships with *Replace
  variables in template filename* disabled; this plugin enables it by
  default (set `ReplaceFilenameVariables=0` for verbatim names).
- **Optional variable expansion inside file contents.** New+ only
  substitutes variables in filenames; this plugin can also expand them in
  the file body for chosen extensions (off by default — see
  [Variable Expansion Inside File Contents](#variable-expansion-inside-file-contents)).

### Troubleshooting

**The plugin panel is empty.** The templates folder does not exist or is
empty. Check `TemplatesPath` in `TCNewPlus.ini` (the default is the
`Templates` folder next to the plugin DLL) and refresh with `Ctrl+R`. If
you have a `%APPDATA%\GHISLER\TCNewPlus.ini`, that file is used instead of
the one shipped with the plugin — edit whichever one is in effect.

**A copied file still contains `$YYYY` in its name.** Either
`ReplaceFilenameVariables=0` is set in the INI, or the pattern is not one of
the supported variables — the patterns are case-sensitive (`$yyyy` is not
substituted).

**Two templates show as `Name.txt` and `Name (2).txt`.** Two different
template files collapse to the same display name after prefix stripping
and substitution (for example `1. Note.txt` and `2. Note.txt`). The
plugin disambiguates them so both stay reachable; rename the templates on
disk to give them distinct names.

**`F6` (move) fails.** By design — moving would delete the template. The
virtual file system is read-only; use `F5` (copy).

**The right-click menu shows Print / Rename / Move / Delete.** That menu is
built by Total Commander and the Windows shell, not by the plugin — the
file-system-plugin interface has no hook to add or remove individual
context-menu items. They are harmless here: the plugin exposes no
rename/move/delete operations (it doesn't export those functions), so they
cannot change your templates — manage templates in the real folder on disk
(see [Managing Templates](#managing-templates)). Hiding the Windows shell
verbs is a Total Commander configuration choice, independent of this plugin.

**I navigated to a path under `\\\TCNewPlus` that doesn't exist and got an
empty panel.** That is intentional: a non-existent sub-path shows an empty
listing so the panel stays inside the plugin instead of jumping back to a
previous directory. Press **Backspace** (or `cd ..`) to return to the
template list. The same applies if you "open in a new tab" on the
templates-folder link entry — use **Enter** on it instead, which follows it
to the real folder.

**Where does this entry come from?** Press **Alt+Enter** (Properties) on
any entry to see the real template path on disk and the active templates
folder.

## Contributing

Build instructions, repo layout and architecture notes for developers live
in [CONTRIBUTING.md](CONTRIBUTING.md).

## License

This project is licensed under the Apache License 2.0 — see
[LICENSE.md](LICENSE.md) for the full text. Source files carry SPDX
identifiers (`SPDX-License-Identifier: Apache-2.0`). Release notes are in
[CHANGELOG.md](CHANGELOG.md).
