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

#include "OSTracer.h"

#include <s2e/S2EExecutionState.h>
#include <s2e/S2E.h>
#include <s2e/Plugins/Opcodes.h>
#include <s2e/Chef/S2ESyscallMonitor.h>

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

// OSAddressSpace //////////////////////////////////////////////////////////////

OSAddressSpace::OSAddressSpace(boost::shared_ptr<OSTracerState> os_state,
        uint64_t page_table)
    : os_state_(os_state),
      page_table_(page_table) {

}

OSAddressSpace::OSAddressSpace(const OSAddressSpace &other)
    : os_state_(other.os_state_),
      thread_(other.thread_),
      page_table_(other.page_table_) {
    for (VMAreaMap::const_iterator it = other.memory_map_.begin(),
            ie = other.memory_map_.end(); it != ie; ++it) {
        memory_map_.insert(std::make_pair(it->first, VMArea(it->second)));
    }
}

// OSThread ////////////////////////////////////////////////////////////////////

OSThread::OSThread(shared_ptr<OSTracerState> os_state, int tid,
        shared_ptr<OSAddressSpace> address_space, uint64_t stack_top)
    : os_state_(os_state),
      address_space_(address_space),
      tid_(tid),
      stack_top_(stack_top),
      kernel_mode_(true),
      running_(false),
      terminated_(false) {

}

OSThread::OSThread(const OSThread &other)
    : os_state_(other.os_state_),
      address_space_(other.address_space_),
      tid_(other.tid_),
      name_(other.name_),
      stack_top_(other.stack_top_),
      kernel_mode_(other.kernel_mode_),
      running_(other.running_),
      terminated_(other.terminated_) {

}


llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const OSThread &thread) {
    out << thread.name() << "[" << thread.tid() << "]";
    return out;
}

// OSTracerState ///////////////////////////////////////////////////////////////

OSTracerState::OSTracerState(OSTracer &os_tracer, S2EExecutionState *s2e_state)
    : StreamAnalyzerState<OSTracerState, OSTracer>(os_tracer, s2e_state) {

}

shared_ptr<OSTracerState> OSTracerState::clone(S2EExecutionState *s2e_state) {
    shared_ptr<OSTracerState> new_state = shared_ptr<OSTracerState>(
            new OSTracerState(analyzer(), s2e_state));

    for (ThreadMap::const_iterator it = threads_.begin(),
            ie = threads_.end(); it != ie; ++it) {
        shared_ptr<OSThread> thread = shared_ptr<OSThread>(
                new OSThread(*it->second));
        shared_ptr<OSAddressSpace> address_space = shared_ptr<OSAddressSpace>(
                new OSAddressSpace(*it->second->address_space_));

        thread->os_state_ = new_state;
        thread->address_space_ = address_space;

        address_space->os_state_ = new_state;
        address_space->thread_ = thread;

        new_state->threads_.insert(std::make_pair(thread->tid_, thread));
        new_state->address_spaces_.insert(std::make_pair(
                address_space->page_table_, address_space));

        if (it->second == active_thread_) {
            new_state->active_thread_ = thread;
        }
    }

    return new_state;
}

OSThread *OSTracerState::getThread(int tid) {
    if (active_thread_ && active_thread_->tid_ == tid) {
        return active_thread_.get();
    }

    ThreadMap::iterator it = threads_.find(tid);
    if (it == threads_.end())
        return NULL;

    return it->second.get();
}

OSThread *OSTracerState::getActiveThread() {
    return active_thread_.get();
}


// OSTracer ////////////////////////////////////////////////////////////////////

OSTracer::OSTracer(S2E &s2e, ExecutionStream &estream,
        boost::shared_ptr<S2ESyscallMonitor> &smonitor)
        : StreamAnalyzer<OSTracerState>(s2e, estream) {

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


shared_ptr<OSTracerState> OSTracer::createState(S2EExecutionState *s2e_state) {
    return shared_ptr<OSTracerState>(new OSTracerState(*this, s2e_state));
}


void OSTracer::onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
                uint64_t data, uint64_t size) {
    shared_ptr<OSTracerState> os_state = getState(state);

    switch (syscall_id) {
    case S2E_THREAD_START: {
        s2e_thread_struct s2e_thread;
        assert(sizeof(s2e_thread) == size);
        if (!state->readMemoryConcrete(data, &s2e_thread, size,
                S2EExecutionState::VirtualAddress)) {
            s2e().getWarningsStream(state) << "Could not read thread descriptor" << '\n';
            return;
        }

        OSTracerState::ThreadMap::iterator it = os_state->threads_.find(s2e_thread.pid);

        if (it != os_state->threads_.end()) {
            // FIXME
            s2e().getWarningsStream(state) << "Existing thread. Cleaning old one first." << '\n';
            // Make sure we erase the old address space
            onThreadExit.emit(state, it->second.get());
            os_state->address_spaces_.erase(it->second->address_space_->page_table());
            os_state->threads_.erase(it);
        }

        shared_ptr<OSAddressSpace> address_space = shared_ptr<OSAddressSpace>(
                new OSAddressSpace(os_state, s2e_thread.address_space));
        os_state->address_spaces_.insert(std::make_pair(address_space->page_table(), address_space));

        shared_ptr<OSThread> os_thread = shared_ptr<OSThread>(new OSThread(
                os_state, s2e_thread.pid, address_space, s2e_thread.stack_top));
        address_space->thread_ = os_thread;

        if (!state->readString(s2e_thread.name, os_thread->name_, 256)) {
            s2e().getWarningsStream(state) << "Could not read thread name" << '\n';
        }

        s2e().getMessagesStream(state) << "Thread start: " << *os_thread
                << " Address space: " << llvm::format("0x%x", s2e_thread.address_space) << '\n';

        os_state->threads_.insert(std::make_pair(os_thread->tid(), os_thread));

        onThreadCreate.emit(state, os_thread.get());
        break;
    }
    case S2E_THREAD_EXIT: {
        int tid = (int)data;
        OSTracerState::ThreadMap::iterator it = os_state->threads_.find(tid);
        if (it == os_state->threads_.end()) {
            s2e().getWarningsStream(state) << "Unknown thread exiting (" << tid << "). Ignoring." << '\n';
        } else {
            s2e().getMessagesStream(state) << "Thread exit: " << *it->second << '\n';
            it->second->terminated_ = true;
            onThreadExit.emit(state, it->second.get());
            os_state->address_spaces_.erase(it->second->address_space_->page_table());
            os_state->threads_.erase(it);
        }
        break;
    }
    case S2E_VM_ALLOC: {
        s2e_vmmap_struct s2e_vmmap;
        assert(sizeof(s2e_vmmap) == size);
        if (!state->readMemoryConcrete(data, &s2e_vmmap, size,
                S2EExecutionState::VirtualAddress)) {
            s2e().getWarningsStream(state) << "Could not read thread descriptor" << '\n';
            return;
        }

        OSTracerState::ThreadMap::iterator it = os_state->threads_.find(s2e_vmmap.pid);
        if (it == os_state->threads_.end()) {
            s2e().getWarningsStream(state) << "VM map for unknown thread " << s2e_vmmap.pid << '\n';
            return;
        }

        OSAddressSpace::VMArea vm_area;

        if (!state->readString(s2e_vmmap.name, vm_area.name, 256)) {
            s2e().getWarningsStream(state) << "Could not read VM area name" << '\n';
        }

        vm_area.start = s2e_vmmap.start;
        vm_area.end = s2e_vmmap.end;

        vm_area.readable = true;
        vm_area.writable = s2e_vmmap.writable;
        vm_area.executable = s2e_vmmap.executable;

        s2e().getMessagesStream(state) << "VM area: "
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
    shared_ptr<OSTracerState> os_state = getState(state);

    if (!os_state->active_thread_) {
        return;
    }

    bool kernel_mode = (current == 0);

    if (os_state->active_thread_->kernel_mode_ != kernel_mode) {
        os_state->active_thread_->kernel_mode_ = kernel_mode;
        onThreadPrivilegeChange.emit(state, os_state->active_thread_.get(),
                kernel_mode);
    }
}


void OSTracer::onPageDirectoryChange(S2EExecutionState *state,
        uint64_t previous, uint64_t next) {
    shared_ptr<OSTracerState> os_state = getState(state);
    shared_ptr<OSThread> next_thread;

    OSTracerState::PageTableMap::iterator it = os_state->address_spaces_.find(next);
    if (it == os_state->address_spaces_.end()) {
        s2e().getWarningsStream(state) << "Unknown process scheduled: Address space "
                << llvm::format("0x%x", next) << '\n';
    } else {
        next_thread = it->second->thread();
        assert(next_thread);
    }

    if (os_state->active_thread_ != next_thread) {
        if (os_state->active_thread_) {
            assert(os_state->active_thread_->running_);
            os_state->active_thread_->running_ = false;
        }
        if (next_thread) {
            s2e().getMessagesStream(state) << "Process scheduled: " << *next_thread << '\n';
            assert(!next_thread->running_);
            next_thread->running_ = true;
        }

        shared_ptr<OSThread> old_thread = os_state->active_thread_;
        os_state->active_thread_ = next_thread;

        onThreadSwitch.emit(state, old_thread.get(), os_state->active_thread_.get());
    }
}

} /* namespace s2e */
