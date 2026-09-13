#include "platform/windows/crashlog.h"
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void modinfo(DWORD64 addr, char *out, size_t cap) {
    HMODULE mod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)addr, &mod);
    char name[MAX_PATH] = {0};
    if (mod)
        GetModuleFileNameA(mod, name, sizeof name);
    char *base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    DWORD64 mbase = mod ? (DWORD64)(uintptr_t)mod : 0;
    snprintf(out, cap, "%s+0x%llx", base[0] ? base : "???", (unsigned long long)(addr - mbase));
}

static LONG WINAPI veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if ((code & 0xC0000000) != 0xC0000000)
        return EXCEPTION_CONTINUE_SEARCH;
    char path[300] = {0};
    char tmp[240] = {0};
    if (!GetTempPathA(sizeof tmp, tmp))
        return EXCEPTION_CONTINUE_SEARCH;
    snprintf(path, sizeof path, "%saudiosync-crash.log", tmp);
    FILE *f = fopen(path, "ab");
    if (!f)
        return EXCEPTION_CONTINUE_SEARCH;

    void *ip = ep->ExceptionRecord->ExceptionAddress;
    fprintf(f, "\n=== crash code=0x%08lx addr=%p ===\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ip);

    HANDLE proc = GetCurrentProcess();
    char sympath[512] = {0};
    snprintf(sympath, sizeof sympath, "SRV*%saudiosync-symcache*https://msdl.microsoft.com/download/symbols",
             tmp);
    char info[320];
    if (SymInitialize(proc, sympath, TRUE)) {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_UNDNAME);
        void *frames[64];
        USHORT nf = CaptureStackBackTrace(0, 64, frames, NULL);
        unsigned char symbuf[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        for (USHORT i = 0; i < nf; i++) {
            DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
            DWORD64 disp = 0;
            if (SymFromAddr(proc, addr, &disp, sym))
                fprintf(f, "#%-2d %p %s+0x%llx\n", i, frames[i], sym->Name, (unsigned long long)disp);
            else {
                modinfo(addr, info, sizeof info);
                fprintf(f, "#%-2d %p %s\n", i, frames[i], info);
            }
        }
        SymCleanup(proc);
    } else {

        void *frames[64];
        USHORT nf = CaptureStackBackTrace(0, 64, frames, NULL);
        for (USHORT i = 0; i < nf; i++) {
            modinfo((DWORD64)(uintptr_t)frames[i], info, sizeof info);
            fprintf(f, "#%-2d %p %s\n", i, frames[i], info);
        }
    }
    fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}

void crashlog_install(void) {
    AddVectoredExceptionHandler(1, veh);
}
