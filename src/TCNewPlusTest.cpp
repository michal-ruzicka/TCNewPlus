// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Michal Růžička <ruzicka.mich@gmail.com>

/*
 * TCNewPlusTest.cpp - test driver for the TCNewPlus plugin.
 *
 * Two layers:
 *
 *   1. Unit tests of the naming rules (NameSubstitution.cpp is linked into
 *      this executable directly) with a fixed time snapshot.
 *
 *   2. End-to-end smoke test: loads the built plugin DLL via LoadLibrary,
 *      points it at a temporary template tree through TCNewPlus.ini written
 *      next to the DLL, and exercises the WFX API the same way Total
 *      Commander does — FsInitW, FsFindFirstW/FsFindNextW/FsFindClose,
 *      FsGetFileW.
 *
 * Usage:
 *      TCNewPlusTest.exe <path-to-TCNewPlus.wfx64-or-wfx>
 *
 * Without an argument only the unit tests run. Exit code 0 = all passed.
 */

#include "fsplugin.h"
#include "NameSubstitution.h"

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

static void CheckEq(const std::wstring& actual, const std::wstring& expected,
                    const char* what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n  expected: %ls\n  actual:   %ls\n",
                     what, expected.c_str(), actual.c_str());
    }
}

// ---------------------------------------------------------------------------
// Unit tests: naming rules
// ---------------------------------------------------------------------------

static SYSTEMTIME FixedTime()
{
    SYSTEMTIME st = {};
    st.wYear         = 2026;
    st.wMonth        = 6;
    st.wDay          = 13;   // a Saturday
    st.wDayOfWeek    = 6;
    st.wHour         = 7;
    st.wMinute       = 5;
    st.wSecond       = 9;
    st.wMilliseconds = 42;
    return st;
}

static std::wstring DatePicture(const SYSTEMTIME& st, const wchar_t* picture)
{
    wchar_t buf[128];
    int n = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, picture,
                            buf, ARRAYSIZE(buf), nullptr);
    return (n > 0) ? std::wstring(buf, n - 1) : std::wstring();
}

static void TestStripSortingPrefix()
{
    CheckEq(StripSortingPrefix(L"1. Letter.docx"), L"Letter.docx",
            "strip '1. ' prefix");
    CheckEq(StripSortingPrefix(L"  2 Foo"), L"Foo",
            "strip spaces and digits");
    CheckEq(StripSortingPrefix(L"12.34. Report.txt"), L"Report.txt",
            "strip multi-digit dotted prefix");
    CheckEq(StripSortingPrefix(L"Letter.docx"), L"Letter.docx",
            "no prefix, unchanged");
    CheckEq(StripSortingPrefix(L"123"), L"123",
            "all-prefix name kept as-is");
    CheckEq(StripSortingPrefix(L""), L"",
            "empty name");
}

static void TestDateTimeVariables()
{
    const SYSTEMTIME st = FixedTime();
    const std::wstring noParent;

    CheckEq(ReplaceTemplateVariables(L"$YYYY-$MM-$DD", st, noParent),
            L"2026-06-13", "padded date");
    CheckEq(ReplaceTemplateVariables(L"$YY $Y", st, noParent),
            L"26 6", "short year forms");
    CheckEq(ReplaceTemplateVariables(L"$M/$D", st, noParent),
            L"6/13", "unpadded month and day");
    CheckEq(ReplaceTemplateVariables(L"$hh.$mm.$ss", st, noParent),
            L"07.05.09", "padded time");
    CheckEq(ReplaceTemplateVariables(L"$h $m $s", st, noParent),
            L"7 5 9", "unpadded time");
    CheckEq(ReplaceTemplateVariables(L"$fff $ff $f", st, noParent),
            L"042 04 0", "millisecond forms");
    CheckEq(ReplaceTemplateVariables(L"$MMMM", st, noParent),
            DatePicture(st, L"MMMM"), "month name (locale)");
    CheckEq(ReplaceTemplateVariables(L"$MMM", st, noParent),
            DatePicture(st, L"MMM"), "abbreviated month name (locale)");
    CheckEq(ReplaceTemplateVariables(L"$DDDD", st, noParent),
            DatePicture(st, L"dddd"), "day-of-week name (locale)");
    CheckEq(ReplaceTemplateVariables(L"$DDD", st, noParent),
            DatePicture(st, L"ddd"), "abbreviated day-of-week name (locale)");
    CheckEq(ReplaceTemplateVariables(L"$Q $", st, noParent),
            L"$Q $", "unknown token and trailing $ stay literal");
}

static void TestSpecialAndEnvironmentVariables()
{
    const SYSTEMTIME st = FixedTime();

    CheckEq(ReplaceTemplateVariables(L"README of $PARENT_FOLDER_NAME.md", st,
                                     L"Project X"),
            L"README of Project X.md", "$PARENT_FOLDER_NAME in subfolder");
    CheckEq(ReplaceTemplateVariables(L"$PARENT_FOLDER_NAME.md", st, L""),
            L"$PARENT_FOLDER_NAME.md", "$PARENT_FOLDER_NAME at root stays");

    SetEnvironmentVariableW(L"TCNP_TEST_VALUE", L"plain");
    CheckEq(ReplaceTemplateVariables(L"by %TCNP_TEST_VALUE%", st, L""),
            L"by plain", "environment variable");
    CheckEq(ReplaceTemplateVariables(L"by %tcnp_test_value%", st, L""),
            L"by plain", "environment variable is case-insensitive");

    SetEnvironmentVariableW(L"TCNP_TEST_INVALID", L"a<b>c:d");
    CheckEq(ReplaceTemplateVariables(L"%TCNP_TEST_INVALID%", st, L""),
            L"a b c d", "invalid filename chars become spaces");
    CheckEq(ReplaceTemplateVariables(L"%TCNP_TEST_INVALID%", st, L"", false),
            L"a<b>c:d", "content mode keeps filename-invalid chars verbatim");
    CheckEq(ReplaceTemplateVariables(L"$PARENT_FOLDER_NAME", st, L"a:b", false),
            L"a:b", "content mode keeps invalid chars in $PARENT_FOLDER_NAME");

    CheckEq(ReplaceTemplateVariables(L"%TCNP_TEST_UNDEFINED% x", st, L""),
            L"%TCNP_TEST_UNDEFINED% x", "undefined variable stays verbatim");
    CheckEq(ReplaceTemplateVariables(L"100% done", st, L""),
            L"100% done", "lone percent sign stays literal");

    SetEnvironmentVariableW(L"TCNP_TEST_VALUE", nullptr);
    SetEnvironmentVariableW(L"TCNP_TEST_INVALID", nullptr);
}

static void TestMakeDisplayName()
{
    const SYSTEMTIME st = FixedTime();
    NameRules rules; // defaults: strip prefix, replace variables

    CheckEq(MakeDisplayName(L"1. Note $YYYY-$MM-$DD.txt", rules, st, L""),
            L"Note 2026-06-13.txt", "full pipeline");

    NameRules keepPrefix;
    keepPrefix.hideStartingDigits = false;
    CheckEq(MakeDisplayName(L"1. Note $YYYY.txt", keepPrefix, st, L""),
            L"1. Note 2026.txt", "prefix kept when configured");

    NameRules verbatim;
    verbatim.hideStartingDigits = false;
    verbatim.replaceVariables   = false;
    CheckEq(MakeDisplayName(L"1. Note $YYYY.txt", verbatim, st, L""),
            L"1. Note $YYYY.txt", "verbatim when both rules disabled");

    SetEnvironmentVariableW(L"TCNP_TEST_TRAIL", L"x|");
    CheckEq(MakeDisplayName(L"Name %TCNP_TEST_TRAIL%", rules, st, L""),
            L"Name x", "trailing sanitised space trimmed");
    SetEnvironmentVariableW(L"TCNP_TEST_TRAIL", nullptr);
}

// ---------------------------------------------------------------------------
// End-to-end test through the plugin DLL
// ---------------------------------------------------------------------------

using PFsInitW           = int    (__stdcall*)(int, tProgressProcW, tLogProcW, tRequestProcW);
using PFsFindFirstW      = HANDLE (__stdcall*)(WCHAR*, WIN32_FIND_DATAW*);
using PFsFindNextW       = BOOL   (__stdcall*)(HANDLE, WIN32_FIND_DATAW*);
using PFsFindClose       = int    (__stdcall*)(HANDLE);
using PFsGetFileW        = int    (__stdcall*)(WCHAR*, WCHAR*, int, RemoteInfoStruct*);
using PFsExtractIconW    = int    (__stdcall*)(WCHAR*, int, HICON*);
using PFsExecuteW        = int    (__stdcall*)(HWND, WCHAR*, WCHAR*);

static int __stdcall TestProgress(int, WCHAR*, WCHAR*, int)
{
    return 0; // never abort
}

static void WriteTextFile(const std::wstring& path, const char* content)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "create test fixture file");
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, nullptr);
        CloseHandle(h);
    }
}

// Writes a UTF-8 file (no BOM) from a wide string — the encoding a modern
// text editor produces for an INI. Used to verify the plugin reads non-ASCII
// config values from a UTF-8 INI rather than misreading them as ANSI.
static void WriteUtf8File(const std::wstring& path, const std::wstring& wcontent)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, wcontent.data(),
                                static_cast<int>(wcontent.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string bytes(n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, wcontent.data(),
                            static_cast<int>(wcontent.size()),
                            &bytes[0], n, nullptr, nullptr);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(h);
    }
}

static std::string ReadTextFile(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    char buf[4096];
    DWORD read = 0;
    ReadFile(h, buf, sizeof(buf), &read, nullptr);
    CloseHandle(h);
    return std::string(buf, read);
}

static std::vector<WIN32_FIND_DATAW> ListVirtualDir(PFsFindFirstW findFirst,
                                                    PFsFindNextW  findNext,
                                                    PFsFindClose  findClose,
                                                    std::wstring  path)
{
    std::vector<WIN32_FIND_DATAW> entries;
    WIN32_FIND_DATAW fd;
    HANDLE h = findFirst(&path[0], &fd);
    if (h == INVALID_HANDLE_VALUE)
        return entries;
    entries.push_back(fd);
    while (findNext(h, &fd))
        entries.push_back(fd);
    findClose(h);
    return entries;
}

static bool ContainsName(const std::vector<WIN32_FIND_DATAW>& entries,
                         const std::wstring& name, bool expectDirectory = false)
{
    for (const auto& fd : entries) {
        if (name == fd.cFileName) {
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            return isDir == expectDirectory;
        }
    }
    return false;
}

static void RunEndToEndTests(const wchar_t* dllPath)
{
    // --- Fixture: temporary template tree ---------------------------------
    wchar_t tempRoot[MAX_PATH];
    GetTempPathW(ARRAYSIZE(tempRoot), tempRoot);
    std::wstring fixture = std::wstring(tempRoot) + L"TCNewPlusTest-" +
                           std::to_wstring(GetCurrentProcessId());
    std::wstring templates = fixture + L"\\Templates";
    std::wstring outDir    = fixture + L"\\Out";
    CreateDirectoryW(fixture.c_str(), nullptr);
    CreateDirectoryW(templates.c_str(), nullptr);
    CreateDirectoryW(outDir.c_str(), nullptr);
    CreateDirectoryW((templates + L"\\3. Proj $YYYY").c_str(), nullptr);

    WriteTextFile(templates + L"\\1. Plain.txt", "Hello from template");
    WriteTextFile(templates + L"\\2. Note $YYYY.txt", "note");
    WriteTextFile(templates + L"\\4. Same.txt", "first");
    WriteTextFile(templates + L"\\5. Same.txt", "second");
    WriteTextFile(templates + L"\\6. Vars $YYYY.md",
                  "Y=$YYYY U=%TCNP_TEST_CONTENT% P=$PARENT_FOLDER_NAME");
    WriteTextFile(templates + L"\\3. Proj $YYYY\\README of $PARENT_FOLDER_NAME.md",
                  "P=$PARENT_FOLDER_NAME");

    // The plugin prefers a per-user INI in %APPDATA%\GHISLER over the one
    // next to the DLL. Move any real per-user INI aside for the duration of
    // the test so this fixture's plugin-dir INI is the one that wins, then
    // restore it afterwards. (On CI / a fresh machine there is none.)
    std::wstring userIni;
    {
        wchar_t appdata[MAX_PATH] = L"";
        if (GetEnvironmentVariableW(L"APPDATA", appdata, ARRAYSIZE(appdata)))
            userIni = std::wstring(appdata) + L"\\GHISLER\\TCNewPlus.ini";
    }
    std::wstring userIniBak = userIni + L".tcnptest-bak";
    bool userIniMoved = false;
    if (!userIni.empty() &&
        GetFileAttributesW(userIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
        userIniMoved = MoveFileExW(userIni.c_str(), userIniBak.c_str(),
                                   MOVEFILE_REPLACE_EXISTING) != FALSE;
    }

    // --- Point the plugin at the fixture via INI next to the DLL ----------
    std::wstring dllDir = dllPath;
    size_t slash = dllDir.find_last_of(L"\\/");
    dllDir = (slash == std::wstring::npos) ? L"." : dllDir.substr(0, slash);
    std::wstring ini = dllDir + L"\\TCNewPlus.ini";

    WritePrivateProfileStringW(L"Templates", L"TemplatesPath",
                               templates.c_str(), ini.c_str());

    // --- Load the plugin through the WFX API ------------------------------
    HMODULE dll = LoadLibraryW(dllPath);
    Check(dll != nullptr, "LoadLibrary on the plugin DLL");
    if (!dll)
        return;

    auto fsInitW      = reinterpret_cast<PFsInitW>(GetProcAddress(dll, "FsInitW"));
    auto fsFindFirstW = reinterpret_cast<PFsFindFirstW>(GetProcAddress(dll, "FsFindFirstW"));
    auto fsFindNextW  = reinterpret_cast<PFsFindNextW>(GetProcAddress(dll, "FsFindNextW"));
    auto fsFindClose  = reinterpret_cast<PFsFindClose>(GetProcAddress(dll, "FsFindClose"));
    auto fsGetFileW   = reinterpret_cast<PFsGetFileW>(GetProcAddress(dll, "FsGetFileW"));
    auto fsIconW      = reinterpret_cast<PFsExtractIconW>(GetProcAddress(dll, "FsExtractCustomIconW"));
    auto fsExecW      = reinterpret_cast<PFsExecuteW>(GetProcAddress(dll, "FsExecuteFileW"));

    Check(fsInitW && fsFindFirstW && fsFindNextW && fsFindClose && fsGetFileW && fsIconW && fsExecW,
          "all WFX entry points exported");
    if (!(fsInitW && fsFindFirstW && fsFindNextW && fsFindClose && fsGetFileW && fsIconW && fsExecW)) {
        FreeLibrary(dll);
        return;
    }

    fsInitW(1, TestProgress, nullptr, nullptr);

    // Expected names share the time snapshot risk only on a year boundary —
    // acceptable for a test that uses $YYYY exclusively.
    SYSTEMTIME now;
    GetLocalTime(&now);
    const std::wstring year = std::to_wstring(now.wYear);

    // --- Root listing ------------------------------------------------------
    auto root = ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
    Check(root.size() == 6, "root lists six entries");
    Check(ContainsName(root, L"Plain.txt"), "prefix stripped from Plain.txt");
    Check(ContainsName(root, L"Note " + year + L".txt"), "$YYYY substituted");
    Check(ContainsName(root, L"Vars " + year + L".md"), "expandable template listed");
    Check(ContainsName(root, L"Proj " + year, true), "folder template listed");
    Check(ContainsName(root, L"Same.txt"), "first colliding name kept");
    Check(ContainsName(root, L"Same (2).txt"), "second colliding name disambiguated");

    // --- Subfolder listing with $PARENT_FOLDER_NAME ------------------------
    auto sub = ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose,
                              L"\\Proj " + year);
    Check(sub.size() == 1, "subfolder lists one entry");
    Check(ContainsName(sub, L"README of Proj " + year + L".md"),
          "$PARENT_FOLDER_NAME expands to the substituted folder name");

    // --- Copy out (FsGetFileW) ---------------------------------------------
    std::wstring remote = L"\\Plain.txt";
    std::wstring local  = outDir + L"\\Plain.txt";
    int rc = fsGetFileW(&remote[0], &local[0], 0, nullptr);
    Check(rc == FS_FILE_OK, "FsGetFileW succeeds");
    Check(ReadTextFile(local) == "Hello from template",
          "copied content matches the template");

    rc = fsGetFileW(&remote[0], &local[0], 0, nullptr);
    Check(rc == FS_FILE_EXISTS, "FsGetFileW refuses to overwrite by default");

    rc = fsGetFileW(&remote[0], &local[0], FS_COPYFLAGS_OVERWRITE, nullptr);
    Check(rc == FS_FILE_OK, "FsGetFileW overwrites with FS_COPYFLAGS_OVERWRITE");

    std::wstring missing  = L"\\No Such Template.txt";
    std::wstring missing2 = outDir + L"\\No Such Template.txt";
    rc = fsGetFileW(&missing[0], &missing2[0], 0, nullptr);
    Check(rc == FS_FILE_NOTFOUND, "FsGetFileW reports a missing template");

    // Resolution without a prior listing in scope: a fresh name lookup in a
    // subfolder forces the ancestor walk in ResolveRealPath.
    std::wstring subRemote = L"\\Proj " + year + L"\\README of Proj " + year + L".md";
    std::wstring subLocal  = outDir + L"\\README.md";
    rc = fsGetFileW(&subRemote[0], &subLocal[0], 0, nullptr);
    Check(rc == FS_FILE_OK, "FsGetFileW resolves a subfolder file");
    Check(ReadTextFile(subLocal) == "P=$PARENT_FOLDER_NAME",
          "subfolder content copied verbatim when expansion is off");

    rc = fsGetFileW(&remote[0], &local[0],
                    FS_COPYFLAGS_MOVE | FS_COPYFLAGS_OVERWRITE, nullptr);
    Check(rc == FS_FILE_NOTSUPPORTED, "move (F6) is refused — read-only FS");

    // --- Custom icons (FsExtractCustomIconW) -------------------------------
    HICON icon = nullptr;
    int iconRc = fsIconW(&remote[0], FS_ICONFLAG_SMALL, &icon);
    Check(iconRc == FS_ICON_EXTRACTED_DESTROY && icon != nullptr,
          "FsExtractCustomIconW returns a destroyable icon for a template file");
    if (icon) DestroyIcon(icon);

    icon = nullptr;
    std::wstring rootPath = L"\\";
    iconRc = fsIconW(&rootPath[0], 0, &icon);
    Check(iconRc == FS_ICON_USEDEFAULT,
          "FsExtractCustomIconW defers the plugin root to the default icon");

    icon = nullptr;
    iconRc = fsIconW(&missing[0], FS_ICONFLAG_SMALL, &icon);
    Check(iconRc == FS_ICON_USEDEFAULT,
          "FsExtractCustomIconW falls back to default for a missing template");

    // --- In-content variable expansion ([FileContents] ReplaceContentVariables) ---
    const std::string yearA = std::to_string(now.wYear);
    SetEnvironmentVariableW(L"TCNP_TEST_CONTENT", L"C:\\tmp\\x");
    WritePrivateProfileStringW(L"FileContents", L"ReplaceContentVariables", L".md",
                               ini.c_str());

    std::wstring varRemote = L"\\Vars " + year + L".md";
    std::wstring varLocal  = outDir + L"\\Vars.md";
    rc = fsGetFileW(&varRemote[0], &varLocal[0], 0, nullptr);
    Check(rc == FS_FILE_OK, "FsGetFileW copies an expandable .md file");
    Check(ReadTextFile(varLocal) ==
              "Y=" + yearA + " U=C:\\tmp\\x P=$PARENT_FOLDER_NAME",
          "content variables expanded without filename sanitisation");

    std::wstring subLocal2 = outDir + L"\\README-expanded.md";
    rc = fsGetFileW(&subRemote[0], &subLocal2[0], 0, nullptr);
    Check(rc == FS_FILE_OK, "FsGetFileW copies an expandable subfolder .md file");
    Check(ReadTextFile(subLocal2) == "P=Proj " + yearA,
          "$PARENT_FOLDER_NAME expands inside subfolder content");

    std::wstring noteRemote = L"\\Note " + year + L".txt";
    std::wstring noteLocal  = outDir + L"\\Note.txt";
    rc = fsGetFileW(&noteRemote[0], &noteLocal[0], 0, nullptr);
    Check(rc == FS_FILE_OK && ReadTextFile(noteLocal) == "note",
          ".txt copied verbatim when only .md expansion is enabled");

    // Remove the key so later sections see the default (no expansion).
    WritePrivateProfileStringW(L"FileContents", L"ReplaceContentVariables", nullptr,
                               ini.c_str());
    SetEnvironmentVariableW(L"TCNP_TEST_CONTENT", nullptr);

    // --- Non-ASCII TemplatesPath read from a UTF-8 INI ---------------------
    // Regression: a templates path with non-ASCII characters (e.g. a folder
    // named "Šablóny") in a UTF-8-encoded INI must be read correctly. The old
    // GetPrivateProfileString-based reader misread BOM-less UTF-8 as ANSI.
    {
        std::wstring naDir = fixture + L"\\Šablóny"; // "Šablóny"
        CreateDirectoryW(naDir.c_str(), nullptr);
        WriteTextFile(naDir + L"\\Accented Template.txt", "x");

        std::wstring iniText = L"[Templates]\r\nTemplatesPath=" + naDir + L"\r\n";
        WriteUtf8File(ini, iniText);

        auto naListing =
            ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
        Check(ContainsName(naListing, L"Accented Template.txt"),
              "non-ASCII TemplatesPath from a UTF-8 INI is read correctly");
    }

    // A trailing backslash on TemplatesPath must not break enumeration.
    {
        std::wstring iniText =
            L"[Templates]\r\nTemplatesPath=" + templates + L"\\\r\n";
        WriteUtf8File(ini, iniText);
        auto trailing =
            ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
        Check(ContainsName(trailing, L"Plain.txt"),
              "trailing backslash on TemplatesPath is tolerated");
    }

    // --- Templates-folder link entry (ShowLinkToTemplatesDir) --------------
    {
        WriteUtf8File(ini,
            L"[Templates]\r\nTemplatesPath=" + templates + L"\r\n"
            L"ShowLinkToTemplatesDir=0. Open Templates\r\n");

        auto withLink =
            ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
        // HideStartingDigits defaults on, so "0. " is stripped. ContainsName's
        // default expects a file (not a directory) — Enter must open, not enter.
        Check(ContainsName(withLink, L"Open Templates"),
              "ShowLinkToTemplatesDir adds a root link entry (as a file)");

        // Opening the link returns FS_EXEC_SYMLINK pointing at the real folder.
        wchar_t rn[MAX_PATH];
        lstrcpynW(rn, L"\\Open Templates", MAX_PATH);
        std::wstring openVerb = L"open";
        int erc = fsExecW(nullptr, rn, &openVerb[0]);
        Check(erc == FS_EXEC_SYMLINK, "opening the link returns FS_EXEC_SYMLINK");
        Check(std::wstring(rn) == templates,
              "link redirects to the real templates folder");
    }

    // Empty value disables the link entry (default behaviour).
    {
        WriteUtf8File(ini,
            L"[Templates]\r\nTemplatesPath=" + templates + L"\r\n"
            L"ShowLinkToTemplatesDir=\r\n");
        auto noLink =
            ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
        Check(!ContainsName(noLink, L"Open Templates"),
              "empty ShowLinkToTemplatesDir shows no link entry");
        Check(ContainsName(noLink, L"Plain.txt"),
              "normal entries still listed when the link is disabled");
    }

    // --- Per-user INI precedence over the plugin-dir INI -------------------
    // A %APPDATA%\GHISLER\TCNewPlus.ini, when present, must win entirely over
    // the INI next to the DLL. Point it at a different templates folder and
    // confirm the plugin lists that one instead.
    if (!userIni.empty()) {
        std::wstring altTemplates = fixture + L"\\AltTemplates";
        CreateDirectoryW(altTemplates.c_str(), nullptr);
        WriteTextFile(altTemplates + L"\\Only In User Config.txt", "alt");

        size_t ghSlash = userIni.find_last_of(L'\\');
        std::wstring ghislerDir = userIni.substr(0, ghSlash);
        bool createdGhisler = CreateDirectoryW(ghislerDir.c_str(), nullptr) != FALSE;

        WritePrivateProfileStringW(L"Templates", L"TemplatesPath",
                                   altTemplates.c_str(), userIni.c_str());

        auto userListing =
            ListVirtualDir(fsFindFirstW, fsFindNextW, fsFindClose, L"\\");
        Check(ContainsName(userListing, L"Only In User Config.txt"),
              "per-user INI in %APPDATA%\\GHISLER takes precedence");
        Check(!ContainsName(userListing, L"Plain.txt"),
              "plugin-dir INI is ignored when the per-user INI exists");

        DeleteFileW(userIni.c_str());
        if (createdGhisler)
            RemoveDirectoryW(ghislerDir.c_str()); // no-op if not empty
    }

    FreeLibrary(dll);

    // --- Cleanup ------------------------------------------------------------
    DeleteFileW(ini.c_str());
    if (!userIni.empty())
        DeleteFileW(userIni.c_str()); // in case the precedence test left one
    if (userIniMoved)
        MoveFileExW(userIniBak.c_str(), userIni.c_str(), MOVEFILE_REPLACE_EXISTING);
    std::wstring rmCmd = L"cmd /c rmdir /s /q \"" + fixture + L"\"";
    _wsystem(rmCmd.c_str());
}

// ---------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv)
{
    TestStripSortingPrefix();
    TestDateTimeVariables();
    TestSpecialAndEnvironmentVariables();
    TestMakeDisplayName();

    if (argc > 1)
        RunEndToEndTests(argv[1]);
    else
        std::printf("No plugin DLL path given - unit tests only.\n");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
