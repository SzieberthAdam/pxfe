/* winfile.c - Win32 common dialogs. Does NOT include raylib.h. */
#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "winfile.h"

static void make_filter(char *dst, size_t cap, const char *filter)
{
    size_t n = strlen(filter);
    if (n + 2 > cap) n = cap - 2;
    memcpy(dst, filter, n);
    dst[n] = 0;
    dst[n + 1] = 0;
    for (size_t i = 0; i < n; i++) if (dst[i] == '|') dst[i] = 0;
}

static void slashes(char *s)
{
    for (; *s; s++) if (*s == 0x5C) *s = '/';
}

bool Win_OpenFileDialog(char *out, int size, const char *filter)
{
    char flt[512];
    char buf[1024] = "";
    make_filter(flt, sizeof flt, filter);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = flt;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof buf;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameA(&ofn)) return false;
    slashes(buf);
    snprintf(out, (size_t)size, "%s", buf);
    return true;
}

bool Win_SaveFileDialog(char *out, int size, const char *filter, const char *defaultPath, const char *ext)
{
    char flt[512];
    char buf[1024] = "";
    char dir[1024] = "";
    make_filter(flt, sizeof flt, filter);
    if (defaultPath && *defaultPath) {
        snprintf(buf, sizeof buf, "%s", defaultPath);
        for (char *p = buf; *p; p++) if (*p == '/') *p = 0x5C;
        char *sep = strrchr(buf, 0x5C);
        if (sep) {
            *sep = 0;
            snprintf(dir, sizeof dir, "%s", buf);
            memmove(buf, sep + 1, strlen(sep + 1) + 1);
        }
    }
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = flt;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof buf;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameA(&ofn)) return false;
    slashes(buf);
    snprintf(out, (size_t)size, "%s", buf);
    return true;
}

static int CALLBACK folder_cb(HWND h, UINT msg, LPARAM lp, LPARAM data)
{
    (void)lp; (void)data;
    (void)h; (void)msg;
    return 0;
}

/* the GUIDs of the folder picker, here so that no uuid library is needed */
static const GUID kClsidFileOpenDialog = {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const GUID kIidFileOpenDialog = {0xD57C7288, 0xD4AD, 0x4768, {0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60}};
static const GUID kIidShellItem = {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};

/* 1: picked, 0: cancelled, -1: the picker is not there */
static int pick_folder_modern(char *out, int size, const char *title, const char *start)
{
    IFileOpenDialog *d = NULL;
    if (FAILED(CoCreateInstance(&kClsidFileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &kIidFileOpenDialog, (void **)&d)) || !d) return -1;
    DWORD opts = 0;
    IFileOpenDialog_GetOptions(d, &opts);
    IFileOpenDialog_SetOptions(d, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    WCHAR w[MAX_PATH * 2];
    if (title && *title && MultiByteToWideChar(CP_ACP, 0, title, -1, w, MAX_PATH * 2)) IFileOpenDialog_SetTitle(d, w);
    if (start && *start) {
        char s[MAX_PATH * 2];
        snprintf(s, sizeof s, "%s", start);
        for (char *p = s; *p; p++) if (*p == '/') *p = 0x5C;
        DWORD a = GetFileAttributesA(s);
        IShellItem *it = NULL;
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && MultiByteToWideChar(CP_ACP, 0, s, -1, w, MAX_PATH * 2) &&
            SUCCEEDED(SHCreateItemFromParsingName(w, NULL, &kIidShellItem, (void **)&it)) && it) {
            IFileOpenDialog_SetFolder(d, it);
            IShellItem_Release(it);
        }
    }
    int rc = 0;
    if (SUCCEEDED(IFileOpenDialog_Show(d, GetActiveWindow()))) {
        IShellItem *it = NULL;
        PWSTR path = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(d, &it)) && it && SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &path)) && path) {
            char buf[MAX_PATH * 2];
            if (WideCharToMultiByte(CP_ACP, 0, path, -1, buf, sizeof buf, NULL, NULL)) {
                slashes(buf);
                snprintf(out, (size_t)size, "%s", buf);
                rc = 1;
            }
            CoTaskMemFree(path);
        }
        if (it) IShellItem_Release(it);
    }
    IFileOpenDialog_Release(d);
    return rc;
}

bool Win_PickFolder(char *out, int size, const char *title, const char *start)
{
    char buf[MAX_PATH] = "";
    CoInitialize(NULL);
    int m = pick_folder_modern(out, size, title, start);
    if (m >= 0) return m == 1;
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = GetActiveWindow();
    bi.pszDisplayName = buf;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = folder_cb;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;
    bool ok = SHGetPathFromIDListA(pidl, buf) != 0;
    CoTaskMemFree(pidl);
    if (!ok) return false;
    slashes(buf);
    snprintf(out, (size_t)size, "%s", buf);
    return true;
}

/* ---- a worker thread (main.c cannot include windows.h) ---- */
typedef struct ThreadStart { void (*fn)(void *); void *arg; } ThreadStart;

static DWORD WINAPI thread_main(LPVOID p)
{
    ThreadStart ts = *(ThreadStart *)p;
    free(p);
    ts.fn(ts.arg);
    return 0;
}

void *Win_StartThread(void (*fn)(void *), void *arg)
{
    ThreadStart *ts = (ThreadStart *)malloc(sizeof *ts);
    if (!ts) return NULL;
    ts->fn = fn; ts->arg = arg;
    HANDLE h = CreateThread(NULL, 0, thread_main, ts, 0, NULL);
    if (!h) free(ts);
    return h;
}

void Win_JoinThread(void *handle)
{
    if (!handle) return;
    WaitForSingleObject((HANDLE)handle, INFINITE);
    CloseHandle((HANDLE)handle);
}

bool Win_OpenFolder(const char *path)
{
    char p[1100];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p; *c; c++) if (*c == '/') *c = 0x5C;
    return (INT_PTR)ShellExecuteA(NULL, "open", p, NULL, NULL, SW_SHOWNORMAL) > 32;
}
