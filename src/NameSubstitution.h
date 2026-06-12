// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Michal Růžička <ruzicka.mich@gmail.com>

/*
 * NameSubstitution.h - PowerToys New+ filename rules, reimplemented.
 *
 * Pure string transformations with no plugin or filesystem dependencies, so
 * the same code is linked into both the plugin DLL and the test executable.
 *
 * Rules implemented (see https://learn.microsoft.com/windows/powertoys/newplus):
 *
 *  1. Sorting-prefix removal — leading digits, spaces and dots that template
 *     authors use to control menu ordering ("1. Letter.docx" -> "Letter.docx").
 *
 *  2. Variable replacement in the filename:
 *       - date/time patterns ($YYYY, $MM, $DD, $hh, $mm, $ss, $fff, $MMMM,
 *         $DDD, ... — case-sensitive, same patterns as PowerRename),
 *       - $PARENT_FOLDER_NAME (case-sensitive; meaningful only for files
 *         inside template subfolders),
 *       - %ENV_VAR% environment variables (case-insensitive).
 *     Filesystem-invalid characters in substituted values are replaced with
 *     spaces, exactly as New+ does.
 */

#pragma once

#include <windows.h>
#include <string>

// Display/creation rules; both default to the plugin's defaults.
struct NameRules {
    bool replaceVariables   = true;
    bool hideStartingDigits = true;
};

// Removes the leading sorting prefix (digits, spaces and dots). If stripping
// would consume the entire name, the original name is returned unchanged.
std::wstring StripSortingPrefix(const std::wstring& name);

// Replaces all New+ variables in `name` using the given local time snapshot.
// `parentFolderName` substitutes $PARENT_FOLDER_NAME; when it is empty (root
// level — no template subfolder above), the token is left in place verbatim,
// matching the New+ documentation ("only works in template subfolders").
// Substituted values have filesystem-invalid characters replaced by spaces.
std::wstring ReplaceTemplateVariables(const std::wstring& name,
                                      const SYSTEMTIME&   localTime,
                                      const std::wstring& parentFolderName);

// Full pipeline: optional prefix strip, optional variable replacement, and a
// final trim of leading/trailing spaces and trailing dots (Windows filenames
// cannot end with either; substitution can introduce them). Falls back to the
// original template name if the result would be empty.
std::wstring MakeDisplayName(const std::wstring& templateName,
                             const NameRules&    rules,
                             const SYSTEMTIME&   localTime,
                             const std::wstring& parentFolderName);
