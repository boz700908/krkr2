//---------------------------------------------------------------------------
// macOS 后端：
//   朗读：常驻 /usr/bin/say -f -（stdin 持续喂文本），flush 时杀掉旧进程重开，
//         非 flush 时写入现有进程 —— 对应 Android 的 QUEUE_FLUSH / QUEUE_ADD。
//   剪贴板：pbcopy。
//---------------------------------------------------------------------------
#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <mach-o/dyld.h>

#include <cstdint>

namespace TVPNarrationPlatform {

inline void SpawnDetached(const char *const argv[], const std::string *stdinText) {
    int pipeFd[2] = {-1, -1};
    if(stdinText && pipe(pipeFd) != 0)
        return;

    pid_t launcher = fork();
    if(launcher == 0) {
        if(stdinText) {
            dup2(pipeFd[0], STDIN_FILENO);
            close(pipeFd[0]);
            if(pipeFd[1] >= 0)
                close(pipeFd[1]);
        }
        int devnull = open("/dev/null", O_WRONLY);
        if(devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        pid_t child = fork();
        if(child != 0)
            _exit(child < 0 ? 127 : 0);
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

// 常驻 say 进程
struct Speaker {
    pid_t pid = -1;
    int fd = -1;
};

inline Speaker &Say() {
    static Speaker s;
    return s;
}

inline void StopSpeech() {
    Speaker &s = Say();
    if(s.fd >= 0) {
        close(s.fd);
        s.fd = -1;
    }
    if(s.pid > 0) {
        kill(s.pid, SIGTERM);
        while(waitpid(s.pid, nullptr, WNOHANG) < 0 && errno == EINTR) {}
        s.pid = -1;
    }
}

inline bool StartSpeaker() {
    Speaker &s = Say();
    int pipeFd[2] = {-1, -1};
    if(pipe(pipeFd) != 0)
        return false;

    pid_t pid = fork();
    if(pid == 0) {
        dup2(pipeFd[0], STDIN_FILENO);
        close(pipeFd[0]);
        close(pipeFd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if(devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl("/usr/bin/say", "say", "-f", "-", (char *)nullptr);
        _exit(127);
    }
    close(pipeFd[0]);
    if(pid < 0) {
        close(pipeFd[1]);
        return false;
    }
    s.pid = pid;
    s.fd = pipeFd[1];
    return true;
}

inline bool WriteAll(int fd, const std::string &data) {
    const char *p = data.c_str();
    size_t left = data.size();
    while(left > 0) {
        ssize_t n = write(fd, p, left);
        if(n <= 0)
            return false;
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

inline bool Speak(const std::string &utf8, bool flush) {
    if(utf8.empty())
        return false;
    Speaker &s = Say();
    if(flush || s.fd < 0) {
        StopSpeech();
        if(!StartSpeaker())
            return false;
    }
    std::string line = utf8;
    line.push_back('\n');
    if(!WriteAll(s.fd, line)) {
        // say 进程退出过，重开一次
        StopSpeech();
        if(!StartSpeaker() || !WriteAll(s.fd, line))
            return false;
    }
    return true;
}

inline std::string ExecutableDir() {
    char buf[4096];
    uint32_t size = static_cast<uint32_t>(sizeof(buf));
    if(_NSGetExecutablePath(buf, &size) != 0)
        return std::string();
    std::string path(buf);
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// 提示音：随 .app 分发的 sounds/*.wav（原样取自 CPyMO）
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
    const char *argv[] = {"/usr/bin/afplay", path.c_str(), nullptr};
    SpawnDetached(argv, nullptr);
}

inline void Vibrate(int) {}

inline bool CopyText(const std::string &utf8) {
    if(utf8.empty())
        return false;
    const char *argv[] = {"/usr/bin/pbcopy", nullptr};
    SpawnDetached(argv, &utf8);
    return true;
}

inline bool CopyTextAppend(const std::string &utf8) {
    return CopyText(utf8);
}

// 桌面端没有可靠的注入通道：选项/对白推进/返回都交给键盘操作（CPyMO 桌面同样如此）
inline void TapImage(int, int) {}
inline void TapScreen(int, int) {}
inline void TapPixels(int, int) {}
inline void TapBack() {}

inline bool ClipboardAvailable() { return access("/usr/bin/pbcopy", X_OK) == 0; }

} // namespace TVPNarrationPlatform
