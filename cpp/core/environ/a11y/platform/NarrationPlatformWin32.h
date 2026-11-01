//---------------------------------------------------------------------------
// Windows 后端（对齐 CPyMO 的桌面实现）：
//   朗读：优先动态加载 Tolk.dll（Tolk_Load / Tolk_Output / Tolk_Unload），
//         与 CPyMO 完全一致；没有 Tolk 时回退到系统 SAPI
//         （powershell System.Speech，静默窗口）。
//   剪贴板：Win32 剪贴板 API。
//   不做链接期依赖，全部 GetProcAddress。
//---------------------------------------------------------------------------
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>   // PlaySound
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

#include <string>

namespace TVPNarrationPlatform {

inline std::wstring Widen(const std::string &utf8) {
    if(utf8.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if(n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
}

inline std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if(n == 0 || n >= sizeof(buf) / sizeof(buf[0]))
        return std::wstring();
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

inline HMODULE LoadTolk() {
    const std::wstring dir = ExeDir();
    const wchar_t *arch =
#if defined(_M_ARM64)
        L"arm64";
#elif defined(_M_ARM64EC)
        L"arm64ec";
#elif defined(_M_IX86)
        L"x86";
#else
        L"x64";
#endif
    std::wstring candidates[4];
    int count = 0;
    if(!dir.empty()) {
        candidates[count++] = dir + L"\\Tolk.dll";
        candidates[count++] = dir + L"\\tolk\\Tolk.dll";
        candidates[count++] = dir + L"\\tolk\\" + arch + L"\\Release\\Tolk.dll";
    }
    candidates[count++] = L"Tolk.dll";
    for(int i = 0; i < count; ++i) {
        HMODULE m = LoadLibraryExW(candidates[i].c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if(m)
            return m;
    }
    return nullptr;
}

// ---- Tolk（动态） ----
// Tolk.dll 依赖同目录的一堆屏幕阅读器桥接 DLL（nvdaControllerClient64.dll、
// SAAPI64.dll、ZDSR、PCTKUSR…），所以必须整目录一起分发（third_party/tolk
// 原样取自 CPyMO），并用 LOAD_WITH_ALTERED_SEARCH_PATH 让这些兄弟 DLL 能被
// 找到，而不是只看 PATH。
struct TolkApi {
    HMODULE module = nullptr;
    void (*load)() = nullptr;
    void (*unload)() = nullptr;
    int (*output)(const wchar_t *, bool) = nullptr;
    bool tried = false;

    bool Ready() {
        if(tried)
            return module != nullptr;
        tried = true;
        module = LoadTolk();
        if(!module)
            return false;
        load = reinterpret_cast<void (*)()>(GetProcAddress(module, "Tolk_Load"));
        unload = reinterpret_cast<void (*)()>(GetProcAddress(module, "Tolk_Unload"));
        output = reinterpret_cast<int (*)(const wchar_t *, bool)>(
            GetProcAddress(module, "Tolk_Output"));
        if(!output) {
            FreeLibrary(module);
            module = nullptr;
            return false;
        }
        if(load)
            load();
        return true;
    }

    void Release() {
        if(module) {
            if(unload)
                unload();
            FreeLibrary(module);
            module = nullptr;
        }
    }
};

inline TolkApi &Tolk() {
    static TolkApi api;
    return api;
}

// ---- SAPI 回退：一次性 powershell 进程 ----
inline HANDLE &SapiProcess() {
    static HANDLE h = nullptr;
    return h;
}

inline void StopSpeech() {
    HANDLE &h = SapiProcess();
    if(h) {
        TerminateProcess(h, 0);
        CloseHandle(h);
        h = nullptr;
    }
    if(Tolk().Ready() && Tolk().output)
        Tolk().output(L"", false);
}

inline void SpeakSapi(const std::wstring &wide) {
    std::wstring script =
        L"Add-Type -AssemblyName System.Speech;"
        L"$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
        L"$s.Rate=0;"
        L"$s.Speak([Console]::In.ReadToEnd())";
    std::wstring cmd = L"powershell -NoProfile -NonInteractive -Command \"" + script + L"\"";

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if(!CreatePipe(&readEnd, &writeEnd, &sa, 0))
        return;
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = readEnd;
    si.hStdOutput = nullptr;
    si.hStdError = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring mutableCmd = cmd;
    BOOL ok = CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(readEnd);
    if(!ok) {
        CloseHandle(writeEnd);
        return;
    }

    DWORD written = 0;
    WriteFile(writeEnd, wide.c_str(), static_cast<DWORD>(wide.size() * sizeof(wchar_t)),
              &written, nullptr);
    CloseHandle(writeEnd);
    CloseHandle(pi.hThread);

    HANDLE &h = SapiProcess();
    if(h) {
        TerminateProcess(h, 0);
        CloseHandle(h);
    }
    h = pi.hProcess;
}

inline bool Speak(const std::string &utf8, bool flush) {
    if(utf8.empty())
        return false;
    std::wstring wide = Widen(utf8);
    if(wide.empty())
        return false;

    if(flush)
        StopSpeech();

    if(Tolk().Ready() && Tolk().output) {
        Tolk().output(wide.c_str(), flush ? true : false);
        return true;
    }
    SpeakSapi(wide);
    return true;
}

// 提示音：随程序分发的 sounds/*.wav（原样取自 CPyMO）
inline void PlaySound(int type) {
    const wchar_t *name = type == 1 ? L"enter.wav" : (type == 2 ? L"menu.wav" : L"select.wav");
    if(type != 1 && type != 2 && type != 3)
        return;
    const std::wstring dir = ExeDir();
    if(dir.empty())
        return;
    std::wstring path = dir + L"\\sounds\\" + name;
    DWORD attr = GetFileAttributesW(path.c_str());
    if(attr == INVALID_FILE_ATTRIBUTES)
        return;
    PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

inline void Vibrate(int) {}

inline bool CopyText(const std::string &utf8) {
    std::wstring wide = Widen(utf8);
    if(wide.empty())
        return false;
    if(!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(mem) {
        void *dst = GlobalLock(mem);
        if(dst) {
            memcpy(dst, wide.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
    return true;
}

inline bool CopyTextAppend(const std::string &utf8) {
    std::string combined = utf8;
    if(OpenClipboard(nullptr)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if(data) {
            const wchar_t *src = static_cast<const wchar_t *>(GlobalLock(data));
            if(src) {
                int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
                if(n > 1) {
                    std::string old(static_cast<size_t>(n - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, src, -1, &old[0], n, nullptr, nullptr);
                    if(!old.empty())
                        combined = old + "\n" + utf8;
                }
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return CopyText(combined);
}

// 桌面端没有可靠的注入通道：选项/对白推进/返回都交给键盘操作（CPyMO 桌面同样如此）
inline void TapImage(int, int) {}
inline void TapScreen(int, int) {}
inline void TapPixels(int, int) {}
inline void TapBack() {}

inline bool ClipboardAvailable() { return true; }

} // namespace TVPNarrationPlatform
