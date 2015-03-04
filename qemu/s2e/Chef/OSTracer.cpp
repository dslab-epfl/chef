/*
 * OSTracer.cpp
 *
 *  Created on: Jan 31, 2015
 *      Author: stefan
 */

#include "OSTracer.h"

#include <s2e/S2EExecutionState.h>
#include <s2e/S2E.h>
#include <s2e/Plugins/Opcodes.h>
#include <s2e/Chef/S2ESyscallMonitor.h>
#include <s2e/Chef/ExecutionStream.h>

#include <llvm/Support/Format.h>

using boost::shared_ptr;


////////////////////////////////////////////////////////////////////////////////
// Definitions from arch/x86/kernel/s2e.c

enum
{
    S2E_OSTRACER_START = 0xBEEF,

    S2E_THREAD_START = S2E_OSTRACER_START,
    S2E_THREAD_EXIT,
    S2E_VM_ALLOC,

    S2E_OSTRACER_END
};

struct s2e_thread_struct
{
    target_int       pid;
    target_ulong     name;
    target_ulong     start, end;
    target_ulong     stack_top;
    target_ulong     address_space;
} __attribute__((packed));

struct s2e_vmmap_struct
{
    target_int     pid;
    target_ulong   start, end;
    target_ulong   name;
    target_int     writable;
    target_int     executable;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////


namespace s2e {

// OSThread ////////////////////////////////////////////////////////////////////

OSThread::OSThread(int tid, shared_ptr<OSAddressSpace> address_space, uint64_t stack_top)
    : address_space_(address_space),
      tid_(tid),
      stack_top_(stack_top),
      kernel_mode_(true),
      running_(false),
      terminated_(false) {

}


llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const OSThread &thread) {
    out << thread.name() << "[" << thread.tid() << "]";
    return out;
}

// OSTracer ////////////////////////////////////////////////////////////////////

OSTracer::OSTracer(S2E &s2e, ExecutionStream &estream,
        boost::shared_ptr<S2ESyscallMonitor> &smonitor)
        : s2e_(s2e),
          stream_(estream) {
    on_privilege_change_ = estream.onPrivilegeChange.connect(
            sigc::mem_fun(*this, &OSTracer::onPrivilegeChange));
    on_page_directory_change_ = estream.onPageDirectoryChange.connect(
            sigc::mem_fun(*this, &OSTracer::onPageDirectoryChange));

    syscall_range_ = smonitor->registerForRange(S2E_OSTRACER_START,
            S2E_OSTRACER_END);
    syscall_range_->onS2ESystemCall.connect(
            sigc::mem_fun(*this, &OSTracer::onS2ESyscall));
}


OSTracer::~OSTracer() {
    syscall_range_->deregister();

    on_privilege_change_.disconnect();
    on_page_directory_change_.disconnect();
}


void OSTracer::onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
                uint64_t data, uint64_t size) {
    switch (syscall_id) {
    case S2E_THREAD_START: {
        s2e_thread_struct s2e_thread;
        assert(sizeof(s2e_thread) == size);
        if (!state->readMemoryConcrete(data, &s2e_thread, size, VirtualAddress)) {
            s2e_.getWarningsStream(state) << "Could not read thread descriptor" << '\n';
            return;
        }

        ThreadMap::iterator it = threads_.find(s2e_thread.pid);

        if (it != threads_.end()) {
            // FIXME
            s2e_.getWarningsStream(state) << "Existing thread. Cleaning old one first." << '\n';
            // Make sure we erase the old address space
            onThreadExit.emit(state, it->second);
            address_spaces_.erase(it->second->address_space_->page_table());
            threads_.erase(it);
        }

        shared_ptr<OSAddressSpace> address_space = shared_ptr<OSAddressSpace>(
                new OSAddressSpace(s2e_thread.address_space));
        address_spaces_.insert(std::make_pair(address_space->page_table(), address_space));

        shared_ptr<OSThread> os_thread = shared_ptr<OSThread>(new OSThread(
                s2e_thread.pid, address_space, s2e_thread.stack_top));
        address_space->thread_ = os_thread;

        if (!state->mem()->readString(s2e_thread.name, os_thread->name_, 256)) {
            s2e_.getWarningsStream(state) << "Could not read thread name" << '\n';
        }

        s2e_.getMessagesStream(state) << "Thread start: " << *os_thread
                << " Address space: " << llvm::format("0x%x", s2e_thread.address_space) << '\n';

        threads_.insert(std::make_pair(os_thread->tid_, os_thread));

        onThreadCreate.emit(state, os_thread);
        break;
    }
    case S2E_THREAD_EXIT: {
        int tid = (int)data;
        ThreadMap::iterator it = threads_.find(tid);
        if (it == threads_.end()) {
            s2e_.getWarningsStream(state) << "Unknown thread exiting (" << tid << "). Ignoring." << '\n';
        } else {
            s2e_.getMessagesStream(state) << "Thread exit: " << *it->second << '\n';
            it->second->terminated_ = true;
            onThreadExit.emit(state, it->second);
            address_spaces_.erase(it->second->address_space_->page_table());
            threads_.erase(it);
        }
        break;
    }
    case S2E_VM_ALLOC: {
        s2e_vmmap_struct s2e_vmmap;
        assert(sizeof(s2e_vmmap) == size);
        if (!state->readMemoryConcrete(data, &s2e_vmmap, size, VirtualAddress)) {
            s2e_.getWarningsStream(state) << "Could not read thread descriptor" << '\n';
            return;
        }

        ThreadMap::iterator it = threads_.find(s2e_vmmap.pid);
        if (it == threads_.end()) {
            s2e_.getWarningsStream(state) << "VM map for unknown thread " << s2e_vmmap.pid << '\n';
            return;
        }

        OSAddressSpace::VMArea vm_area;

        if (!state->mem()->readString(s2e_vmmap.name, vm_area.name, 256)) {
            s2e_.getWarningsStream(state) << "Could not read VM area name" << '\n';
        }

        vm_area.start = s2e_vmmap.start;
        vm_area.end = s2e_vmmap.end;

        vm_area.readable = true;
        vm_area.writable = s2e_vmmap.writable;
        vm_area.executable = s2e_vmmap.executable;

        s2e_.getMessagesStream(state) << "VM area: "
                << llvm::format("0x%x-0x%x", vm_area.start, vm_area.end)
                << " " << vm_area.name << '\n';

        it->second->address_space_->memory_map_.insert(
                std::make_pair(vm_area.start, vm_area));
        break;
    }
    }
}


void OSTracer::onPrivilegeChange(S2EExecutionState *state,
        unsigned previous, unsigned current) {
    if (!active_thread_) {
        return;
    }

    bool kernel_mode = (current == 0);

    if (active_thread_->kernel_mode_ != kernel_mode) {
        active_thread_->kernel_mode_ = kernel_mode;
        onThreadPrivilegeChange.emit(state, active_thread_, kernel_mode);
    }
}


void OSTracer::onPageDirectoryChange(S2EExecutionState *state,
        uint64_t previous, uint64_t next) {
    PageTableMap::iterator it = address_spaces_.find(next);
    if (it == address_spaces_.end()) {
        s2e_.getWarningsStream(state) << "Unknown process scheduled: Address space "
                << llvm::format("0x%x", next) << '\n';
        return;
    }

    OSThreadRef next_thread = it->second->thread();
    assert(next_thread);

    if (active_thread_ != next_thread) {
        if (active_thread_) {
            assert(active_thread_->running_);
            active_thread_->running_ = false;
        }
        assert(!next_thread->running_);
        next_thread->running_ = true;

        s2e_.getMessagesStream(state) << "Process scheduled: " << *next_thread << '\n';

        OSThreadRef old_thread = active_thread_;
        active_thread_ = next_thread;

        onThreadSwitch.emit(state, old_thread, active_thread_);
    }
}

} /* namespace s2e */
