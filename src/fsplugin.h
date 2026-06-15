// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Michal Růžička <ruzicka.mich@gmail.com>

/*
 * fsplugin.h - Total Commander File System plugin (WFX) API.
 *
 * Subset of the official fsplugin interface (https://www.ghisler.com/plugins.htm,
 * "FS-Plugin writer's guide") covering the functions this plugin implements:
 * read-only browsing (FsFindFirst/Next/Close) and downloading (FsGetFile),
 * in both ANSI and Unicode variants. Write operations (FsPutFile, FsMkDir,
 * FsDeleteFile, ...) are intentionally absent — not exporting them is how a
 * WFX plugin declares itself read-only to Total Commander.
 */

#pragma once

#include <windows.h>

// ---------------------------------------------------------------------------
// Return codes for FsGetFile / FsPutFile / FsRenMovFile
// ---------------------------------------------------------------------------

#define FS_FILE_OK                  0   // operation completed successfully
#define FS_FILE_EXISTS              1   // target file already exists
#define FS_FILE_NOTFOUND            2   // source file not found
#define FS_FILE_READERROR           3   // error reading from the source
#define FS_FILE_WRITEERROR          4   // error writing to the target
#define FS_FILE_USERABORT           5   // user aborted via the progress dialog
#define FS_FILE_NOTSUPPORTED        6   // operation not supported by the plugin
#define FS_FILE_EXISTSRESUMEALLOWED 7   // target exists, resume is possible

// ---------------------------------------------------------------------------
// Return codes for FsExecuteFile
// ---------------------------------------------------------------------------

#define FS_EXEC_OK        0    // command executed successfully
#define FS_EXEC_ERROR     1    // execution failed
#define FS_EXEC_YOURSELF  (-1) // TC should download the file and execute it locally
#define FS_EXEC_SYMLINK   (-2) // RemoteName points to a different directory

// ---------------------------------------------------------------------------
// CopyFlags passed to FsGetFile / FsPutFile
// ---------------------------------------------------------------------------

#define FS_COPYFLAGS_OVERWRITE             1  // overwrite the target if it exists
#define FS_COPYFLAGS_RESUME                2  // resume an interrupted transfer
#define FS_COPYFLAGS_MOVE                  4  // delete the remote file after a successful copy
#define FS_COPYFLAGS_EXISTS_SAMECASE       8  // target exists with identical case
#define FS_COPYFLAGS_EXISTS_DIFFERENTCASE 16  // target exists with different case

// ---------------------------------------------------------------------------
// FsExtractCustomIcon flags and return codes
// ---------------------------------------------------------------------------

#define FS_ICONFLAG_SMALL          1  // the small 16x16 icon is requested
#define FS_ICONFLAG_BACKGROUND     2  // called from TC's background thread

#define FS_ICON_USEDEFAULT         0  // no icon returned; TC shows the default
#define FS_ICON_EXTRACTED          1  // icon returned; TC must NOT destroy it
#define FS_ICON_EXTRACTED_DESTROY  2  // icon returned; TC must DestroyIcon it
#define FS_ICON_DELAYED            3  // extract later on the background thread

// ---------------------------------------------------------------------------
// FsStatusInfo operation start/end markers and operation codes
// ---------------------------------------------------------------------------

#define FS_STATUS_START 0
#define FS_STATUS_END   1

#define FS_STATUS_OP_LIST             1
#define FS_STATUS_OP_GET_SINGLE       2
#define FS_STATUS_OP_GET_MULTI        3
#define FS_STATUS_OP_PUT_SINGLE       4
#define FS_STATUS_OP_PUT_MULTI        5
#define FS_STATUS_OP_RENMOV_SINGLE    6
#define FS_STATUS_OP_RENMOV_MULTI     7
#define FS_STATUS_OP_DELETE           8
#define FS_STATUS_OP_ATTRIB           9
#define FS_STATUS_OP_MKDIR           10
#define FS_STATUS_OP_EXEC            11
#define FS_STATUS_OP_CALCSIZE        12
#define FS_STATUS_OP_SEARCH          13
#define FS_STATUS_OP_SEARCH_TEXT     14
#define FS_STATUS_OP_SYNC_SEARCH     15
#define FS_STATUS_OP_SYNC_GET        16
#define FS_STATUS_OP_SYNC_PUT        17
#define FS_STATUS_OP_SYNC_DELETE     18

// ---------------------------------------------------------------------------
// Structures
// ---------------------------------------------------------------------------

// Extra information about the remote file, passed by TC to FsGetFile.
typedef struct {
    DWORD    SizeLow;
    DWORD    SizeHigh;
    FILETIME LastWriteTime;
    DWORD    Attr;
} RemoteInfoStruct;

// Passed by TC to FsSetDefaultParams right after loading the plugin.
typedef struct {
    int   size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char  DefaultIniName[MAX_PATH];
} FsDefaultParamStruct;

// ---------------------------------------------------------------------------
// Callback functions provided by Total Commander via FsInit / FsInitW
// ---------------------------------------------------------------------------

// Progress callback: return value 1 means the user wants to abort.
typedef int  (__stdcall *tProgressProc) (int PluginNr, char*  SourceName, char*  TargetName, int PercentDone);
typedef int  (__stdcall *tProgressProcW)(int PluginNr, WCHAR* SourceName, WCHAR* TargetName, int PercentDone);

typedef void (__stdcall *tLogProc) (int PluginNr, int MsgType, char*  LogString);
typedef void (__stdcall *tLogProcW)(int PluginNr, int MsgType, WCHAR* LogString);

typedef BOOL (__stdcall *tRequestProc) (int PluginNr, int RequestType, char*  CustomTitle, char*  CustomText, char*  ReturnedText, int maxlen);
typedef BOOL (__stdcall *tRequestProcW)(int PluginNr, int RequestType, WCHAR* CustomTitle, WCHAR* CustomText, WCHAR* ReturnedText, int maxlen);

// ---------------------------------------------------------------------------
// Exported plugin functions (implemented in TCNewPlus.cpp)
// ---------------------------------------------------------------------------

extern "C" {

int    __stdcall FsInit (int PluginNr, tProgressProc  pProgressProc,  tLogProc  pLogProc,  tRequestProc  pRequestProc);
int    __stdcall FsInitW(int PluginNr, tProgressProcW pProgressProcW, tLogProcW pLogProcW, tRequestProcW pRequestProcW);

HANDLE __stdcall FsFindFirst (char*  Path, WIN32_FIND_DATAA* FindData);
HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData);
BOOL   __stdcall FsFindNext (HANDLE Hdl, WIN32_FIND_DATAA* FindData);
BOOL   __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData);
int    __stdcall FsFindClose(HANDLE Hdl);

int    __stdcall FsGetFile (char*  RemoteName, char*  LocalName, int CopyFlags, RemoteInfoStruct* ri);
int    __stdcall FsGetFileW(WCHAR* RemoteName, WCHAR* LocalName, int CopyFlags, RemoteInfoStruct* ri);

int    __stdcall FsExecuteFile (HWND MainWin, char*  RemoteName, char*  Verb);
int    __stdcall FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb);

int    __stdcall FsExtractCustomIcon (char*  RemoteName, int ExtractFlags, HICON* TheIcon);
int    __stdcall FsExtractCustomIconW(WCHAR* RemoteName, int ExtractFlags, HICON* TheIcon);

void   __stdcall FsGetDefRootName(char* DefRootName, int maxlen);
void   __stdcall FsSetDefaultParams(FsDefaultParamStruct* dps);

} // extern "C"
