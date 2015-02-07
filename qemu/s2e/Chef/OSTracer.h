/*
 * OSTracer.h
 *
 *  Created on: Jan 31, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_
#define QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_

#include <s2e/Signals/Signals.h>
#include <s2e/ExecutionStream.h>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <stdint.h>
#include <map>

namespace s2e {


class ExecutionStream;
class ExecutionStreamFilter;
class S2E;


class OSVirtualMemoryArea {
public:
    OSVirtualMemoryArea()
        : name(),
          start(0), end(0),
          readable(false), writable(false), executable(false) {}

    std::string name;
    uint64_t start;
    uint64_t end;

    bool readable;
    bool writable;
    bool executable;
};


class OSAddressSpace {
public:
    OSAddressSpace() {}

private:
    std::map<uint64_t, OSVirtualMemoryArea> memory_map_;
};


class OSTracer;


class OSThread {
public:
    int tid() const {
        return tid_;
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

private:
    OSThread(OSTracer &tracer, int tid, uint64_t address_space);

    int tid_;
    uint64_t address_space_;
    bool kernel_mode_;
    bool running_;
    bool terminated_;

    friend class OSTracer;
};


typedef boost::shared_ptr<OSThread> OSThreadRef;


class OSThreadMatcher {
public:
    OSThreadMatcher(OSThreadRef thread, bool include_kernel)
        : thread_(thread),
          include_kernel_(include_kernel) {

    }

    bool operator()() {
        return thread_->running() && (include_kernel_ || !thread_->kernel_mode());
    }

private:
    OSThreadRef thread_;
    bool include_kernel_;
};


class OSTracer {
public:
    OSTracer(S2E &s2e, ExecutionStream &estream);
    ~OSTracer();

    ExecutionStream &exec_stream() {
        return exec_stream_;
    }

    sigc::signal<void, S2EExecutionState*, OSThreadRef> onThreadCreate;
    sigc::signal<void, S2EExecutionState*, OSThreadRef> onThreadExit;
    sigc::signal<void, S2EExecutionState*, OSThreadRef, OSThreadRef> onThreadSwitch;

private:
    typedef std::map<int, OSThreadRef> ThreadMap;
    typedef std::map<uint64_t, OSThreadRef> AddressSpaceMap;

    S2E &s2e_;
    ExecutionStream &exec_stream_;

    sigc::connection on_custom_instruction_;
    sigc::connection on_privilege_change_;
    sigc::connection on_page_directory_change_;

    ThreadMap threads_;
    AddressSpaceMap address_spaces_;

    boost::shared_ptr<OSThread> active_thread_;

    void onCustomInstruction(S2EExecutionState *state, uint64_t arg);
    void onPrivilegeChange(S2EExecutionState *state,
            unsigned previous, unsigned current);
    void onPageDirectoryChange(S2EExecutionState *state,
            uint64_t previous, uint64_t current);
};

} /* namespace s2e */

#endif /* QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_ */
