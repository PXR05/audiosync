#include "convert.h"
#include "log.h"
#include "platform/util.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void convert_normalize(const char *in, char *out, size_t cap) {
    char tmp[32] = {0};
    snprintf(tmp, sizeof tmp, "%s", in ? in : "keep");
    for (char *p = tmp; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
    }
    if (tmp[0] == '.')
        memmove(tmp, tmp + 1, strlen(tmp));
    if (!strcmp(tmp, "mp3") || !strcmp(tmp, "opus") || !strcmp(tmp, "flac") || !strcmp(tmp, "ogg") ||
        !strcmp(tmp, "oga") || !strcmp(tmp, "m4a") || !strcmp(tmp, "wav"))
        snprintf(out, cap, "%s", tmp[0] ? tmp : "keep");
    else
        snprintf(out, cap, "keep");
}

const char *convert_ext(const char *fmt) {
    if (!fmt || !strcmp(fmt, "keep"))
        return "";
    if (!strcmp(fmt, "mp3"))
        return "mp3";
    if (!strcmp(fmt, "opus"))
        return "opus";
    if (!strcmp(fmt, "flac"))
        return "flac";
    if (!strcmp(fmt, "ogg") || !strcmp(fmt, "oga"))
        return "ogg";
    if (!strcmp(fmt, "m4a"))
        return "m4a";
    if (!strcmp(fmt, "wav"))
        return "wav";
    return "";
}

int convert_needed(const char *fmt, const char *src_ext) {
    const char *want = convert_ext(fmt);
    if (!*want)
        return 0;
    if (!src_ext)
        return 1;
    char s[16] = {0};
    snprintf(s, sizeof s, "%s", src_ext);
    for (char *p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
    if (s[0] == '.')
        memmove(s, s + 1, strlen(s));

    if (!strcmp(want, "ogg") && !strcmp(s, "oga"))
        return 0;
    return strcmp(want, s) != 0;
}

int convert_find_ffmpeg(char *out, size_t cap) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, sizeof exe - 1);
    char *sep = strrchr(exe, '\\');
    if (sep) {
        char cand[MAX_PATH * 2];
        snprintf(cand, sizeof cand, "%.*s\\ffmpeg.exe", (int)(sep - exe), exe);
        if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, cap, "%s", cand);
            return 0;
        }
    }
    char found[MAX_PATH] = {0};
    if (SearchPathA(NULL, "ffmpeg.exe", NULL, sizeof found, found, NULL) > 0) {
        snprintf(out, cap, "%s", found);
        return 0;
    }
    return -1;
}

static const char *codec_args(const char *fmt) {
    if (!strcmp(fmt, "mp3"))
        return "-c:a libmp3lame -q:a 0 -id3v2_version 3 -write_id3v1 1 -c:v copy";
    if (!strcmp(fmt, "opus"))
        return "-c:a libopus -b:a 192k -vbr on -compression_level 10 -c:v copy";
    if (!strcmp(fmt, "flac"))
        return "-c:a flac -compression_level 8 -c:v copy";
    if (!strcmp(fmt, "ogg") || !strcmp(fmt, "oga"))
        return "-c:a libvorbis -q:a 10 -c:v copy";
    if (!strcmp(fmt, "m4a"))
        return "-c:a aac -q:a 2 -c:v copy";
    if (!strcmp(fmt, "wav"))
        return "-c:a pcm_s16le";
    return NULL;
}

static void quote_arg(char *d, size_t cap, const char *s) {
    size_t o = 0;
    if (o < cap - 1)
        d[o++] = '"';
    for (; *s && o < cap - 2; s++) {
        if (*s == '"') {
            if (o < cap - 3) {
                d[o++] = '\\';
                d[o++] = '"';
            }
        } else
            d[o++] = *s;
    }
    if (o < cap - 1)
        d[o++] = '"';
    d[o < cap ? o : cap - 1] = 0;
}

int convert_run(const char *ffmpeg, const wchar_t *wsrc, const wchar_t *wdst, const char *fmt) {
    const char *ca = codec_args(fmt);
    if (!ca) {
        log_err("convert: unknown format '%s'", fmt);
        return -1;
    }
    char *src = wide_to_utf8(wsrc), *dst = wide_to_utf8(wdst);
    if (!src || !dst) {
        free(src);
        free(dst);
        return -1;
    }

    const char *map = "-map 0 -map_metadata 0 -map_metadata 0:s:0";
    if (!strcmp(fmt, "wav"))
        map = "-map 0:a -map_metadata 0 -map_metadata 0:s:a:0";
    else if (!strcmp(fmt, "opus") || !strcmp(fmt, "ogg") || !strcmp(fmt, "oga")) {

        map = "-map 0 -map -0:v -map_metadata 0 -map_metadata 0:s:0";
        log_info("convert: '%s' drops cover art (unsupported in ogg/opus)", fmt);
    }
    char qsrc[MAX_PATH * 3], qdst[MAX_PATH * 3];
    quote_arg(qsrc, sizeof qsrc, src);
    quote_arg(qdst, sizeof qdst, dst);
    char args[8192];
    snprintf(args, sizeof args, "-y -nostdin -hide_banner -loglevel warning -i %s %s %s %s", qsrc, map, ca,
             qdst);
    free(src);
    free(dst);

    char ffq[MAX_PATH * 2];
    quote_arg(ffq, sizeof ffq, ffmpeg);
    size_t cmdlen = strlen(ffq) + 1 + strlen(args) + 1;
    wchar_t *wcmd = NULL;
    {
        char *full = (char *)malloc(cmdlen);
        if (!full)
            return -1;
        snprintf(full, cmdlen, "%s %s", ffq, args);
        wcmd = utf8_to_wide(full);
        free(full);
        if (!wcmd)
            return -1;
    }

    char logpath[MAX_PATH] = {0};
    GetTempPathA(sizeof logpath, logpath);
    strcat(logpath, "audiosync-ffmpeg.log");
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE hlog =
        CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hlog == INVALID_HANDLE_VALUE) {
        free(wcmd);
        return -1;
    }
    HANDLE hnul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);

    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = hlog;
    si.hStdOutput = hnul != INVALID_HANDLE_VALUE ? hnul : hlog;
    si.hStdInput = hnul != INVALID_HANDLE_VALUE ? hnul : NULL;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    int ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(wcmd);
    if (hnul != INVALID_HANDLE_VALUE)
        CloseHandle(hnul);
    CloseHandle(hlog);
    if (!ok) {
        log_err("convert: cannot launch ffmpeg (%lu)", GetLastError());
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code == 0) {
        DeleteFileA(logpath);
        return 0;
    }

    FILE *f = fopen(logpath, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        long off = n > 1500 ? n - 1500 : 0;
        fseek(f, off, SEEK_SET);
        char tail[1600] = {0};
        fread(tail, 1, sizeof tail - 1, f);
        fclose(f);
        log_err("convert: ffmpeg failed (code %lu): %s", code, tail + (off ? 200 : 0));
    } else
        log_err("convert: ffmpeg failed (code %lu)", code);
    return -1;
}
