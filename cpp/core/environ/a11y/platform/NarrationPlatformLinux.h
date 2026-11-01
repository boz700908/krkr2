//---------------------------------------------------------------------------
// Linux 后端（对齐 CPyMO 的 cpymo_backend_text_extract 桌面实现）：
//   朗读：speech-dispatcher 的 spd-say，缺失时回退 espeak-ng / espeak
//   剪贴板：xclip / xsel / wl-copy
// 全部通过 fork/exec，不引入构建依赖；子进程被二次 fork 交给 init 回收。
//---------------------------------------------------------------------------
#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace TVPNarrationPlatform {

// 二次 fork：父进程只回收「发射器」，真正的播放器立刻改挂到 init，
// 不产生僵尸进程，也不改宿主进程的 SIGCHLD 处理方式。
inline void SpawnDetached(const char *const argv[], const std::string *stdinText) {
    int pipeFd[2] = {-1, -1};
    if(stdinText && pipe(pipeFd) != 0)
        return;

    pid_t launcher = fork();
    if(launcher == 0) {
        if(stdinText) {
            dup2(pipeFd[0], STDIN_FILENO);
            close(pipeFd[0]);
            pipeFd[0] = -1;
            if(pipeFd[1] >= 0)
                close(pipeFd[1]);
        }
        int devnull = open("/dev/null", O_WRONLY);
        if(devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        pid_t speaker = fork();
        if(speaker != 0)
            _exit(speaker < 0 ? 127 : 0);
        execvp(argv[0], const_cast<char *const *>(argv));
        _exit(127);
    }

    if(stdinText) {
        close(pipeFd[0]);
        if(launcher > 0 && pipeFd[1] >= 0) {
            const char *p = stdinText->c_str();
            size_t left = stdinText->size();
            while(left > 0) {
                ssize_t n = write(pipeFd[1], p, left);
                if(n <= 0)
                    break;
                p += n;
                left -= static_cast<size_t>(n);
            }
        }
        if(pipeFd[1] >= 0)
            close(pipeFd[1]);
    }

    if(launcher > 0)
        while(waitpid(launcher, nullptr, 0) < 0 && errno == EINTR) {}
}

inline bool HaveExecutable(const char *name) {
    const char *path = getenv("PATH");
    if(!path)
        return false;
    std::string dirs(path);
    size_t begin = 0;
    while(begin <= dirs.size()) {
        size_t end = dirs.find(':', begin);
        if(end == std::string::npos)
            end = dirs.size();
        std::string dir = dirs.substr(begin, end - begin);
        if(dir.empty())
            dir = ".";
        std::string full = dir + "/" + name;
        if(access(full.c_str(), X_OK) == 0)
            return true;
        begin = end + 1;
    }
    return false;
}

inline void StopSpeech() {
    if(HaveExecutable("spd-say")) {
        const char *argv[] = {"spd-say", "-C", nullptr};
        SpawnDetached(argv, nullptr);
    } else if(HaveExecutable("pkill")) {
        const char *argv[] = {"pkill", "-x", HaveExecutable("espeak-ng") ? "espeak-ng" : "espeak",
                              nullptr};
        SpawnDetached(argv, nullptr);
    }
}

inline bool Speak(const std::string &utf8, bool flush) {
    if(utf8.empty())
        return false;
    if(flush)
        StopSpeech();
    if(HaveExecutable("spd-say")) {
        const char *argv[] = {"spd-say", "--", utf8.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
        return true;
    }
    if(HaveExecutable("espeak-ng")) {
        const char *argv[] = {"espeak-ng", "-v", "zh", utf8.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
        return true;
    }
    if(HaveExecutable("espeak")) {
        const char *argv[] = {"espeak", "-v", "zh", utf8.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
        return true;
    }
    return false;
}

inline std::string ExecutableDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(n <= 0)
        return std::string();
    std::string path(buf, static_cast<size_t>(n));
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// 提示音：随程序分发的 sounds/*.wav（原样取自 CPyMO）
inline void PlaySound(int type) {
    if(type != 1 && type != 2 && type != 3)
        return;
    const char *name = type == 1 ? "enter.wav" : (type == 2 ? "menu.wav" : "select.wav");
    const std::string dir = ExecutableDir();
    if(dir.empty())
        return;
    const std::string path = dir + "/sounds/" + name;
    if(access(path.c_str(), R_OK) != 0)
        return;
    if(HaveExecutable("paplay")) {
        const char *argv[] = {"paplay", path.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
    } else if(HaveExecutable("aplay")) {
        const char *argv[] = {"aplay", "-q", path.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
    } else if(HaveExecutable("ffplay")) {
        const char *argv[] = {"ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet",
                              path.c_str(), nullptr};
        SpawnDetached(argv, nullptr);
    }
}

inline void Vibrate(int) {}

inline bool CopyText(const std::string &utf8) {
    if(utf8.empty())
        return false;
    if(getenv("WAYLAND_DISPLAY") && HaveExecutable("wl-copy")) {
        const char *argv[] = {"wl-copy", nullptr};
        SpawnDetached(argv, &utf8);
        return true;
    }
    if(HaveExecutable("xclip")) {
        const char *argv[] = {"xclip", "-selection", "clipboard", "-i", nullptr};
        SpawnDetached(argv, &utf8);
        return true;
    }
    if(HaveExecutable("xsel")) {
        const char *argv[] = {"xsel", "-b", "-i", nullptr};
        SpawnDetached(argv, &utf8);
        return true;
    }
    if(HaveExecutable("wl-copy")) {
        const char *argv[] = {"wl-copy", nullptr};
        SpawnDetached(argv, &utf8);
        return true;
    }
    return false;
}

inline bool CopyTextAppend(const std::string &utf8) {
    // 桌面端「追加复制」退化为复制当前句：命令行剪贴板工具没有可靠的读回语义
    return CopyText(utf8);
}

// 桌面端没有可靠的注入通道：选项/对白推进/返回都交给键盘操作（CPyMO 桌面同样如此）
inline void TapImage(int, int) {}
inline void TapScreen(int, int) {}
inline void TapPixels(int, int) {}
inline void TapBack() {}

inline bool ClipboardAvailable() {
    return HaveExecutable("xclip") || HaveExecutable("xsel") || HaveExecutable("wl-copy");
}

} // namespace TVPNarrationPlatform
