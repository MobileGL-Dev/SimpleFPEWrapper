// SimpleFPEWrapper - tests/sfpew_fault_trap.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

// Reading one byte too far out of a client vertex array is not observable in
// pixels - the bytes are simply not used - so the tests that are about read
// EXTENT put the array flush against an unreadable page and let the hardware
// answer. What it answers with is a SIGSEGV, which without this would take
// the whole process down and say nothing about which call did it; the trap
// turns it into an ordinary test failure at the assertion that follows.
//
//     sfpew_test::FaultProbe probe;
//     if (probe.Try()) { ...the call under test...; }
//     ASSERT_FALSE(probe.faulted()) << "...";
//
// Nothing is caught when the code is correct, so the trap costs a sigaction
// pair and is otherwise inert.
#pragma once

#include <csetjmp>
#include <csignal>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace sfpew_test {

namespace fault_detail {
inline volatile sig_atomic_t g_faulted = 0;
inline sigjmp_buf g_jump;
inline void Handler(int) {
    g_faulted = 1;
    siglongjmp(g_jump, 1);
}
} // namespace fault_detail

class FaultProbe {
public:
    FaultProbe() {
        fault_detail::g_faulted = 0;
        struct sigaction action;
        std::memset(&action, 0, sizeof action);
        action.sa_handler = fault_detail::Handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_NODEFER;
        sigaction(SIGSEGV, &action, &previous_segv_);
        sigaction(SIGBUS, &action, &previous_bus_);
    }
    ~FaultProbe() {
        sigaction(SIGSEGV, &previous_segv_, nullptr);
        sigaction(SIGBUS, &previous_bus_, nullptr);
    }
    FaultProbe(const FaultProbe&) = delete;
    FaultProbe& operator=(const FaultProbe&) = delete;

    // False once the guarded region has been touched: the body is skipped on
    // the way back out of the handler.
    bool Try() { return sigsetjmp(fault_detail::g_jump, 1) == 0; }
    bool faulted() const { return fault_detail::g_faulted != 0; }

private:
    struct sigaction previous_segv_;
    struct sigaction previous_bus_;
};

// `bytes` writable bytes with an unreadable page immediately after them
// (Guard::kAfter, the default - catches reading past the end) or immediately
// before them (Guard::kBefore - catches walking backwards, which is what a
// negative stride cast to size_t does).
class GuardedArray {
public:
    enum class Guard { kAfter, kBefore };

    explicit GuardedArray(size_t bytes, Guard guard = Guard::kAfter) {
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (bytes > page) return;
        mapping_ = static_cast<unsigned char*>(mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mapping_ == MAP_FAILED) {
            mapping_ = nullptr;
            return;
        }
        length_ = 2 * page;
        const bool after = guard == Guard::kAfter;
        if (mprotect(mapping_ + (after ? page : 0), page, PROT_NONE) != 0) {
            munmap(mapping_, length_);
            mapping_ = nullptr;
            length_ = 0;
            return;
        }
        data_ = after ? mapping_ + page - bytes : mapping_ + page;
        std::memset(data_, 0, bytes);
    }
    ~GuardedArray() {
        if (mapping_ != nullptr) munmap(mapping_, length_);
    }
    GuardedArray(const GuardedArray&) = delete;
    GuardedArray& operator=(const GuardedArray&) = delete;

    unsigned char* data() const { return data_; }

private:
    unsigned char* mapping_ = nullptr;
    unsigned char* data_ = nullptr;
    size_t length_ = 0;
};

} // namespace sfpew_test
