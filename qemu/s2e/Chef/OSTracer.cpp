/*
 * OSTracer.cpp
 *
 *  Created on: Jan 31, 2015
 *      Author: stefan
 */

#include "OSTracer.h"

#include <s2e/ExecutionStream.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/S2E.h>
#include <s2e/Plugins/Opcodes.h>

#include <llvm/Support/Format.h>

using boost::shared_ptr;


////////////////////////////////////////////////////////////////////////////////
// Definitions from arch/x86/kernel/s2e.c

enum
{
    S2E_THREAD_START = 0xBEEF,
    S2E_THREAD_EXIT,
    S2E_VM_ALLOC
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

OSThread::OSThread(OSTracer &tracer, int tid, uint64_t address_space,
        uint64_t stack_top)
    : tid_(tid),
      address_space_(address_space),
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

OSTracer::OSTracer(S2E &s2e, ExecutionStream &estream)
        : s2e_(s2e) {
    on_custom_instruction_ = estream.onCustomInstruction.connect(
            sigc::mem_fun(*this, &OSTracer::onCustomInstruction));
    on_privilege_change_ = estream.onPrivilegeChange.connect(
            sigc::mem_fun(*this, &OSTracer::onPrivilegeChange));
    on_page_directory_change_ = estream.onPageDirectoryChange.connect(
            sigc::mem_fun(*this, &OSTracer::onPageDirectoryChange));
}


OSTracer::~OSTracer() {
    on_custom_instruction_.disconnect();
    on_privilege_change_.disconnect();
    on_page_directory_change_.disconnect();
}


void OSTracer::onCustomInstruction(S2EExecutionState *state, uint64_t arg) {
    // FIXME: Factor this out in a mixin or base class

    // We only look at the syscall convention
    if (!OPCODE_CHECK(arg, SYSCALL_OPCODE))
        return;

    target_uint syscall_id = 0;
    target_ulong data = 0;
    target_uint size = 0;
    bool success = true;

    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EAX]),
            &syscall_id, sizeof(syscall_id));
    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_ECX]),
            &data, sizeof(data));
    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EDX]),
            &size, sizeof(size));

    if (!success) {
        s2e_.getWarningsStream(state) << "Could not read syscall parameters" << '\n';
        return;
    }

    switch (syscall_id) {
    case S2E_THREAD_START: {
        s2e_thread_struct s2e_thread;
        assert(sizeof(s2e_thread) == size);
        if (!state->readMemoryConcrete(data, &s2e_thread, size, VirtualAddress)) {
            s2e_.getWarningsStream(state) << "Could not read thread descriptor" << '\n';
            return;
        }

        ThreadMap::iterator it = threads_.find(s2e_thread.pid);

        shared_ptr<OSThread> os_thread = shared_ptr<OSThread>(new OSThread(*this,
                s2e_thread.pid, s2e_thread.address_space, s2e_thread.stack_top));

        if (!state->mem()->readString(s2e_thread.name, os_thread->name_, 256)) {
            s2e_.getWarningsStream(state) << "Could not read thread name" << '\n';
        }

        s2e_.getMessagesStream(state) << "Thread start: " << *os_thread
                << " Address space: " << llvm::format("0x%x", s2e_thread.address_space) << '\n';

        if (it != threads_.end()) {
            // FIXME
            s2e_.getWarningsStream(state) << "Existing thread. Cleaning old one first." << '\n';
            // Make sure we erase the old address space
            onThreadExit.emit(state, it->second);
            address_spaces_.erase(it->second->address_space_);
            threads_.erase(it);
        }

        threads_.insert(std::make_pair(os_thread->tid_, os_thread));
        address_spaces_.insert(std::make_pair(os_thread->address_space_, os_thread));
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
            address_spaces_.erase(it->second->address_space_);
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

#if 0
        if (active_thread_->kernel_mode_) {
            active_thread_->user_exec_stream_.disconnect();
        } else {
            active_thread_->user_exec_stream_.connect();
        }
#endif
    }
}


void OSTracer::onPageDirectoryChange(S2EExecutionState *state,
        uint64_t previous, uint64_t current) {
    AddressSpaceMap::iterator it = address_spaces_.find(current);
    if (it == address_spaces_.end()) {
        s2e_.getWarningsStream(state) << "Unknown process scheduled: Address space "
                << llvm::format("0x%x", current) << '\n';
        return;
    }

#if 0
    if (active_thread_) {
        assert(active_thread_->kernel_mode_);
        active_thread_->exec_stream_.disconnect();
    }
#endif

    if (active_thread_ != it->second) {
        if (active_thread_) {
            assert(active_thread_->running_);
            active_thread_->running_ = false;
        }
        assert(!it->second->running_);
        it->second->running_ = true;

        s2e_.getMessagesStream(state) << "Process scheduled: " << *it->second << '\n';

        OSThreadRef old_thread = active_thread_;
        active_thread_ = it->second;

        onThreadSwitch.emit(state, old_thread, active_thread_);
    }

#if 0
    assert(active_thread_->kernel_mode_);
    active_thread_->exec_stream_.connect();
#endif
}

} /* namespace s2e */
