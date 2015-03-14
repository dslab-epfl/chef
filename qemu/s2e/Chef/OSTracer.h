/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2015, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Currently maintained by:
 *    Stefan Bucur <stefan.bucur@epfl.ch>
 *
 * All contributors are listed in the S2E-AUTHORS file.
 */

#ifndef QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_
#define QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_

#include <s2e/Signals/Signals.h>
#include <s2e/Chef/StreamAnalyzer.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <stdint.h>
#include <map>

namespace llvm {
class raw_ostream;
}

namespace s2e {


class S2ESyscallMonitor;
class S2ESyscallRange;

class OSThread;

class OSTracerState;

// TODO: Maybe at some point we can differentiate between processes and
// address spaces.
class OSAddressSpace {
public:
    struct VMArea {
        std::string name;
        uint64_t start;
        uint64_t end;

        bool readable;
        bool writable;
        bool executable;

        VMArea() : name(),
                   start(0), end(0),
                   readable(false), writable(false), executable(false) {}

        VMArea(const VMArea &other)
            : name(other.name),
              start(other.start), end(other.end),
              readable(other.readable), writable(other.writable), executable(other.executable) {

        }

    private:
        void operator=(VMArea&);
    };

public:
    OSTracerState *os_state() const {
        return os_state_.lock().get();
    }

    uint64_t page_table() const {
        return page_table_;
    }

    // FIXME An address space can be shared by several threads
    boost::shared_ptr<OSThread> thread() {
        return thread_.lock();
    }

private:
    OSAddressSpace(boost::shared_ptr<OSTracerState> os_state,
            uint64_t page_table);
    OSAddressSpace(const OSAddressSpace &other);

    typedef std::map<uint64_t, VMArea> VMAreaMap;

    boost::weak_ptr<OSTracerState> os_state_;
    // FIXME: In general, one or more threads share an address space.
    boost::weak_ptr<OSThread> thread_;

    uint64_t page_table_;
    VMAreaMap memory_map_;

    friend class OSTracer;
    friend class OSTracerState;

    void operator=(OSAddressSpace&);
};


class OSThread {
public:
    int tid() const {
        return tid_;
    }

    const std::string &name() const {
        return name_;
    }

    bool kernel_mode() const {
        return kernel_mode_;
    }

    bool running() const {
        return running_;
    }

    bool terminated() const {
        return terminated_;
    }

    OSAddressSpace* address_space() const {
        return address_space_.get();
    }

    uint64_t stack_top() const {
        return stack_top_;
    }

    OSTracerState *os_state() const {
        return os_state_.lock().get();
    }

private:
    OSThread(boost::shared_ptr<OSTracerState> os_state, int tid,
            boost::shared_ptr<OSAddressSpace> address_space,
            uint64_t stack_top);
    OSThread(const OSThread &other);

    boost::weak_ptr<OSTracerState> os_state_;
    boost::shared_ptr<OSAddressSpace> address_space_;

    int tid_;
    std::string name_;

    uint64_t stack_top_;
    bool kernel_mode_;
    bool running_;
    bool terminated_;

    friend class OSTracer;
    friend class OSTracerState;

    void operator=(const OSThread&);
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const OSThread &thread);


class OSTracer;


class OSTracerState : public StreamAnalyzerState<OSTracerState, OSTracer> {
public:
    OSThread *getThread(int tid);
    OSThread *getActiveThread();

    boost::shared_ptr<OSTracerState> clone(S2EExecutionState *s2e_state);

private:
    typedef std::map<int, boost::shared_ptr<OSThread> > ThreadMap;
    typedef std::map<uint64_t, boost::shared_ptr<OSAddressSpace> > PageTableMap;

    OSTracerState(OSTracer &os_tracer, S2EExecutionState *s2e_state);

    ThreadMap threads_;
    PageTableMap address_spaces_;

    boost::shared_ptr<OSThread> active_thread_;

    friend class OSTracer;
};


class OSTracer : public StreamAnalyzer<OSTracerState> {
public:
    OSTracer(S2E &s2e, ExecutionStream &estream,
            boost::shared_ptr<S2ESyscallMonitor> &smonitor);
    ~OSTracer();

    sigc::signal<void, S2EExecutionState*, OSThread*> onThreadCreate;
    sigc::signal<void, S2EExecutionState*, OSThread*> onThreadExit;
    sigc::signal<void, S2EExecutionState*, OSThread*, OSThread*> onThreadSwitch;
    sigc::signal<void, S2EExecutionState*, OSThread*, bool> onThreadPrivilegeChange;

protected:
    boost::shared_ptr<OSTracerState> createState(S2EExecutionState *s2e_state);

private:
    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
                uint64_t data, uint64_t size);
    void onPrivilegeChange(S2EExecutionState *state,
            unsigned previous, unsigned current);
    void onPageDirectoryChange(S2EExecutionState *state,
            uint64_t previous, uint64_t current);

    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    sigc::connection on_privilege_change_;
    sigc::connection on_page_directory_change_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_ */
