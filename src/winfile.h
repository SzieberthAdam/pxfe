/* winfile.h - native Windows file/folder dialogs (kept apart from raylib because windows.h clashes with its names). */
#ifndef WINFILE_H
#define WINFILE_H

#include <stdbool.h>

/* filter: "Description|*.ext;*.ext2|Description 2|*.*" ('|' separates fields). Result path uses '/' separators. */
bool Win_OpenFileDialog(char *out, int size, const char *filter);
/* defaultPath: full suggested path (may be ""), ext: default extension without dot (e.g. "pxfd"). */
bool Win_SaveFileDialog(char *out, int size, const char *filter, const char *defaultPath, const char *ext);
bool Win_PickFolder(char *out, int size, const char *title);
/* Opens a folder in the Explorer (ShellExecute). */
bool Win_OpenFolder(const char *path);
/* A worker thread: fn(arg) runs on it; Win_JoinThread waits for it and releases the handle. */
void *Win_StartThread(void (*fn)(void *), void *arg);
void  Win_JoinThread(void *handle);

#endif
