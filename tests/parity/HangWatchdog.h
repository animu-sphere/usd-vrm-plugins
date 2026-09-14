// SPDX-License-Identifier: Apache-2.0
//
// TEMPORARY -- the diagnosis of PR #191's Linux-only hang, to be removed with
// it. If the process has not finished `seconds` after Start(), every thread's
// backtrace is printed to stderr, the frames inside this executable are run
// through addr2line (so a function with internal linkage gets a name and a
// line), and the process exits 3.
#pragma once

#if defined(__linux__)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <dirent.h>
#include <dlfcn.h>
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

struct Stack
{
    long tid = 0;
    int size = 0;
    void* frames[64] = {};
};

inline Stack* Stacks()
{
    static Stack stacks[64];
    return stacks;
}

inline std::atomic<int>& StackCount()
{
    static std::atomic<int> count{0};
    return count;
}

inline void Dump(int)
{
    const int slot = StackCount()++;
    if (slot >= 64) {
        return;
    }
    Stack& stack = Stacks()[slot];
    stack.tid = static_cast<long>(syscall(SYS_gettid));
    stack.size = backtrace(stack.frames, 64);
    char head[64];
    const int length = std::snprintf(head, sizeof head, "--- thread %ld\n",
                                     stack.tid);
    (void)!write(STDERR_FILENO, head, static_cast<size_t>(length));
    backtrace_symbols_fd(stack.frames, stack.size, STDERR_FILENO);
}

// The frames of each recorded stack that fall inside this executable, as
// call-site offsets addr2line can read.
inline void Symbolize()
{
    Dl_info self = {};
    if (!dladdr(reinterpret_cast<void*>(&Symbolize), &self) || !self.dli_fbase) {
        return;
    }
    char exe[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (length <= 0) {
        return;
    }
    const int count = std::min(StackCount().load(), 64);
    for (int s = 0; s < count; ++s) {
        const Stack& stack = Stacks()[s];
        std::string command = "addr2line -f -C -i -p -e '";
        command += exe;
        command += "'";
        bool any = false;
        for (int f = 0; f < stack.size; ++f) {
            Dl_info info = {};
            if (!dladdr(stack.frames[f], &info) || info.dli_fbase != self.dli_fbase) {
                continue;
            }
            char offset[32];
            std::snprintf(offset, sizeof offset, " 0x%lx",
                          static_cast<unsigned long>(
                              static_cast<char*>(stack.frames[f])
                              - static_cast<char*>(self.dli_fbase) - 1));
            command += offset;
            any = true;
        }
        if (!any) {
            continue;
        }
        std::fprintf(stderr, "--- thread %ld, in this executable:\n", stack.tid);
        std::fflush(stderr);
        command += " 1>&2";
        (void)!std::system(command.c_str());

        // The interrupted PC is the frame after the signal trampoline: the
        // third one (Dump, the trampoline, then it). Disassemble around it
        // with source lines, so a spin inlined from a header names its line.
        if (stack.size >= 3) {
            Dl_info info = {};
            if (dladdr(stack.frames[2], &info) && info.dli_fbase == self.dli_fbase) {
                const unsigned long pc = static_cast<unsigned long>(
                    static_cast<char*>(stack.frames[2])
                    - static_cast<char*>(self.dli_fbase));
                char range[160];
                std::snprintf(range, sizeof range,
                              " --start-address=0x%lx --stop-address=0x%lx",
                              pc > 0xa0 ? pc - 0xa0 : 0, pc + 0x60);
                std::fprintf(stderr, "--- thread %ld, interrupted at 0x%lx:\n",
                             stack.tid, pc);
                std::fflush(stderr);
                std::string disassemble = "objdump -d -l -C --no-show-raw-insn";
                disassemble += range;
                disassemble += " '";
                disassemble += exe;
                disassemble += "' 1>&2";
                (void)!std::system(disassemble.c_str());
            }
        }
    }
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
        Symbolize();
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
