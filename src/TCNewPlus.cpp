// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Michal Růžička <ruzicka.mich@gmail.com>

/*
 * TCNewPlus.cpp - Total Commander File System plugin (WFX) that exposes a
 *                 PowerToys New+ style template folder as a read-only
 *                 virtual file system.
 *
 * How it works:
 *   1. TC lists the virtual file system (\\\TCNewPlus) via FsFindFirstW /
 *      FsFindNextW. The plugin enumerates the real templates folder and
 *      applies the New+ naming rules to every entry at listing time:
 *      sorting prefixes ("1. ", "2 ") are stripped and variables ($YYYY,
 *      $MM, $hh, %USERNAME%, ...) are substituted with the current local
 *      time. The displayed name IS the name TC will create on copy.
 *   2. Each displayed name is remembered in a session-wide map back to the
 *      real template path. The map only grows — a copy started after the
 *      clock ticked past the minute the listing was made still resolves.
 *   3. On F5, TC creates the target file under the displayed name and asks
 *      the plugin for the content via FsGetFileW; the plugin streams the
 *      real template file and stamps the copy with the current time ("new
 *      document" semantics, like New+).
 *   4. No write entry points are exported, so TC itself disables delete /
 *      rename / upload — the virtual file system is read-only by design.
 *
 * Configuration is read from TCNewPlus.ini next to the plugin DLL on every
 * root listing, so edits take effect on the next Ctrl+R. See the shipped
 * TCNewPlus.sample.ini for the available keys.
 */

#include "fsplugin.h"
#include "NameSubstitution.h"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static HINSTANCE      g_hInstance = nullptr;
static int            g_pluginNr  = 0;
static tProgressProc  g_progressA = nullptr;
static tProgressProcW g_progressW = nullptr;

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

// Virtual path (e.g. L"\\Project 2026-06-13\\README.md", display names) to
// real template path on disk. Grows for the lifetime of the TC session;
// entries are only overwritten, never dropped, so a name listed before a
// clock tick still resolves when the copy starts after it.
static std::map<std::wstring, std::wstring, CaseInsensitiveLess> g_virtualToReal;
static std::mutex g_mapMutex;

// Virtual paths that are "link to the templates folder" entries (the
// ShowLinkToTemplatesDir feature). Opening one navigates TC to the real
// templates directory rather than copying a file. Guarded by g_mapMutex.
static std::set<std::wstring, CaseInsensitiveLess> g_linkPaths;

// A snapshot of the moment a directory was listed (refreshed): the local time,
// the same as a FILETIME, and the environment block. The same snapshot drives
// name substitution AND, later, the file's content substitution and copy
// timestamps, so every value stays consistent even if the clock ticks or a
// system variable changes between the listing and the copy.
struct Snapshot {
    SYSTEMTIME localTime{};
    FILETIME   fileTime{};
    std::map<std::wstring, std::wstring, CaseInsensitiveLess> env;
};

// Virtual path -> the snapshot taken when it was last listed. Grows/overwrites
// like g_virtualToReal; snapshot objects are shared per listing. Guarded by
// g_mapMutex.
static std::map<std::wstring, std::shared_ptr<Snapshot>, CaseInsensitiveLess> g_snapshots;

// Captures the current local time and environment block.
static std::shared_ptr<Snapshot> CaptureSnapshot()
{
    auto s = std::make_shared<Snapshot>();
    GetLocalTime(&s->localTime);
    GetSystemTimeAsFileTime(&s->fileTime);
    if (LPWCH block = GetEnvironmentStringsW()) {
        for (LPWCH p = block; *p; ) {
            std::wstring entry(p);
            p += entry.size() + 1;
            // Skip the special "=C:=..." drive entries (leading '=').
            size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq != 0)
                s->env.emplace(entry.substr(0, eq), entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }
    return s;
}

// Builds an EnvLookup backed by a snapshot's captured environment.
static EnvLookup SnapshotEnv(const std::shared_ptr<Snapshot>& snap)
{
    return [snap](const std::wstring& name, std::wstring& value) -> bool {
        auto it = snap->env.find(name);
        if (it == snap->env.end())
            return false;
        value = it->second;
        return true;
    };
}

// Returns the snapshot stored for `vpath`, or a fresh one if the path was never
// listed in this session (best effort — keeps a copy working).
static std::shared_ptr<Snapshot> SnapshotFor(const std::wstring& vpath)
{
    {
        std::lock_guard<std::mutex> lock(g_mapMutex);
        auto it = g_snapshots.find(vpath);
        if (it != g_snapshots.end())
            return it->second;
    }
    return CaptureSnapshot();
}

struct VirtualEntry {
    WIN32_FIND_DATAW fd;       // cFileName already holds the display name
    std::wstring     realPath;
    std::wstring     sortKey;  // raw template name (with prefix) for ordering
};

struct FindContext {
    std::vector<VirtualEntry> entries;
    size_t                    next = 0;
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    std::wstring templatesPath;
    NameRules    rules;
    // Extensions (".md", ".txt", …) whose file contents should have New+
    // variables expanded on copy. Empty by default — no in-content expansion.
    std::set<std::wstring, CaseInsensitiveLess> expandExtensions;
    // Label for an extra root entry that links to the real templates folder
    // (ShowLinkToTemplatesDir). Empty = no link entry shown.
    std::wstring linkName;
    // When true, displayed entries carry the current time instead of their
    // template's real timestamps — consistent with date variables in names.
    bool setTimestampsToCurrentTime = false;
    // When true, implies setTimestampsToCurrentTime AND steps each displayed
    // modification time one second earlier per entry (in real-name order, per
    // directory), so sorting the panel by modification time reproduces the
    // prefix ordering. Display only — substitution and copy times use the
    // snapshot instant unchanged.
    bool useTimestampsToIndicateOrdering = false;
    // The INI file actually consulted, and whether it existed on disk (for the
    // Alt+Enter properties / diagnostics).
    std::wstring iniPath;
    bool         iniExists = false;
};

// Splits a comma/semicolon-separated extension list into a set, trimming
// whitespace and ensuring a leading dot on each entry ("md, .txt" ->
// {".md", ".txt"}).
static std::set<std::wstring, CaseInsensitiveLess> ParseExtensionList(const std::wstring& raw)
{
    std::set<std::wstring, CaseInsensitiveLess> out;
    std::wstring token;

    auto flush = [&]() {
        size_t b = token.find_first_not_of(L" \t");
        size_t e = token.find_last_not_of(L" \t");
        if (b != std::wstring::npos) {
            std::wstring t = token.substr(b, e - b + 1);
            if (!t.empty()) {
                if (t[0] != L'.')
                    t.insert(t.begin(), L'.');
                out.insert(t);
            }
        }
        token.clear();
    };

    for (wchar_t c : raw) {
        if (c == L',' || c == L';')
            flush();
        else
            token += c;
    }
    flush();
    return out;
}

// Returns the extension of a path, including the leading dot (".md"), or an
// empty string when there is none.
static std::wstring FileExtension(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    std::wstring leaf = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t dot = leaf.find_last_of(L'.');
    return (dot == std::wstring::npos) ? std::wstring() : leaf.substr(dot);
}

static std::wstring PluginDirectory()
{
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(g_hInstance, path, ARRAYSIZE(path));
    std::wstring dir(path);
    size_t slash = dir.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? dir : dir.substr(0, slash);
}

static std::wstring ExpandEnvironment(const std::wstring& value)
{
    wchar_t buf[4096];
    DWORD n = ExpandEnvironmentStringsW(value.c_str(), buf, ARRAYSIZE(buf));
    return (n > 0 && n <= ARRAYSIZE(buf)) ? std::wstring(buf) : value;
}

// Total Commander's own settings folder (where wincmd.ini lives), set by
// FsSetDefaultParams right after FsInit. Empty when unknown.
static std::wstring g_tcSettingsDir;

// Per-user configuration override, in Total Commander's default settings
// folder (%APPDATA%\GHISLER). Empty when %APPDATA% is somehow undefined.
static std::wstring UserConfigPath()
{
    wchar_t appdata[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, ARRAYSIZE(appdata));
    if (n == 0 || n >= ARRAYSIZE(appdata))
        return std::wstring();
    return std::wstring(appdata) + L"\\GHISLER\\TCNewPlus.ini";
}

// Reads a whole text file into a wide string, honouring a byte-order mark
// (UTF-8, UTF-16 LE or UTF-16 BE) and defaulting to UTF-8 when there is none.
// We parse the INI ourselves instead of using GetPrivateProfileString because
// that API treats a BOM-less file as ANSI (the system code page), which
// mangles non-ASCII paths (e.g. a folder named "Osobní") in a UTF-8 INI.
static std::wstring ReadTextFileToWide(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::wstring();

    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    if (!bytes.empty()) {
        DWORD got = 0;
        if (!ReadFile(h, &bytes[0], static_cast<DWORD>(bytes.size()), &got, nullptr))
            got = 0;
        bytes.resize(got);
    }
    CloseHandle(h);

    auto u = [&](size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 2 && u(0) == 0xFF && u(1) == 0xFE) {           // UTF-16 LE
        const size_t count = (bytes.size() - 2) / 2;
        std::wstring w(count, L'\0');
        memcpy(&w[0], bytes.data() + 2, count * 2);
        return w;
    }
    if (bytes.size() >= 2 && u(0) == 0xFE && u(1) == 0xFF) {           // UTF-16 BE
        const size_t count = (bytes.size() - 2) / 2;
        std::wstring w(count, L'\0');
        for (size_t i = 0; i < count; ++i)
            w[i] = static_cast<wchar_t>((u(2 + 2 * i) << 8) | u(3 + 2 * i));
        return w;
    }

    size_t off = (bytes.size() >= 3 && u(0) == 0xEF && u(1) == 0xBB && u(2) == 0xBF)
                     ? 3 : 0;                                          // skip UTF-8 BOM
    int len = static_cast<int>(bytes.size() - off);
    if (len <= 0)
        return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, len, nullptr, 0);
    std::wstring w(wlen, L'\0');
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, len, &w[0], wlen);
    return w;
}

static std::wstring TrimWs(const std::wstring& s)
{
    size_t b = s.find_first_not_of(L" \t");
    if (b == std::wstring::npos)
        return std::wstring();
    size_t e = s.find_last_not_of(L" \t");
    return s.substr(b, e - b + 1);
}

// Finds [section] key=value in INI text (case-insensitive section and key).
// Lines starting with ';' or '#' are comments. Surrounding double quotes are
// stripped from the value. Returns true and sets `out` when found.
static bool IniLookup(const std::wstring& text, const std::wstring& section,
                      const std::wstring& key, std::wstring& out)
{
    std::wstring curSection;
    size_t pos = 0;
    const size_t n = text.size();

    while (pos < n) {
        size_t eol = text.find_first_of(L"\r\n", pos);
        if (eol == std::wstring::npos)
            eol = n;
        std::wstring line = TrimWs(text.substr(pos, eol - pos));
        pos = (eol < n) ? eol + 1 : n;

        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;

        if (line[0] == L'[') {
            size_t close = line.find(L']');
            if (close != std::wstring::npos)
                curSection = line.substr(1, close - 1);
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        if (_wcsicmp(TrimWs(line.substr(0, eq)).c_str(), key.c_str()) == 0 &&
            _wcsicmp(curSection.c_str(), section.c_str()) == 0) {
            std::wstring val = TrimWs(line.substr(eq + 1));
            if (val.size() >= 2 && val.front() == L'"' && val.back() == L'"')
                val = val.substr(1, val.size() - 2);
            out = val;
            return true;
        }
    }
    return false;
}

static bool IniLookupBool(const std::wstring& text, const std::wstring& section,
                          const std::wstring& key, bool def)
{
    std::wstring v;
    if (!IniLookup(text, section, key, v) || v.empty())
        return def;
    if (_wcsicmp(v.c_str(), L"0") == 0 || _wcsicmp(v.c_str(), L"false") == 0 ||
        _wcsicmp(v.c_str(), L"no") == 0 || _wcsicmp(v.c_str(), L"off") == 0)
        return false;
    if (_wcsicmp(v.c_str(), L"1") == 0 || _wcsicmp(v.c_str(), L"true") == 0 ||
        _wcsicmp(v.c_str(), L"yes") == 0 || _wcsicmp(v.c_str(), L"on") == 0)
        return true;
    return _wtoi(v.c_str()) != 0;
}

// Strips trailing path separators so e.g. a TemplatesPath written with a
// trailing backslash ("...\Templates\") joins cleanly with child names.
static std::wstring StripTrailingSeparators(std::wstring p)
{
    while (p.size() > 1 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    return p;
}

static Config LoadConfig()
{
    Config cfg;
    const std::wstring dir = PluginDirectory();

    // Lookup order, mirroring TCOfficeView: the first file that exists is
    // used in full — settings are NOT merged across them.
    //   1. <folder of wincmd.ini>\TCNewPlus.ini (TC's own settings folder;
    //      differs from 2 only in a portable or redirected TC)
    //   2. %APPDATA%\GHISLER\TCNewPlus.ini   (per-user override)
    //   3. <plugin install dir>\TCNewPlus.ini (system-wide)
    const auto exists = [](const std::wstring& path) {
        return !path.empty() &&
               GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    const std::wstring tcIni = g_tcSettingsDir.empty()
                                   ? std::wstring()
                                   : g_tcSettingsDir + L"\\TCNewPlus.ini";
    const std::wstring userIni = UserConfigPath();
    const std::wstring iniPath = exists(tcIni)     ? tcIni
                                 : exists(userIni) ? userIni
                                                   : dir + L"\\TCNewPlus.ini";
    cfg.iniPath   = iniPath;
    cfg.iniExists = GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    const std::wstring ini = ReadTextFileToWide(iniPath);

    std::wstring value;
    cfg.templatesPath =
        (IniLookup(ini, L"Templates", L"TemplatesPath", value) && !value.empty())
            ? StripTrailingSeparators(ExpandEnvironment(value))
            : dir + L"\\Templates";

    cfg.rules.replaceVariables =
        IniLookupBool(ini, L"FileNames", L"ReplaceFilenameVariables", true);
    cfg.rules.hideStartingDigits =
        IniLookupBool(ini, L"FileNames", L"HideStartingDigits", true);

    std::wstring exts;
    IniLookup(ini, L"FileContents", L"ReplaceContentVariables", exts);
    cfg.expandExtensions = ParseExtensionList(exts);

    // IniLookup already trims the value; an absent key or empty value leaves
    // linkName empty, which disables the templates-folder link entry.
    IniLookup(ini, L"Templates", L"ShowLinkToTemplatesDir", cfg.linkName);

    cfg.setTimestampsToCurrentTime =
        IniLookupBool(ini, L"Templates", L"SetTimestampsToCurrentTime", false);
    cfg.useTimestampsToIndicateOrdering =
        IniLookupBool(ini, L"Templates", L"UseTimestampsToIndicateOrdering", false);

    return cfg;
}

// ---------------------------------------------------------------------------
// Virtual path handling
// ---------------------------------------------------------------------------

// Normalizes a path coming from TC: backslash separators, no trailing
// backslash, root is L"\\".
static std::wstring NormalizeVirtualPath(const wchar_t* path)
{
    std::wstring p = path ? path : L"";
    if (p.empty())
        return L"\\";
    while (p.size() > 1 && p.back() == L'\\')
        p.pop_back();
    return p;
}

static std::wstring VirtualParent(const std::wstring& vpath)
{
    size_t slash = vpath.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0)
        return L"\\";
    return vpath.substr(0, slash);
}

static std::wstring VirtualLeaf(const std::wstring& vpath)
{
    size_t slash = vpath.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? vpath : vpath.substr(slash + 1);
}

static std::wstring JoinVirtual(const std::wstring& dir, const std::wstring& name)
{
    return (dir == L"\\") ? L"\\" + name : dir + L"\\" + name;
}

static std::wstring MapLookup(const std::wstring& vpath)
{
    std::lock_guard<std::mutex> lock(g_mapMutex);
    auto it = g_virtualToReal.find(vpath);
    return (it != g_virtualToReal.end()) ? it->second : std::wstring();
}

// True if `vpath` is the virtual "link to the templates folder" entry.
static bool IsTemplatesDirLink(const std::wstring& vpath)
{
    std::lock_guard<std::mutex> lock(g_mapMutex);
    return g_linkPaths.count(vpath) != 0;
}

static bool EnumerateVirtualDir(const std::wstring& virtualDir,
                                const Config&       cfg,
                                std::vector<VirtualEntry>& out);

// A display name is valid for the panel if it is non-empty and contains no
// characters that are illegal in a Windows filename. Used to reject a bad
// ShowLinkToTemplatesDir value (real template files can never carry such
// characters, so only the user-typed link label needs checking).
static bool IsValidEntryName(const std::wstring& name)
{
    if (name.empty())
        return false;
    for (wchar_t c : name) {
        switch (c) {
            case L'\\': case L'/': case L':': case L'*':
            case L'?':  case L'"': case L'<': case L'>': case L'|':
                return false;
            default:
                if (c < 0x20)
                    return false;
        }
    }
    return true;
}

// Ensures `display` is unique within a directory listing, appending " (2)",
// " (3)", … before the extension on collision. Records the chosen name in
// `usedNames`.
static std::wstring MakeUniqueName(std::wstring display, bool isDir,
                                   std::set<std::wstring, CaseInsensitiveLess>& usedNames)
{
    if (usedNames.count(display)) {
        size_t dot = isDir ? std::wstring::npos : display.find_last_of(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? display : display.substr(0, dot);
        std::wstring ext  = (dot == std::wstring::npos) ? L"" : display.substr(dot);
        for (int ordinal = 2; ; ++ordinal) {
            wchar_t suffix[16];
            swprintf_s(suffix, L" (%d)", ordinal);
            std::wstring candidate = stem + suffix + ext;
            if (!usedNames.count(candidate)) {
                display = candidate;
                break;
            }
        }
    }
    usedNames.insert(display);
    return display;
}

// Maps a virtual path to the real template path, enumerating ancestor
// directories on demand (TC may call FsGetFile without a prior listing in
// this process, e.g. when resuming a queued background transfer).
static std::wstring ResolveRealPath(const std::wstring& vpath, const Config& cfg)
{
    if (vpath == L"\\" || vpath.empty())
        return cfg.templatesPath;

    std::wstring real = MapLookup(vpath);
    if (!real.empty())
        return real;

    std::vector<VirtualEntry> ignored;
    if (!EnumerateVirtualDir(VirtualParent(vpath), cfg, ignored))
        return std::wstring();

    return MapLookup(vpath);
}

// Lists the real directory behind `virtualDir` and produces entries whose
// names already follow the New+ rules. Every entry is recorded in the
// virtual-to-real map. Returns false when the real directory cannot be
// resolved or opened.
static bool EnumerateVirtualDir(const std::wstring& virtualDir,
                                const Config&       cfg,
                                std::vector<VirtualEntry>& out)
{
    const std::wstring realDir = ResolveRealPath(virtualDir, cfg);
    if (realDir.empty())
        return false;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((realDir + L"\\*").c_str(), &fd);
    // INVALID_HANDLE_VALUE with ERROR_FILE_NOT_FOUND means the directory
    // exists but is empty — keep going (we may still add the link entry).
    // Any other error means the directory is missing/unreadable.
    if (hFind == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND)
        return false;

    // One snapshot per directory listing: every name (and, when enabled, every
    // displayed time) uses it, and it is stored per entry so the later copy of
    // that entry expands its contents and stamps its times from the SAME
    // moment — even if the clock or a system variable changed in between.
    const std::shared_ptr<Snapshot> snap = CaptureSnapshot();
    const EnvLookup env = SnapshotEnv(snap);

    // UseTimestampsToIndicateOrdering implies the current-time display, and
    // additionally steps the times per entry below.
    const bool currentTimeDisplay =
        cfg.setTimestampsToCurrentTime || cfg.useTimestampsToIndicateOrdering;

    // $PARENT_FOLDER_NAME inside a template subfolder expands to the
    // (already substituted) display name of that subfolder; empty at root.
    const std::wstring parentName =
        (virtualDir == L"\\") ? std::wstring() : VirtualLeaf(virtualDir);

    std::set<std::wstring, CaseInsensitiveLess> usedNames;

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::wstring display = MakeUniqueName(
                MakeDisplayName(fd.cFileName, cfg.rules, snap->localTime, parentName, env),
                isDir, usedNames);

            VirtualEntry entry;
            entry.fd = fd;
            entry.sortKey = fd.cFileName; // real name, with prefix, for ordering
            wcsncpy_s(entry.fd.cFileName, display.c_str(), _TRUNCATE);
            entry.fd.cAlternateFileName[0] = L'\0';
            // The copy TC creates must be writable even if the template is
            // marked read-only on disk — New+ copies are ordinary new files.
            entry.fd.dwFileAttributes &= ~FILE_ATTRIBUTE_READONLY;
            if (currentTimeDisplay)
                entry.fd.ftCreationTime = entry.fd.ftLastAccessTime =
                    entry.fd.ftLastWriteTime = snap->fileTime;
            entry.realPath = realDir + L"\\" + fd.cFileName;

            // Key the maps by the exact name handed back to TC (cFileName, which
            // wcsncpy_s may have truncated at MAX_PATH) so a later lookup with
            // the name TC echoes back always matches.
            {
                std::lock_guard<std::mutex> lock(g_mapMutex);
                const std::wstring key = JoinVirtual(virtualDir, entry.fd.cFileName);
                g_virtualToReal[key] = entry.realPath;
                g_snapshots[key] = snap;
            }

            out.push_back(std::move(entry));
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    // Optional "link to the templates folder" entry (ShowLinkToTemplatesDir),
    // shown only at the root. Its label follows the same naming rules as
    // template files (prefix hiding + variable substitution); an invalid or
    // empty resulting name is skipped. Opening it navigates TC to the real
    // templates folder (handled in FsExecuteFileW).
    if (virtualDir == L"\\" && !cfg.linkName.empty()) {
        std::wstring linkDisplay =
            MakeDisplayName(cfg.linkName, cfg.rules, snap->localTime, std::wstring(), env);
        if (IsValidEntryName(linkDisplay)) {
            linkDisplay = MakeUniqueName(linkDisplay, /*isDir=*/false, usedNames);

            VirtualEntry link;
            link.fd = {};
            link.fd.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            link.sortKey = cfg.linkName; // raw label (with prefix) for ordering
            // Timestamps: the snapshot time under the current-time display;
            // otherwise mirror the link's target (the templates folder).
            WIN32_FILE_ATTRIBUTE_DATA ad;
            if (!currentTimeDisplay &&
                GetFileAttributesExW(cfg.templatesPath.c_str(),
                                     GetFileExInfoStandard, &ad)) {
                link.fd.ftCreationTime   = ad.ftCreationTime;
                link.fd.ftLastAccessTime = ad.ftLastAccessTime;
                link.fd.ftLastWriteTime  = ad.ftLastWriteTime;
            } else {
                link.fd.ftCreationTime = link.fd.ftLastAccessTime =
                    link.fd.ftLastWriteTime = snap->fileTime;
            }
            wcsncpy_s(link.fd.cFileName, linkDisplay.c_str(), _TRUNCATE);
            link.fd.cAlternateFileName[0] = L'\0';

            // Track the link ONLY in g_linkPaths — deliberately NOT in
            // g_virtualToReal. If it were resolvable to a real path, TC could
            // treat it as a browsable directory (e.g. "open in new tab")
            // and FsFindFirstW would re-list the templates folder as a phantom
            // subdirectory. It is a file entry; Enter goes through
            // FsExecuteFileW, which redirects to the real folder.
            const std::wstring linkVPath = JoinVirtual(L"\\", link.fd.cFileName);
            {
                std::lock_guard<std::mutex> lock(g_mapMutex);
                g_linkPaths.insert(linkVPath);
            }
            out.push_back(std::move(link));
        }
    }

    // UseTimestampsToIndicateOrdering: order entries by their real name (with
    // prefix, natural/Explorer-style sort, same as the New+ menu) and step the
    // displayed modification time one second earlier per position. Sorting the
    // panel by modification time then reproduces that order. We subtract (never
    // add) so the times never land in the future. Display only — the maps,
    // names and copy times keep the snapshot instant.
    if (cfg.useTimestampsToIndicateOrdering && !out.empty()) {
        std::stable_sort(out.begin(), out.end(),
                         [](const VirtualEntry& a, const VirtualEntry& b) {
                             return StrCmpLogicalW(a.sortKey.c_str(),
                                                   b.sortKey.c_str()) < 0;
                         });
        ULONGLONG base = (static_cast<ULONGLONG>(snap->fileTime.dwHighDateTime) << 32) |
                         snap->fileTime.dwLowDateTime;
        for (size_t i = 0; i < out.size(); ++i) {
            ULONGLONG t = base - static_cast<ULONGLONG>(i) * 10000000ULL; // i seconds
            FILETIME ft;
            ft.dwLowDateTime  = static_cast<DWORD>(t & 0xFFFFFFFFULL);
            ft.dwHighDateTime = static_cast<DWORD>(t >> 32);
            out[i].fd.ftCreationTime = out[i].fd.ftLastAccessTime =
                out[i].fd.ftLastWriteTime = ft;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Progress reporting
// ---------------------------------------------------------------------------

// Returns true when the user requested an abort via TC's progress dialog.
static bool ProgressAborted(const wchar_t* source, const wchar_t* target, int percent)
{
    if (g_progressW)
        return g_progressW(g_pluginNr, const_cast<wchar_t*>(source),
                           const_cast<wchar_t*>(target), percent) != 0;

    if (g_progressA) {
        char src[MAX_PATH * 2] = "";
        char dst[MAX_PATH * 2] = "";
        WideCharToMultiByte(CP_ACP, 0, source, -1, src, sizeof(src), nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, target, -1, dst, sizeof(dst), nullptr, nullptr);
        return g_progressA(g_pluginNr, src, dst, percent) != 0;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Unicode entry points (primary implementation)
// ---------------------------------------------------------------------------

int __stdcall FsInitW(int PluginNr, tProgressProcW pProgressProcW,
                      tLogProcW /*pLogProcW*/, tRequestProcW /*pRequestProcW*/)
{
    g_pluginNr  = PluginNr;
    g_progressW = pProgressProcW;
    return 0;
}

HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData)
{
    const std::wstring virtualDir = NormalizeVirtualPath(Path);
    const Config cfg = LoadConfig();

    auto* ctx = new FindContext();
    if (!EnumerateVirtualDir(virtualDir, cfg, ctx->entries)) {
        delete ctx;
        // A failing ROOT means the templates folder itself is missing or
        // unreadable — a real configuration error, so report "path not found".
        //
        // A failing SUBPATH (a typed non-existent name, or the link entry
        // opened as a folder) is reported as an empty directory instead. TC
        // treats ERROR_NO_MORE_FILES as "exists but empty" and stays inside
        // the plugin showing an empty panel — one Backspace from the root —
        // rather than bouncing the panel to an unrelated directory from its
        // history, which a generic error makes it do.
        SetLastError(virtualDir == L"\\" ? ERROR_PATH_NOT_FOUND
                                         : ERROR_NO_MORE_FILES);
        return INVALID_HANDLE_VALUE;
    }

    if (ctx->entries.empty()) {
        delete ctx;
        SetLastError(ERROR_NO_MORE_FILES);
        return INVALID_HANDLE_VALUE;
    }

    *FindData = ctx->entries[0].fd;
    ctx->next = 1;
    return ctx;
}

BOOL __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData)
{
    auto* ctx = static_cast<FindContext*>(Hdl);
    if (!ctx || ctx->next >= ctx->entries.size())
        return FALSE;

    *FindData = ctx->entries[ctx->next++].fd;
    return TRUE;
}

int __stdcall FsFindClose(HANDLE Hdl)
{
    delete static_cast<FindContext*>(Hdl);
    return 0;
}

// Sentinel returned by CopyExpanded when the source is not valid UTF-8, so
// the caller falls back to a verbatim byte copy rather than risk corrupting
// the file.
static const int FS_RAW_FALLBACK = -1;

struct CopyTimes {
    FILETIME create{};
    FILETIME access{};
    FILETIME write{};
};

// The timestamps a copied-out file should carry so it matches what the panel
// shows for the entry: the listing snapshot's time when SetTimestampsToCurrent
// Time is on, otherwise the template file's own times.
static CopyTimes ResolveCopyTimes(const std::wstring& realPath, const Config& cfg,
                                  const std::shared_ptr<Snapshot>& snap)
{
    // Copy timestamps are governed SOLELY by SetTimestampsToCurrentTime. The
    // UseTimestampsToIndicateOrdering steps are a display-only sorting aid and
    // must not leak into created files — with SetTimestampsToCurrentTime off, a
    // copy keeps the template's real times even when ordering is on.
    CopyTimes t;
    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!cfg.setTimestampsToCurrentTime &&
        GetFileAttributesExW(realPath.c_str(), GetFileExInfoStandard, &ad)) {
        t.create = ad.ftCreationTime;
        t.access = ad.ftLastAccessTime;
        t.write  = ad.ftLastWriteTime;
    } else {
        t.create = t.access = t.write = snap->fileTime;
    }
    return t;
}

static void StampFile(HANDLE hFile, const CopyTimes& t)
{
    SetFileTime(hFile, &t.create, &t.access, &t.write);
}

// Copies `realPath` to `localName` while expanding New+ variables in its
// (UTF-8) text content. A leading UTF-8 byte-order mark is preserved.
// Variables are expanded WITHOUT filename sanitisation — characters like
// ':' or '\' from %USERPROFILE% stay intact in the content. Returns an
// FS_FILE_* code, or FS_RAW_FALLBACK when the content is not valid UTF-8.
static int CopyExpanded(const std::wstring& realPath,
                        const std::wstring& localName,
                        const std::wstring& parentName,
                        const CopyTimes&    times,
                        const std::shared_ptr<Snapshot>& snap)
{
    HANDLE hSrc = CreateFileW(realPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hSrc == INVALID_HANDLE_VALUE)
        return FS_FILE_NOTFOUND;

    LARGE_INTEGER size = {};
    GetFileSizeEx(hSrc, &size);
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    bool readOk = bytes.empty();
    if (!bytes.empty()) {
        DWORD got = 0;
        readOk = ReadFile(hSrc, &bytes[0], static_cast<DWORD>(bytes.size()),
                          &got, nullptr) && got == bytes.size();
    }
    CloseHandle(hSrc);
    if (!readOk)
        return FS_FILE_READERROR;

    const char* kBom = "\xEF\xBB\xBF";
    bool hasBom = bytes.size() >= 3 && memcmp(bytes.data(), kBom, 3) == 0;
    const char* textPtr = bytes.data() + (hasBom ? 3 : 0);
    int textLen = static_cast<int>(bytes.size() - (hasBom ? 3 : 0));

    std::wstring wide;
    if (textLen > 0) {
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       textPtr, textLen, nullptr, 0);
        if (wlen <= 0)
            return FS_RAW_FALLBACK; // not valid UTF-8 — let the caller copy raw
        wide.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, textPtr, textLen, &wide[0], wlen);
    }

    std::wstring expanded = ReplaceTemplateVariables(
        wide, snap->localTime, parentName, SnapshotEnv(snap), false);

    std::string out;
    if (hasBom)
        out.append(kBom, 3);
    if (!expanded.empty()) {
        int blen = WideCharToMultiByte(CP_UTF8, 0, expanded.data(),
                                       static_cast<int>(expanded.size()),
                                       nullptr, 0, nullptr, nullptr);
        size_t base = out.size();
        out.resize(base + blen);
        WideCharToMultiByte(CP_UTF8, 0, expanded.data(),
                            static_cast<int>(expanded.size()),
                            &out[base], blen, nullptr, nullptr);
    }

    HANDLE hDst = CreateFileW(localName.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDst == INVALID_HANDLE_VALUE)
        return FS_FILE_WRITEERROR;

    bool writeOk = out.empty();
    if (!out.empty()) {
        DWORD put = 0;
        writeOk = WriteFile(hDst, out.data(), static_cast<DWORD>(out.size()),
                            &put, nullptr) && put == out.size();
    }
    StampFile(hDst, times);
    CloseHandle(hDst);

    if (!writeOk) {
        DeleteFileW(localName.c_str());
        return FS_FILE_WRITEERROR;
    }
    return FS_FILE_OK;
}

int __stdcall FsGetFileW(WCHAR* RemoteName, WCHAR* LocalName, int CopyFlags,
                         RemoteInfoStruct* /*ri*/)
{
    if (CopyFlags & FS_COPYFLAGS_RESUME)
        return FS_FILE_NOTSUPPORTED;
    // F6 (move) would have to delete the template afterwards; the virtual
    // file system is read-only, so moving is refused — use F5 instead.
    if (CopyFlags & FS_COPYFLAGS_MOVE)
        return FS_FILE_NOTSUPPORTED;

    const std::wstring vpath = NormalizeVirtualPath(RemoteName);

    // The templates-folder link is not a real file — it cannot be copied out.
    if (IsTemplatesDirLink(vpath))
        return FS_FILE_NOTSUPPORTED;

    const Config cfg = LoadConfig();
    const std::wstring realPath = ResolveRealPath(vpath, cfg);
    if (realPath.empty())
        return FS_FILE_NOTFOUND;

    if (GetFileAttributesW(LocalName) != INVALID_FILE_ATTRIBUTES &&
        !(CopyFlags & FS_COPYFLAGS_OVERWRITE))
        return FS_FILE_EXISTS;

    if (ProgressAborted(RemoteName, LocalName, 0))
        return FS_FILE_USERABORT;

    // Use the snapshot taken when this entry was listed, so the content
    // substitution and the copy's timestamps match the name TC already fixed at
    // listing time (same clock instant, same environment values).
    const std::shared_ptr<Snapshot> snap = SnapshotFor(vpath);

    // The created file carries the same timestamps the panel shows for the
    // entry (template's real times by default, the snapshot time when
    // SetTimestampsToCurrentTime is on).
    const CopyTimes copyTimes = ResolveCopyTimes(realPath, cfg, snap);

    // Expand New+ variables inside the content for configured extensions.
    // $PARENT_FOLDER_NAME uses the (already substituted) display name of the
    // immediate parent folder; empty at the plugin root.
    if (cfg.expandExtensions.count(FileExtension(vpath))) {
        const std::wstring parentDir = VirtualParent(vpath);
        const std::wstring parentName =
            (parentDir == L"\\") ? std::wstring() : VirtualLeaf(parentDir);
        int r = CopyExpanded(realPath, LocalName, parentName, copyTimes, snap);
        if (r != FS_RAW_FALLBACK) {
            if (r == FS_FILE_OK)
                ProgressAborted(RemoteName, LocalName, 100);
            return r;
        }
        // Not valid UTF-8 — fall through to a verbatim byte copy.
    }

    HANDLE hSrc = CreateFileW(realPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hSrc == INVALID_HANDLE_VALUE)
        return (GetLastError() == ERROR_FILE_NOT_FOUND ||
                GetLastError() == ERROR_PATH_NOT_FOUND)
                   ? FS_FILE_NOTFOUND : FS_FILE_READERROR;

    LARGE_INTEGER totalSize = {};
    GetFileSizeEx(hSrc, &totalSize);

    HANDLE hDst = CreateFileW(LocalName, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrc);
        return FS_FILE_WRITEERROR;
    }

    int  result = FS_FILE_OK;
    char buffer[256 * 1024];
    LONGLONG copied = 0;

    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(hSrc, buffer, sizeof(buffer), &bytesRead, nullptr)) {
            result = FS_FILE_READERROR;
            break;
        }
        if (bytesRead == 0)
            break;

        DWORD bytesWritten = 0;
        if (!WriteFile(hDst, buffer, bytesRead, &bytesWritten, nullptr) ||
            bytesWritten != bytesRead) {
            result = FS_FILE_WRITEERROR;
            break;
        }

        copied += bytesRead;
        int percent = (totalSize.QuadPart > 0)
                          ? static_cast<int>(copied * 100 / totalSize.QuadPart)
                          : 100;
        if (ProgressAborted(RemoteName, LocalName, percent)) {
            result = FS_FILE_USERABORT;
            break;
        }
    }

    if (result == FS_FILE_OK)
        StampFile(hDst, copyTimes);

    CloseHandle(hSrc);
    CloseHandle(hDst);

    if (result != FS_FILE_OK)
        DeleteFileW(LocalName);
    else
        ProgressAborted(RemoteName, LocalName, 100);

    return result;
}

int __stdcall FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb)
{
    if (_wcsicmp(Verb, L"open") == 0) {
        const std::wstring vpath = NormalizeVirtualPath(RemoteName);

        if (IsTemplatesDirLink(vpath)) {
            // The templates-folder link: navigate TC straight to the real
            // folder. FS_EXEC_SYMLINK accepts a local path (drive letter or
            // UNC) of at most MAX_PATH-1 characters in RemoteName.
            const std::wstring target = LoadConfig().templatesPath;
            lstrcpynW(RemoteName, target.c_str(), MAX_PATH);
            return FS_EXEC_SYMLINK;
        }

        // Let TC download the file to a temporary location and open it there.
        return FS_EXEC_YOURSELF;
    }

    if (_wcsicmp(Verb, L"properties") == 0) {
        const std::wstring vpath = NormalizeVirtualPath(RemoteName);
        const Config cfg = LoadConfig();
        const std::wstring realPath = ResolveRealPath(vpath, cfg);

        auto onOff = [](bool b) -> std::wstring { return b ? L"on" : L"off"; };
        std::wstring exts;
        for (const auto& e : cfg.expandExtensions)
            exts += (exts.empty() ? L"" : L",") + e;

        std::wstring text =
            L"Virtual name:\n" + vpath +
            L"\n\nTemplate source:\n" +
            (realPath.empty() ? L"(not found)" : realPath) +
            L"\n\nActive config file" +
            (cfg.iniExists ? L":\n" : L" (does not exist, using defaults):\n") +
            cfg.iniPath +
            L"\n\nSettings in effect:" +
            L"\n  TemplatesPath = " + cfg.templatesPath +
            L"\n  ReplaceFilenameVariables = " + onOff(cfg.rules.replaceVariables) +
            L"\n  HideStartingDigits = " + onOff(cfg.rules.hideStartingDigits) +
            L"\n  ReplaceContentVariables = " + (exts.empty() ? L"(none)" : exts) +
            L"\n  ShowLinkToTemplatesDir = " +
            (cfg.linkName.empty() ? L"(none)" : cfg.linkName) +
            L"\n  SetTimestampsToCurrentTime = " + onOff(cfg.setTimestampsToCurrentTime) +
            L"\n  UseTimestampsToIndicateOrdering = " +
            onOff(cfg.useTimestampsToIndicateOrdering);
        MessageBoxW(MainWin, text.c_str(), L"TCNewPlus", MB_OK | MB_ICONINFORMATION);
        return FS_EXEC_OK;
    }

    return FS_EXEC_ERROR;
}

int __stdcall FsExtractCustomIconW(WCHAR* RemoteName, int ExtractFlags, HICON* TheIcon)
{
    const std::wstring vpath = NormalizeVirtualPath(RemoteName);

    // The plugin's own icon in Network Neighborhood comes from the DLL's
    // icon resource, and the root has no item of its own to draw — leave
    // ".." / the root to TC's default icon.
    if (vpath == L"\\")
        return FS_ICON_USEDEFAULT;

    // The templates-folder link has no real file of its own; show a generic
    // folder icon derived from attributes (no disk access).
    if (IsTemplatesDirLink(vpath)) {
        SHFILEINFOW sfi = {};
        UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES |
                     ((ExtractFlags & FS_ICONFLAG_SMALL) ? SHGFI_SMALLICON
                                                         : SHGFI_LARGEICON);
        if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                           flags) && sfi.hIcon) {
            *TheIcon = sfi.hIcon;
            return FS_ICON_EXTRACTED_DESTROY;
        }
        return FS_ICON_USEDEFAULT;
    }

    const std::wstring realPath = ResolveRealPath(vpath, LoadConfig());
    if (realPath.empty())
        return FS_ICON_USEDEFAULT;

    // Show the real template file's shell icon (the same icon the created
    // file will have), so e.g. a .docx template shows the Word document
    // icon and folder templates show a folder.
    SHFILEINFOW sfi = {};
    UINT flags = SHGFI_ICON |
                 ((ExtractFlags & FS_ICONFLAG_SMALL) ? SHGFI_SMALLICON
                                                      : SHGFI_LARGEICON);
    if (SHGetFileInfoW(realPath.c_str(), 0, &sfi, sizeof(sfi), flags) && sfi.hIcon) {
        *TheIcon = sfi.hIcon;
        return FS_ICON_EXTRACTED_DESTROY; // TC owns it; DestroyIcon when done
    }

    return FS_ICON_USEDEFAULT;
}

// ---------------------------------------------------------------------------
// ANSI entry points (wrappers for old 32-bit Total Commander versions)
// ---------------------------------------------------------------------------

static std::wstring AnsiToWide(const char* s)
{
    if (!s)
        return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, s, -1, &out[0], n);
    return out;
}

static void FindDataWToA(const WIN32_FIND_DATAW& w, WIN32_FIND_DATAA* a)
{
    a->dwFileAttributes = w.dwFileAttributes;
    a->ftCreationTime   = w.ftCreationTime;
    a->ftLastAccessTime = w.ftLastAccessTime;
    a->ftLastWriteTime  = w.ftLastWriteTime;
    a->nFileSizeHigh    = w.nFileSizeHigh;
    a->nFileSizeLow     = w.nFileSizeLow;
    a->dwReserved0      = w.dwReserved0;
    a->dwReserved1      = w.dwReserved1;
    WideCharToMultiByte(CP_ACP, 0, w.cFileName, -1,
                        a->cFileName, sizeof(a->cFileName), nullptr, nullptr);
    a->cAlternateFileName[0] = '\0';
}

int __stdcall FsInit(int PluginNr, tProgressProc pProgressProc,
                     tLogProc /*pLogProc*/, tRequestProc /*pRequestProc*/)
{
    g_pluginNr  = PluginNr;
    g_progressA = pProgressProc;
    return 0;
}

HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData)
{
    WIN32_FIND_DATAW fdw;
    HANDLE h = FsFindFirstW(&AnsiToWide(Path)[0], &fdw);
    if (h != INVALID_HANDLE_VALUE)
        FindDataWToA(fdw, FindData);
    return h;
}

BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData)
{
    WIN32_FIND_DATAW fdw;
    if (!FsFindNextW(Hdl, &fdw))
        return FALSE;
    FindDataWToA(fdw, FindData);
    return TRUE;
}

int __stdcall FsGetFile(char* RemoteName, char* LocalName, int CopyFlags,
                        RemoteInfoStruct* ri)
{
    std::wstring remote = AnsiToWide(RemoteName);
    std::wstring local  = AnsiToWide(LocalName);
    return FsGetFileW(&remote[0], &local[0], CopyFlags, ri);
}

int __stdcall FsExecuteFile(HWND MainWin, char* RemoteName, char* Verb)
{
    // Use a MAX_PATH buffer: FsExecuteFileW may write a redirect path back into
    // RemoteName for FS_EXEC_SYMLINK (the templates-folder link), which would
    // overflow a buffer sized to the original name.
    wchar_t remote[MAX_PATH];
    lstrcpynW(remote, AnsiToWide(RemoteName).c_str(), MAX_PATH);
    std::wstring verb = AnsiToWide(Verb);

    int rc = FsExecuteFileW(MainWin, remote, &verb[0]);
    if (rc == FS_EXEC_SYMLINK)
        WideCharToMultiByte(CP_ACP, 0, remote, -1, RemoteName, MAX_PATH, nullptr, nullptr);
    return rc;
}

int __stdcall FsExtractCustomIcon(char* RemoteName, int ExtractFlags, HICON* TheIcon)
{
    std::wstring remote = AnsiToWide(RemoteName);
    return FsExtractCustomIconW(&remote[0], ExtractFlags, TheIcon);
}

// ---------------------------------------------------------------------------
// Plugin metadata
// ---------------------------------------------------------------------------

void __stdcall FsGetDefRootName(char* DefRootName, int maxlen)
{
    lstrcpynA(DefRootName, "TCNewPlus", maxlen);
}

void __stdcall FsSetDefaultParams(FsDefaultParamStruct* dps)
{
    // DefaultIniName names a file shared by all file system plugins; only its
    // folder is of interest here. A path that is not absolute (never seen
    // from TC) is ignored rather than resolved against the current directory.
    g_tcSettingsDir.clear();
    if (!dps)
        return;
    wchar_t buf[MAX_PATH] = L"";
    int n = MultiByteToWideChar(CP_ACP, 0, dps->DefaultIniName,
                                static_cast<int>(strnlen(dps->DefaultIniName, MAX_PATH)),
                                buf, MAX_PATH - 1);
    const std::wstring path(buf, n > 0 ? n : 0);
    const bool absolute = path.size() > 2 &&
                          (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\'));
    const size_t slash = path.find_last_of(L'\\');
    if (absolute && slash != std::wstring::npos)
        g_tcSettingsDir = path.substr(0, slash);
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
