#pragma once
// Host-test stub for <psp2/io/fcntl.h>: in-memory fake Vita filesystem.
#include <stddef.h>

typedef int SceUID;
typedef long SceOff;
typedef size_t SceSize;

#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_RDWR   (SCE_O_RDONLY | SCE_O_WRONLY)
#define SCE_O_CREAT  0x0200
#define SCE_O_TRUNC  0x0400
#define SCE_O_APPEND 0x0100

#define SCE_SEEK_SET 0
#define SCE_SEEK_END 2

SceUID sceIoOpen(const char* path, int flags, int mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void* data, SceSize size);
int sceIoWrite(SceUID fd, const void* data, SceSize size);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoRemove(const char* path);
int sceIoRename(const char* oldpath, const char* newpath);
int sceIoMkdir(const char* path, int mode);
