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
#include <boost/weak_ptr.hpp>

#include <stdint.h>
#include <map>

namespace llvm {
class raw_ostream;
}

namespace s2e {


class ExecutionStream;
class ExecutionStreamFilter;
class S2E;
class S2ESyscallMonitor;
class S2ESyscallRange;

class OSThread;

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
    };

public:
    uint64_t page_table() const {
        return page_table_;
    }

    // FIXME
    boost::shared_ptr<OSThread> thread() {
        return thread_.lock();
    }

private:
    OSAddressSpace(uint64_t page_table)
        : page_table_(page_table) {

    }

    uint64_t page_table_;
    std::map<uint64_t, VMArea> memory_map_;

    // FIXME: In general, one or more threads share an address space.
    boost::weak_ptr<OSThread> thread_;

    friend class OSTracer;
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

    boost::shared_ptr<OSAddressSpace> address_space() const {
        return address_space_;
    }

    uint64_t stack_top() const {
        return stack_top_;
    }

private:
    OSThread(int tid, boost::shared_ptr<OSAddressSpace> address_space,
            uint64_t stack_top);

    boost::shared_ptr<OSAddressSpace> address_space_;

    int tid_;
    std::string name_;

    uint64_t stack_top_;
    bool kernel_mode_;
    bool running_;
    bool terminated_;

    friend class OSTracer;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const OSThread &thread);


typedef boost::shared_ptr<OSThread> OSThreadRef;


class OSTracer {
public:
    OSTracer(S2E &s2e, ExecutionStream &estream,
            boost::shared_ptr<S2ESyscallMonitor> &smonitor);
    ~OSTracer();

    ExecutionStream &stream() {
        return stream_;
    }

    sigc::signal<void, S2EExecutionState*, OSThreadRef> onThreadCreate;
    sigc::signal<void, S2EExecutionState*, OSThreadRef> onThreadExit;
    sigc::signal<void, S2EExecutionState*, OSThreadRef, OSThreadRef> onThreadSwitch;
    sigc::signal<void, S2EExecutionState*, OSThreadRef, bool> onThreadPrivilegeChange;

private:
    typedef std::map<int, OSThreadRef> ThreadMap;
    typedef std::map<uint64_t, boost::shared_ptr<OSAddressSpace> > PageTableMap;

    S2E &s2e_;
    ExecutionStream &stream_;
    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    sigc::connection on_privilege_change_;
    sigc::connection on_page_directory_change_;

    ThreadMap threads_;
    PageTableMap address_spaces_;

    OSThreadRef active_thread_;

    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
                uint64_t data, uint64_t size);
    void onPrivilegeChange(S2EExecutionState *state,
            unsigned previous, unsigned current);
    void onPageDirectoryChange(S2EExecutionState *state,
            uint64_t previous, uint64_t current);
};

} /* namespace s2e */

#endif /* QEMU_S2E_PLUGINS_CHEF_OSTRACER_H_ */
