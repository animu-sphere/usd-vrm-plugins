// SPDX-License-Identifier: Apache-2.0
//
// TEMPORARY -- the diagnosis of PR #191's Linux-only hang, to be removed with
// it. If the process has not finished `seconds` after Start(), every thread's
// backtrace is printed to stderr and the process exits 3, so a hang reaches the
// CI log as a stack rather than as a ctest timeout with nothing in it.
#pragma once

#if defined(__linux__)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <dirent.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace hangwatch
{

inline std::atomic<const char*>& Where()
{
    static std::atomic<const char*> where{"start"};
    return where;
}

inline void Mark(const char* where)
{
    Where() = where;
    std::fprintf(stderr, "[progress] %s\n", where);
    std::fflush(stderr);
}

inline void Dump(int)
{
    void* frames[64];
    const int n = backtrace(frames, 64);
    char head[64];
    const int length = std::snprintf(head, sizeof head, "--- thread %ld\n",
                                     static_cast<long>(syscall(SYS_gettid)));
    (void)!write(STDERR_FILENO, head, static_cast<size_t>(length));
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
}

inline void Start(unsigned seconds)
{
    if (const char* override = std::getenv("HANGWATCH_SECONDS")) {
        seconds = static_cast<unsigned>(std::atoi(override));
    }
    void* warm[1];
    backtrace(warm, 1);  // load the unwinder before a signal handler needs it
    struct sigaction action = {};
    action.sa_handler = Dump;
    sigaction(SIGUSR2, &action, nullptr);
    std::thread([seconds] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stderr, "[hang] not finished after %u s; last mark: %s\n",
                     seconds, Where().load());
        std::fflush(stderr);
        const long self = syscall(SYS_gettid);
        if (DIR* tasks = opendir("/proc/self/task")) {
            while (dirent* entry = readdir(tasks)) {
                const long tid = std::atol(entry->d_name);
                if (tid <= 0 || tid == self) {
                    continue;
                }
                syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
                usleep(300000);
            }
            closedir(tasks);
        }
        std::_Exit(3);
    }).detach();
}

} // namespace hangwatch

#else

namespace hangwatch
{
inline void Mark(const char*) {}
inline void Start(unsigned) {}
} // namespace hangwatch

#endif
