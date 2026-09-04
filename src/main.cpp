// main.cpp
//
// Entry point. Opens an optional .pxb path passed as argv[1] (used by the OS
// file-association handlers on all three platforms) and runs the preview loop.
//
#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#endif

#include "app.h"
#include "renderer.h"
#include "ui.h"
#include "fileutil.h"
#include "regutil.h"

#include <SDL3/SDL.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
// On Windows the narrow argv is in the active code page (e.g. GBK on zh-CN);
// re-acquire it as UTF-8 via the wide command line. Same MinGW UTF-8 path
// caveat documented in fileutil.h — do not trust the raw narrow argv.
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include <cstring>
#include <cstdlib>
static std::vector<std::string> utf8_argv() {
    int argc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> out;
    out.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        const wchar_t* s = wargv[i];
        int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
        std::string u(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, s, -1, u.data(), len, nullptr, nullptr);
        out.push_back(std::move(u));
    }
    if (wargv) LocalFree(wargv);
    return out;
}

// ---- Diagnostics -------------------------------------------------------------
// pxb-preview.exe is a Win GUI subsystem binary (no console), so printf and
// stderr are invisible. Mirror every diagnostic to <exe-dir>/pxb-preview.log
// (preferred, survives even when %TEMP% is unwritable) and fall back to
// %TEMP%\pxb-preview.log. Also call OutputDebugStringA so DebugView /
// VS debugger can capture it live.
static FILE* g_log = nullptr;
static char g_log_path[MAX_PATH] = {0};

static void log_open() {
    if (g_log) return;

    // Try 1: log next to the exe (always writable if the user can extract
    // the zip there; survives %TEMP% being missing/unwritable).
    char exe_path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        char* slash = strrchr(exe_path, '\\');
        if (!slash) slash = strrchr(exe_path, '/');
        if (slash) {
            slash[1] = '\0';   // keep trailing separator
            strncpy(g_log_path, exe_path, MAX_PATH - 1);
            strncat(g_log_path, "pxb-preview.log", MAX_PATH - strlen(g_log_path) - 1);
            g_log = fopen(g_log_path, "w");
        }
    }

    // Try 2: fall back to %TEMP%\pxb-preview.log
    if (!g_log) {
        char tmp[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tmp) > 0) {
            strncpy(g_log_path, tmp, MAX_PATH - 1);
            strncat(g_log_path, "pxb-preview.log", MAX_PATH - strlen(g_log_path) - 1);
            g_log = fopen(g_log_path, "w");
        }
    }

    if (!g_log) {
        // Last resort: still record the path we tried, for OutputDebugString.
        return;
    }

    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(g_log, "pxb-preview log opened at %s\n", ts);
    fprintf(g_log, "log path: %s\n", g_log_path);
    fprintf(g_log, "cmdline: %s\n", GetCommandLineA());
    OSVERSIONINFOW vi; ZeroMemory(&vi, sizeof(vi)); vi.dwOSVersionInfoSize = sizeof(vi);
    if (GetVersionExW(&vi)) {
        fprintf(g_log, "windows: %lu.%lu build %lu\n",
                vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    }
    fflush(g_log);

    char dbg[512];
    snprintf(dbg, sizeof(dbg), "[pxb-preview] log opened: %s\n", g_log_path);
    OutputDebugStringA(dbg);
}
static void log_close() { if (g_log) { fclose(g_log); g_log = nullptr; } }
static void log_msg(const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (g_log) {
        fprintf(g_log, "[%s] %s\n", tag, buf);
        fflush(g_log);
    }
    // Always mirror to OutputDebugString so a live debugger / DebugView can
    // see it even when the log file write failed.
    char dbg[1100];
    snprintf(dbg, sizeof(dbg), "[pxb-preview][%s] %s\n", tag, buf);
    OutputDebugStringA(dbg);
}
static std::string log_path_str() {
    return g_log_path[0] ? std::string(g_log_path) : std::string("(log open failed)");
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    log_open();   // safe to call again; idempotent
    DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = ep ? (void*)ep->ExceptionRecord->ExceptionAddress : 0;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "[CRASH] code=0x%08lx addr=%p (unhandled exception, window destroyed)",
             code, addr);
    log_msg("FATAL", "%s", buf);
    log_close();
    return EXCEPTION_EXECUTE_HANDLER;   // let Windows terminate
}
#else
// Non-Windows: a terminal launch has stderr, so mirror diagnostics there and
// keep pxb-preview.log in the CWD for bug reports. SEH does not exist; a
// minimal signal handler covers hard crashes.
static FILE* g_log = nullptr;
static void log_open() {
    if (g_log) return;
    g_log = fopen("pxb-preview.log", "w");
    if (g_log) {
        time_t t = time(nullptr);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(g_log, "pxb-preview log opened at %s\n", ts);
        fflush(g_log);
    }
}
static void log_close() { if (g_log) { fclose(g_log); g_log = nullptr; } }
static void log_msg(const char* tag, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[pxb-preview][%s] %s\n", tag, buf);
    if (g_log) { fprintf(g_log, "[%s] %s\n", tag, buf); fflush(g_log); }
}
static std::string log_path_str() { return "pxb-preview.log"; }
static void crash_handler(int sig) {
    log_msg("FATAL", "signal %d (unhandled crash)", sig);
    log_close();
    _Exit(128 + sig);   // std::_Exit: no atexit/static destructor races in a crash path
}
#endif // _WIN32

int main(int argc, char** argv) {
    (void)argc; (void)argv;   // used only on non-Windows; Win32 re-derives UTF-8 argv
#ifdef _WIN32
    std::vector<std::string> args = utf8_argv();
#else
    // Non-Windows: argv is already UTF-8; build the same vector so the rest
    // of main() is platform-agnostic.
    std::vector<std::string> args(argv, argv + argc);
#endif
    const int argc_u = (int)args.size();
    auto arg = [&](int i) -> const char* { return (i >= 0 && i < argc_u) ? args[i].c_str() : ""; };

    log_open();
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#else
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGILL, crash_handler);
    signal(SIGFPE, crash_handler);
#endif
    log_msg("INFO", "main entered, argc=%d", argc_u);
    for (int i = 0; i < argc_u; ++i) {
        log_msg("INFO", "argv[%d] = %s", i, args[i].c_str());
    }

    // Headless registration mode. Used by the elevated (UAC) child process
    // launched from the File menu: no SDL, no window — pure Win32 so the
    // operation works without initializing the renderer.
    //
    // i18n note: these MessageBox strings are intentionally hardcoded (the
    // only such strings in the codebase). This child process is spawned via
    // ShellExecuteW(runas) and never inherits the GUI's language choice; it
    // runs before any UI exists, so tr() here would always return the zh
    // default anyway. If a language preference is ever persisted (registry/
    // config), route these through the i18n tables.
#ifdef _WIN32
    if (argc_u >= 2) {
        const std::string& mode = args[1];
        if (mode == "--register" || mode == "--unregister") {
            std::string err;
            bool ok = (mode == "--register")
                          ? pxb::register_pxb_association(err)
                          : pxb::unregister_pxb_association(err);
            std::wstring title = (mode == "--register")
                                     ? L"注册 .pxb 文件关联"
                                     : L"取消 .pxb 文件关联";
            std::wstring msg;
            if (ok) {
                msg = (mode == "--register")
                          ? L"注册成功：双击 .pxb 将用 PXB Preview 打开，\n资源管理器可预览缩略图。"
                          : L"已取消注册。";
            } else {
                msg = L"操作失败：\n";
                int n = MultiByteToWideChar(CP_UTF8, 0, err.c_str(), (int)err.size(),
                                            nullptr, 0);
                std::wstring werr(n > 0 ? n : 0, L'\0');
                if (n > 0)
                    MultiByteToWideChar(CP_UTF8, 0, err.c_str(), (int)err.size(),
                                        werr.data(), n);
                msg += werr;
            }
            log_msg("REG", "%s %s", mode.c_str(), ok ? "OK" : ("FAILED: " + err).c_str());
            MessageBoxW(nullptr, msg.c_str(), title.c_str(),
                        ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
            log_close();
            return ok ? 0 : 1;
        }
    }
#else
    // The file-association CLI is a Windows feature (UAC child of the GUI's
    // File menu). On Linux/macOS, associations are installed by the platform
    // integration scripts under integrations/{linux,macos}.
    if (argc_u >= 2 && (args[1] == "--register" || args[1] == "--unregister")) {
        log_msg("WARN", "%s is Windows-only; use the integrations/{linux,macos} "
                        "install scripts on this platform", args[1].c_str());
        log_close();
        return 1;
    }
#endif

    pxb::App app;

    pxb::Renderer renderer;
    std::string err;
    if (!renderer.init(840, 540, "PXB Preview", err)) {
        log_msg("ERROR", "renderer.init failed: %s", err.c_str());
        std::string msg = "Failed to initialize renderer.\n\n";
        msg += err;
        msg += "\n\nA log was written to:\n";
        msg += log_path_str();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PXB Preview", msg.c_str(), nullptr);
        return 1;
    }
    log_msg("INFO", "renderer.init OK (840x540)");

    // Let App reach the renderer so it can delete evicted sibling GL textures
    // on the main thread (GL is per-thread; worker threads must never touch it).
    app.set_renderer(&renderer);

    // Wire the document-loaded hook so every successful load clears the
    // previous document's GPU textures and then uploads the new frame/thumbnail
    // textures, discarding the CPU-side pixels afterwards.
    //
    // Clear-then-upload lives HERE (not in the main loop) so texture ownership
    // is fully owned by the Renderer. The old design cleared textures in the
    // loop based on current_path changes, which raced with this upload and left
    // the preview permanently blank (the load callback had already discarded
    // the CPU pixels, and the loop deleted the just-uploaded GPU texture).
    app.on_document_loaded = [&renderer](pxb::PxbDocument& doc) {
        // Only drop the DOCUMENT textures (frames + doc thumbnail). Sibling-list
        // thumbnail textures (negative keys) are intentionally preserved so
        // navigating between files doesn't thrash the file-list previews.
        renderer.clear_document_textures();
        renderer.upload_document(doc);
    };

    // Drag & drop a .pxb onto the window loads it directly.
    renderer.on_drop_file = [&app](const std::string& p) {
        app.load_file(p);
    };

    // Auto-load a .pxb passed on the command line (e.g. double-click in
    // Explorer). This is done *after* renderer init so the upload-and-discard
    // hook is already wired.
#ifdef _WIN32
    try {
#endif
        if (pxb::path_exists(arg(1))) {
            if (pxb::is_pxb_file(arg(1))) {
                log_msg("INFO", "auto-load file: %s", arg(1));
                bool ok = app.load_file(arg(1));
                log_msg("INFO", "load_file result: %s (status: %s)",
                        ok ? "OK" : "FAILED", app.status_msg.c_str());
            } else {
                log_msg("INFO", "argv[1] is not a .pxb file; ignoring");
            }
        }
#ifdef _WIN32
    } catch (const std::exception& e) {
        log_msg("ERROR", "load_file threw: %s", e.what());
    }
#endif

    Uint64 last = SDL_GetTicks();
    bool running = true;
    while (running) {
        if (!renderer.begin_frame()) { running = false; break; }

        Uint64 now = SDL_GetTicks();
        double dt = (double)(now - last) / 1000.0;
        last = now;
        if (dt > 0.25) dt = 0.25;   // clamp after stalls

        app.tick(dt);
        // Drain GL texture keys dropped by the metadata cache (eviction) and
        // delete them on the main thread. Keeps RAM/GPU bounded without leaking
        // textures when the sibling list scrolls past the LRU cap.
        app.frame_cache_maintenance();

#ifdef _WIN32
        // Update live memory counters for the status bar.
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                 sizeof(pmc))) {
            app.mem_working_mb = (int)(pmc.WorkingSetSize / (1024 * 1024));
            app.mem_private_mb = (int)(pmc.PrivateUsage / (1024 * 1024));
        }
#endif

        pxb::render_ui(app, renderer);
        renderer.end_frame();
    }
    // Persist the view configuration (View-menu toggles, panel widths,
    // minimap position) so the next launch restores this session's layout.
    pxb::save_view_settings(app);
    log_msg("INFO", "main exit normally");
    log_close();
    return 0;
}
