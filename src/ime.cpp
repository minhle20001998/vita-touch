#include "ime.h"
#include <psp2/ime_dialog.h>
#include <psp2/apputil.h>
#include <psp2/io/fcntl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <string.h>

namespace ime {

// Lifecycle log for hardware debugging (ime.log next to boot.log).
static void ilog(const char* msg) {
    int fd = sceIoOpen("ux0:data/VitaTouchPad/ime.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static bool g_open = false;
static bool g_finished = false;
static bool g_cancelled = false;
static std::string g_text;
static SceWChar16 g_buffer[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static void utf8ToUtf16(const std::string& in, SceWChar16* out, size_t maxLen) {
    size_t o = 0;
    size_t i = 0;
    while (i < in.size() && o + 1 < maxLen) {
        unsigned char c = in[i];
        if (c < 0x80) {
            out[o++] = c;
            i++;
        } else if ((c >> 5) == 0x6 && i + 1 < in.size()) {
            out[o++] = ((SceWChar16)(c & 0x1F) << 6) | (in[i + 1] & 0x3F);
            i += 2;
        } else if ((c >> 4) == 0xE && i + 2 < in.size()) {
            out[o++] = ((SceWChar16)(c & 0x0F) << 12) | ((SceWChar16)(in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F);
            i += 3;
        } else {
            out[o++] = '?';
            i++;
        }
    }
    out[o] = 0;
}

static std::string utf16ToUtf8(const SceWChar16* in) {
    std::string out;
    for (size_t i = 0; in[i]; i++) {
        SceWChar16 c = in[i];
        if (c < 0x80) {
            out += (char)c;
        } else if (c < 0x800) {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static SceWChar16 g_title[64];

bool init() {
    sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL);
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    init.workBufSize = 0;
    sceAppUtilInit(&init, &boot);
    SceCommonDialogConfigParam cfg;
    sceCommonDialogConfigParamInit(&cfg);
    sceCommonDialogSetConfigParam(&cfg);
    return true;
}

bool open(const std::string& title, const std::string& initialText, int maxLen) {
    if (g_open) return false;
    if (maxLen <= 0 || maxLen > SCE_IME_DIALOG_MAX_TEXT_LENGTH) maxLen = SCE_IME_DIALOG_MAX_TEXT_LENGTH;

    memset(g_buffer, 0, sizeof(g_buffer));
    utf8ToUtf16(initialText, g_buffer, SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1);

    utf8ToUtf16(title, g_title, 64);

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = 0;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = g_title;
    param.maxTextLength = (SceUInt32)maxLen;
    param.initialText = g_buffer;
    param.inputTextBuffer = g_buffer;

    int initRc = sceImeDialogInit(&param);
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "open: init rc=0x%08X title='%s' initial='%s' maxLen=%d",
                 (unsigned)initRc, title.c_str(), initialText.c_str(), maxLen);
        ilog(buf);
    }
    if (initRc < 0) return false;
    g_open = true;
    g_finished = false;
    g_cancelled = false;
    return true;
}

bool isOpen() { return g_open; }

void update() {
    if (!g_open) return;
    SceCommonDialogStatus status = sceImeDialogGetStatus();
    static int lastStatus = -1;
    static int frames = 0;
    frames++;
    if ((int)status != lastStatus || (frames % 300) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "update: status=%d frame=%d", (int)status, frames);
        ilog(buf);
        lastStatus = (int)status;
    }
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result;
        memset(&result, 0, sizeof(result));
        sceImeDialogGetResult(&result);
        g_cancelled = (result.button == SCE_IME_DIALOG_BUTTON_CLOSE);
        g_text = g_cancelled ? "" : utf16ToUtf8(g_buffer);
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "finished: button=%d cancelled=%d textLen=%u",
                     (int)result.button, g_cancelled ? 1 : 0,
                     (unsigned)g_text.size());
            ilog(buf);
        }
        sceImeDialogTerm();
        g_open = false;
        g_finished = true;
    }
}

bool finished() {
    if (g_finished) {
        g_finished = false;
        return true;
    }
    return false;
}

bool cancelled() { return g_cancelled; }

const std::string& text() { return g_text; }

} // namespace ime
