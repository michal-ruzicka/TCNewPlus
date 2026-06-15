// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Michal Růžička <ruzicka.mich@gmail.com>

#include "NameSubstitution.h"

namespace {

// Characters that cannot appear in a Windows filename. New+ replaces them
// with spaces in substituted values; we do the same.
bool IsInvalidFilenameChar(wchar_t c)
{
    switch (c) {
        case L'\\': case L'/': case L':': case L'*':
        case L'?':  case L'"': case L'<': case L'>': case L'|':
            return true;
        default:
            return c < 0x20; // control characters
    }
}

std::wstring Sanitize(const std::wstring& value)
{
    std::wstring out(value);
    for (wchar_t& c : out)
        if (IsInvalidFilenameChar(c))
            c = L' ';
    return out;
}

std::wstring PadNumber(unsigned value, int width)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%0*u", width, value);
    return buf;
}

// Formats the date with a single locale picture ("MMMM", "ddd", ...) using
// the user's default locale — the same names New+ / PowerRename produce.
std::wstring FormatDatePicture(const SYSTEMTIME& st, const wchar_t* picture)
{
    wchar_t buf[128];
    int n = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, picture,
                            buf, ARRAYSIZE(buf), nullptr);
    return (n > 0) ? std::wstring(buf, n - 1) : std::wstring();
}

// Resolves one date/time token (the part after '$'). Tokens are checked
// longest-first so e.g. "$DDDD" is not consumed as "$DD" + "DD".
// Returns the number of characters consumed from `tail`, 0 when no token
// matches.
size_t ResolveDateToken(const std::wstring& tail, const SYSTEMTIME& st,
                        std::wstring& value)
{
    struct Token { const wchar_t* pattern; };

    auto starts = [&tail](const wchar_t* p) {
        return tail.compare(0, wcslen(p), p) == 0;
    };

    if (starts(L"YYYY")) { value = PadNumber(st.wYear, 4);                return 4; }
    if (starts(L"YY"))   { value = PadNumber(st.wYear % 100, 2);          return 2; }
    if (starts(L"Y"))    { value = PadNumber(st.wYear % 10, 1);           return 1; }
    if (starts(L"MMMM")) { value = FormatDatePicture(st, L"MMMM");        return 4; }
    if (starts(L"MMM"))  { value = FormatDatePicture(st, L"MMM");         return 3; }
    if (starts(L"MM"))   { value = PadNumber(st.wMonth, 2);               return 2; }
    if (starts(L"M"))    { value = PadNumber(st.wMonth, 1);               return 1; }
    if (starts(L"DDDD")) { value = FormatDatePicture(st, L"dddd");        return 4; }
    if (starts(L"DDD"))  { value = FormatDatePicture(st, L"ddd");         return 3; }
    if (starts(L"DD"))   { value = PadNumber(st.wDay, 2);                 return 2; }
    if (starts(L"D"))    { value = PadNumber(st.wDay, 1);                 return 1; }
    if (starts(L"hh"))   { value = PadNumber(st.wHour, 2);                return 2; }
    if (starts(L"h"))    { value = PadNumber(st.wHour, 1);                return 1; }
    if (starts(L"mm"))   { value = PadNumber(st.wMinute, 2);              return 2; }
    if (starts(L"m"))    { value = PadNumber(st.wMinute, 1);              return 1; }
    if (starts(L"ss"))   { value = PadNumber(st.wSecond, 2);              return 2; }
    if (starts(L"s"))    { value = PadNumber(st.wSecond, 1);              return 1; }
    if (starts(L"fff"))  { value = PadNumber(st.wMilliseconds, 3);        return 3; }
    if (starts(L"ff"))   { value = PadNumber(st.wMilliseconds / 10, 2);   return 2; }
    if (starts(L"f"))    { value = PadNumber(st.wMilliseconds / 100, 1);  return 1; }

    return 0;
}

} // namespace

std::wstring StripSortingPrefix(const std::wstring& name)
{
    size_t pos = 0;
    while (pos < name.size() &&
           ((name[pos] >= L'0' && name[pos] <= L'9') ||
            name[pos] == L' ' || name[pos] == L'.'))
        ++pos;

    // A name consisting solely of digits, spaces and dots has nothing left
    // after stripping — keep it as-is rather than producing an empty name.
    if (pos == 0 || pos >= name.size())
        return name;

    return name.substr(pos);
}

std::wstring ReplaceTemplateVariables(const std::wstring& name,
                                      const SYSTEMTIME&   localTime,
                                      const std::wstring& parentFolderName,
                                      bool                sanitizeInvalidChars)
{
    static const std::wstring kParentToken = L"PARENT_FOLDER_NAME";

    auto maybeSanitize = [sanitizeInvalidChars](const std::wstring& v) {
        return sanitizeInvalidChars ? Sanitize(v) : v;
    };

    std::wstring out;
    out.reserve(name.size() + 16);

    size_t i = 0;
    while (i < name.size()) {
        wchar_t c = name[i];

        if (c == L'$') {
            std::wstring tail = name.substr(i + 1);

            // $PARENT_FOLDER_NAME — checked before date tokens because it
            // also starts with characters a date token could match.
            if (tail.compare(0, kParentToken.size(), kParentToken) == 0) {
                if (!parentFolderName.empty()) {
                    out += maybeSanitize(parentFolderName);
                } else {
                    // Root level: New+ documents the variable as working
                    // "only in template subfolders" — keep the token.
                    out += L'$';
                    out += kParentToken;
                }
                i += 1 + kParentToken.size();
                continue;
            }

            std::wstring value;
            size_t consumed = ResolveDateToken(tail, localTime, value);
            if (consumed > 0) {
                out += maybeSanitize(value);
                i += 1 + consumed;
                continue;
            }

            out += L'$'; // no token matched — '$' is literal
            ++i;
            continue;
        }

        if (c == L'%') {
            // %ENV_VAR% — case-insensitive, same semantics as
            // ExpandEnvironmentStrings: an undefined variable is left in the
            // name verbatim (including both percent signs).
            size_t close = name.find(L'%', i + 1);
            if (close != std::wstring::npos && close > i + 1) {
                std::wstring var = name.substr(i + 1, close - i - 1);
                DWORD needed = GetEnvironmentVariableW(var.c_str(), nullptr, 0);
                if (needed > 0) {
                    std::wstring value(needed, L'\0');
                    DWORD written = GetEnvironmentVariableW(var.c_str(),
                                                            &value[0], needed);
                    value.resize(written);
                    out += maybeSanitize(value);
                } else {
                    out += name.substr(i, close - i + 1);
                }
                i = close + 1;
                continue;
            }

            out += L'%'; // no closing '%' (or "%%") — literal
            ++i;
            continue;
        }

        out += c;
        ++i;
    }

    return out;
}

std::wstring MakeDisplayName(const std::wstring& templateName,
                             const NameRules&    rules,
                             const SYSTEMTIME&   localTime,
                             const std::wstring& parentFolderName)
{
    std::wstring name = templateName;

    if (rules.hideStartingDigits)
        name = StripSortingPrefix(name);

    if (rules.replaceVariables)
        name = ReplaceTemplateVariables(name, localTime, parentFolderName);

    // Windows rejects filenames ending with a space or a dot; substitution
    // (sanitised values, removed tokens) can leave them behind.
    size_t begin = name.find_first_not_of(L' ');
    size_t end   = name.find_last_not_of(L" .");
    name = (begin == std::wstring::npos || end == std::wstring::npos)
               ? std::wstring()
               : name.substr(begin, end - begin + 1);

    return name.empty() ? templateName : name;
}
