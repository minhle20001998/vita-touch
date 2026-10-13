// Fake Vita filesystem backing the stub fcntl.h.
#include <psp2/io/fcntl.h>
#include <map>
#include <string>
#include <string.h>

static std::map<std::string, std::string> g_files;
static std::map<int, std::pair<std::string, size_t>> g_fds;
static int g_nextFd = 100;
static bool g_failNextRename = false;

void fakefs_reset() {
    g_files.clear();
    g_fds.clear();
    g_nextFd = 100;
    g_failNextRename = false;
}
void fakefs_failNextRename() { g_failNextRename = true; }
std::string fakefs_read(const char* path) {
    auto it = g_files.find(path);
    return it == g_files.end() ? std::string() : it->second;
}
void fakefs_write(const char* path, const std::string& data) { g_files[path] = data; }

SceUID sceIoOpen(const char* path, int flags, int mode) {
    (void)mode;
    std::string p = path;
    bool creat = flags & SCE_O_CREAT;
    bool trunc = flags & SCE_O_TRUNC;
    bool append = flags & SCE_O_APPEND;
    auto it = g_files.find(p);
    if (it == g_files.end()) {
        if (!creat) return -1;
        g_files[p] = "";
        it = g_files.find(p);
    } else if (trunc) {
        it->second.clear();
    }
    int fd = g_nextFd++;
    g_fds[fd] = {p, append ? it->second.size() : 0};
    return fd;
}
int sceIoClose(SceUID fd) { return g_fds.erase(fd) ? 0 : -1; }
int sceIoRead(SceUID fd, void* data, SceSize size) {
    auto it = g_fds.find(fd);
    if (it == g_fds.end()) return -1;
    const std::string& content = g_files[it->second.first];
    size_t pos = it->second.second;
    size_t avail = pos < content.size() ? content.size() - pos : 0;
    size_t n = size < avail ? size : avail;
    memcpy(data, content.data() + pos, n);
    it->second.second += n;
    return (int)n;
}
int sceIoWrite(SceUID fd, const void* data, SceSize size) {
    auto it = g_fds.find(fd);
    if (it == g_fds.end()) return -1;
    std::string& content = g_files[it->second.first];
    size_t pos = it->second.second;
    if (pos > content.size()) content.resize(pos, '\0');
    if (pos + size > content.size()) content.resize(pos + size);
    memcpy(&content[pos], data, size);
    it->second.second += size;
    return (int)size;
}
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence) {
    auto it = g_fds.find(fd);
    if (it == g_fds.end()) return -1;
    const std::string& content = g_files[it->second.first];
    SceOff base = (whence == SCE_SEEK_END) ? (SceOff)content.size() : 0;
    it->second.second = (size_t)(base + offset);
    return (SceOff)it->second.second;
}
int sceIoRemove(const char* path) { return g_files.erase(path) ? 0 : -1; }
int sceIoRename(const char* oldpath, const char* newpath) {
    if (g_failNextRename) {
        g_failNextRename = false;
        return -1;
    }
    auto it = g_files.find(oldpath);
    if (it == g_files.end()) return -1;
    g_files[newpath] = it->second;
    g_files.erase(it);
    return 0;
}
int sceIoMkdir(const char* path, int mode) {
    (void)path;
    (void)mode;
    return 0;
}
