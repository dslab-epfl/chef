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

OSThread::OSThread(OSTracer &tracer, int tid, uint64_t address_space)
    : tid_(tid),
      address_space_(address_space),
      kernel_mode_(true),
      exec_stream_(tracer.exec_stream()),
      user_exec_stream_(tracer.exec_stream()) {

}

// OSTracer ////////////////////////////////////////////////////////////////////

OSTracer::OSTracer(S2E &s2e, ExecutionStream &estream)
        : s2e_(s2e), exec_stream_(estream) {
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
    // We only look at the syscall convention
    uint8_t opc = (arg>>8) & 0xFF;
    if (opc != 0xB0)
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
        s2e_.getMessagesStream(state) << "Thread start: " << s2e_thread.pid
                << " Address space: " << llvm::format("0x%x", s2e_thread.address_space) << '\n';

        ThreadMap::iterator it = threads_.find(s2e_thread.pid);

        shared_ptr<OSThread> os_thread = shared_ptr<OSThread>(new OSThread(*this,
                s2e_thread.pid, s2e_thread.address_space));

        if (it != threads_.end()) {
            s2e_.getWarningsStream(state) << "Existing thread. Cleaning old one first." << '\n';
            // Make sure we erase the old address space
            address_spaces_.erase(it->second->address_space_);
            threads_.erase(it);
        }

        threads_.insert(std::make_pair(os_thread->tid_, os_thread));
        address_spaces_.insert(std::make_pair(os_thread->address_space_, os_thread));
        break;
    }
    case S2E_THREAD_EXIT: {
        int tid = (int)data;
        s2e_.getMessagesStream(state) << "Thread exit: " << tid << '\n';
        ThreadMap::iterator it = threads_.find(tid);
        if (it == threads_.end()) {
            s2e_.getWarningsStream(state) << "Unknown thread exiting. Ignoring." << '\n';
        } else {
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
    if (!active_) {
        return;
    }

    bool kernel_mode = (current == 0);

    if (active_->kernel_mode_ != kernel_mode) {
        active_->kernel_mode_ = kernel_mode;
        if (active_->kernel_mode_) {
            active_->user_exec_stream_.disconnect();
        } else {
            active_->user_exec_stream_.connect();
        }
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

    s2e_.getMessagesStream(state) << "Process scheduled: " << it->second->tid() << '\n';

    if (active_) {
        assert(active_->kernel_mode_);
        active_->exec_stream_.disconnect();
    }

    active_ = it->second;

    assert(active_->kernel_mode_);
    active_->exec_stream_.connect();
}

} /* namespace s2e */
