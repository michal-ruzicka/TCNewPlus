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
 * INI for the available keys.
 */

#include "fsplugin.h"
#include "NameSubstitution.h"

#include <windows.h>
#include <shellapi.h>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

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

struct VirtualEntry {
    WIN32_FIND_DATAW fd;       // cFileName already holds the display name
    std::wstring     realPath;
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

// Per-user configuration override, in Total Commander's own settings folder
// (%APPDATA%\GHISLER). Empty when %APPDATA% is somehow undefined.
static std::wstring UserConfigPath()
{
    wchar_t appdata[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, ARRAYSIZE(appdata));
    if (n == 0 || n >= ARRAYSIZE(appdata))
        return std::wstring();
    return std::wstring(appdata) + L"\\GHISLER\\TCNewPlus.ini";
}

static Config LoadConfig()
{
    Config cfg;
    const std::wstring dir = PluginDirectory();

    // Lookup order, mirroring TCOfficeView: the first file that exists is
    // used in full — settings are NOT merged across the two.
    //   1. %APPDATA%\GHISLER\TCNewPlus.ini   (per-user override)
    //   2. <plugin install dir>\TCNewPlus.ini (system-wide; ships with the plugin)
    const std::wstring userIni = UserConfigPath();
    const std::wstring ini =
        (!userIni.empty() &&
         GetFileAttributesW(userIni.c_str()) != INVALID_FILE_ATTRIBUTES)
            ? userIni
            : dir + L"\\TCNewPlus.ini";

    wchar_t value[4096] = L"";
    GetPrivateProfileStringW(L"TCNewPlus", L"TemplatesPath", L"",
                             value, ARRAYSIZE(value), ini.c_str());
    cfg.templatesPath = value[0] ? ExpandEnvironment(value)
                                 : dir + L"\\Templates";

    cfg.rules.replaceVariables =
        GetPrivateProfileIntW(L"TCNewPlus", L"ReplaceVariables", 1, ini.c_str()) != 0;
    cfg.rules.hideStartingDigits =
        GetPrivateProfileIntW(L"TCNewPlus", L"HideStartingDigits", 1, ini.c_str()) != 0;

    wchar_t exts[2048] = L"";
    GetPrivateProfileStringW(L"TCNewPlus", L"ExpandInFileExtensions", L"",
                             exts, ARRAYSIZE(exts), ini.c_str());
    cfg.expandExtensions = ParseExtensionList(exts);

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

static bool EnumerateVirtualDir(const std::wstring& virtualDir,
                                const Config&       cfg,
                                std::vector<VirtualEntry>& out);

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
    if (hFind == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND; // empty directory

    // One time snapshot per directory listing so all names in the panel
    // carry the same timestamp.
    SYSTEMTIME now;
    GetLocalTime(&now);

    // $PARENT_FOLDER_NAME inside a template subfolder expands to the
    // (already substituted) display name of that subfolder; empty at root.
    const std::wstring parentName =
        (virtualDir == L"\\") ? std::wstring() : VirtualLeaf(virtualDir);

    std::set<std::wstring, CaseInsensitiveLess> usedNames;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring display =
            MakeDisplayName(fd.cFileName, cfg.rules, now, parentName);

        // Two templates can collapse to the same display name (e.g. "1. A"
        // and "2. A") — disambiguate with " (2)" before the extension.
        if (usedNames.count(display)) {
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
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

        VirtualEntry entry;
        entry.fd = fd;
        wcsncpy_s(entry.fd.cFileName, display.c_str(), _TRUNCATE);
        entry.fd.cAlternateFileName[0] = L'\0';
        // The copy TC creates must be writable even if the template is
        // marked read-only on disk — New+ copies are ordinary new files.
        entry.fd.dwFileAttributes &= ~FILE_ATTRIBUTE_READONLY;
        entry.realPath = realDir + L"\\" + fd.cFileName;

        {
            std::lock_guard<std::mutex> lock(g_mapMutex);
            g_virtualToReal[JoinVirtual(virtualDir, display)] = entry.realPath;
        }

        out.push_back(std::move(entry));
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
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
        SetLastError(ERROR_PATH_NOT_FOUND);
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

static void StampNow(HANDLE hFile)
{
    // "New document" semantics: the copy is a brand-new file created now, not
    // a clone of the template's timestamps.
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(hFile, &now, &now, &now);
}

// Copies `realPath` to `localName` while expanding New+ variables in its
// (UTF-8) text content. A leading UTF-8 byte-order mark is preserved.
// Variables are expanded WITHOUT filename sanitisation — characters like
// ':' or '\' from %USERPROFILE% stay intact in the content. Returns an
// FS_FILE_* code, or FS_RAW_FALLBACK when the content is not valid UTF-8.
static int CopyExpanded(const std::wstring& realPath,
                        const std::wstring& localName,
                        const std::wstring& parentName)
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

    SYSTEMTIME now;
    GetLocalTime(&now);
    std::wstring expanded = ReplaceTemplateVariables(wide, now, parentName, false);

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
    StampNow(hDst);
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

    const Config cfg = LoadConfig();
    const std::wstring vpath = NormalizeVirtualPath(RemoteName);
    const std::wstring realPath = ResolveRealPath(vpath, cfg);
    if (realPath.empty())
        return FS_FILE_NOTFOUND;

    if (GetFileAttributesW(LocalName) != INVALID_FILE_ATTRIBUTES &&
        !(CopyFlags & FS_COPYFLAGS_OVERWRITE))
        return FS_FILE_EXISTS;

    if (ProgressAborted(RemoteName, LocalName, 0))
        return FS_FILE_USERABORT;

    // Expand New+ variables inside the content for configured extensions.
    // $PARENT_FOLDER_NAME uses the (already substituted) display name of the
    // immediate parent folder; empty at the plugin root.
    if (cfg.expandExtensions.count(FileExtension(vpath))) {
        const std::wstring parentDir = VirtualParent(vpath);
        const std::wstring parentName =
            (parentDir == L"\\") ? std::wstring() : VirtualLeaf(parentDir);
        int r = CopyExpanded(realPath, LocalName, parentName);
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
        StampNow(hDst);

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
        // Let TC download the file to a temporary location and open it there.
        return FS_EXEC_YOURSELF;
    }

    if (_wcsicmp(Verb, L"properties") == 0) {
        const std::wstring vpath = NormalizeVirtualPath(RemoteName);
        const Config cfg = LoadConfig();
        const std::wstring realPath = ResolveRealPath(vpath, cfg);

        std::wstring text = L"Virtual name:\n" + vpath +
                            L"\n\nTemplate source:\n" +
                            (realPath.empty() ? L"(not found)" : realPath) +
                            L"\n\nTemplates folder:\n" + cfg.templatesPath;
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
    std::wstring remote = AnsiToWide(RemoteName);
    std::wstring verb   = AnsiToWide(Verb);
    return FsExecuteFileW(MainWin, &remote[0], &verb[0]);
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

void __stdcall FsSetDefaultParams(FsDefaultParamStruct* /*dps*/)
{
    // Configuration deliberately lives next to the plugin DLL (TCNewPlus.ini),
    // not in TC's default INI location — nothing to pick up here.
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
