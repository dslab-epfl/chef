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

#include "InterpreterTracer.h"

#include <llvm/Support/CommandLine.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {

namespace {

llvm::cl::opt<bool>
DebugInstructions("debug-interp-instructions",
        llvm::cl::desc("Print detected interpreter instructions"),
        llvm::cl::init(false));

}

// HighLevelStack //////////////////////////////////////////////////////////////

HighLevelStack::HighLevelStack(InterpreterTracer &tracer,
        S2EExecutionState *s2e_state)
    : StreamAnalyzerState<HighLevelStack, InterpreterTracer>(tracer, s2e_state) {

}


shared_ptr<HighLevelStack> HighLevelStack::clone(S2EExecutionState *s2e_state) {
    shared_ptr<HighLevelStack> new_state = shared_ptr<HighLevelStack>(
            new HighLevelStack(analyzer(), s2e_state));
    for (FrameVector::const_iterator it = frames_.begin(),
            ie = frames_.end(); it != ie; ++it) {
        shared_ptr<HighLevelFrame> frame = make_shared<HighLevelFrame>(*(*it));
        if (!new_state->frames_.empty()) {
            frame->parent = new_state->frames_.back();
        }
        new_state->frames_.push_back(frame);
    }
    return new_state;
}

// InterpreterTracer ///////////////////////////////////////////////////////////

InterpreterTracer::InterpreterTracer(CallTracer &call_tracer)
    : StreamAnalyzer<HighLevelStack>(call_tracer.s2e(), call_tracer.stream()),
      os_tracer_(call_tracer.os_tracer()),
      call_tracer_(call_tracer) {

    on_state_switch_ = stream().onStateSwitch.connect(
            sigc::mem_fun(*this, &InterpreterTracer::onStateSwitch));
}


InterpreterTracer::~InterpreterTracer() {
    on_concrete_data_memory_access_.disconnect();
    on_symbolic_data_memory_access_.disconnect();
    on_stack_frame_push_.disconnect();
    on_stack_frame_popping_.disconnect();
    on_state_switch_.disconnect();
}


void InterpreterTracer::setInterpreterStructureParams(S2EExecutionState *state,
        const InterpreterStructureParams &params) {
    interp_params_ = params;

    HighLevelStack *hl_stack = getState(state).get();
    CallStack *ll_stack = call_tracer_.getState(state).get();

    hl_stack->frames_.clear();

    for (unsigned i = 0; i < ll_stack->size(); ++i) {
        shared_ptr<CallStackFrame> ll_frame = ll_stack->frame(
                ll_stack->size() - i - 1);
        if (ll_frame->function == interp_params_.interp_loop_function) {
            if (hl_stack->frames_.empty()) {
                hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                        ll_frame->id));
            } else {
                hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                        hl_stack->frames_.back(), ll_frame->id));
            }
        }
    }

    on_concrete_data_memory_access_.disconnect();
    on_symbolic_data_memory_access_.disconnect();
    on_stack_frame_push_.disconnect();
    on_stack_frame_popping_.disconnect();

    on_stack_frame_push_ = call_tracer_.onStackFramePush.connect(
            sigc::mem_fun(*this, &InterpreterTracer::onLowLevelStackFramePush));
    on_stack_frame_popping_ = call_tracer_.onStackFramePopping.connect(
            sigc::mem_fun(*this, &InterpreterTracer::onLowLevelStackFramePopping));
}


shared_ptr<HighLevelStack> InterpreterTracer::createState(S2EExecutionState *s2e_state) {
    return shared_ptr<HighLevelStack>(new HighLevelStack(*this, s2e_state));
}


void InterpreterTracer::pushHighLevelFrame(CallStack *call_stack,
        HighLevelStack *hl_stack) {
    // Enter a new interpretation frame
    if (hl_stack->frames_.empty()) {
        hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                call_stack->top()->id));
    } else {
        hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                hl_stack->frames_.back(), call_stack->top()->id));
    }

    onHighLevelFramePush.emit(call_stack->s2e_state(), hl_stack);

#if 1
    s2e().getMessagesStream(call_stack->s2e_state())
            << "Enter high-level frame. Stack size: "
            << hl_stack->frames_.size() << '\n';
#endif
}


void InterpreterTracer::popHighLevelFrame(CallStack *call_stack,
        HighLevelStack *hl_stack) {

    onHighLevelFramePopping.emit(call_stack->s2e_state(), hl_stack);

    hl_stack->frames_.pop_back();

#if 1
    s2e().getMessagesStream(call_stack->s2e_state())
            << "Leaving high-level frame. Stack size: "
            << hl_stack->frames_.size() << '\n';
#endif
}


void InterpreterTracer::onConcreteDataMemoryAccess(S2EExecutionState *state,
        uint64_t address, uint64_t value, uint8_t size, unsigned flags) {
    OSThread *thread = os_tracer_.getState(state)->getThread(call_tracer_.tracked_tid());

    if (!thread->running() || thread->kernel_mode()) {
        return;
    }
    // XXX: Hack, hack, hack: We filter out memory accesses in the kernel
    // space that occur as part of Qemu's interrupt handling preparation,
    // which happens before the task's privilege level is updated.
    // See op_helper.c, function do_interrupt_protected.
    if (address >= 0xc0000000) {
#if 0
        s2e().getWarningsStream(state) << "Spurious kernel-space memory access from userland. Skipping." << '\n';
#endif
        return;
    }

    bool is_write = flags & S2E_MEM_TRACE_FLAG_WRITE;

    shared_ptr<CallStack> ll_stack = call_tracer_.getState(state);

    shared_ptr<HighLevelStack> hl_stack = getState(state);
    assert(hl_stack->size() > 0);
    HighLevelFrame *hl_frame = hl_stack->top().get();
    if (ll_stack->top()->id != hl_frame->low_level_frame_id) {
        s2e().getMessagesStream(state) << "HL frame ID does not match LL frame ID. "
                << "Assuming HL stack unwind." << '\n';
    }

    if (state->getPc() == interp_params_.hlpc_update_pc) {
        if (!is_write) {
            s2e().getWarningsStream(state) << "Unexpected memory access: "
                    << llvm::format("EIP=0x%x Addr=0x%x Value=0x%x Size=%d Flags=0x%08x",
                            state->getPc(), address, value, size, flags)
                    << '\n';
        }
        assert(is_write);

        if (!hl_frame->hlpc_ptr) {
            hl_frame->hlpc_ptr = address;
        } else if (hl_frame->hlpc_ptr != address){
            s2e().getMessagesStream(state)
                    << "Different HLPC location used within the same LL frame. " <<
                    "Assuming different HL frame." << '\n';

            bool is_return = false;
            for (unsigned i = 0; i < hl_stack->size(); ++i) {
                if (hl_stack->frame(i)->hlpc_ptr == address) {
                    is_return = true;
                    break;
                }
            }

            if (is_return) {
                while (hl_stack->top()->hlpc_ptr != address) {
                    popHighLevelFrame(ll_stack.get(), hl_stack.get());
                }
            } else {
                pushHighLevelFrame(ll_stack.get(), hl_stack.get());
                hl_stack->top()->hlpc_ptr = address;
            }
            hl_frame = hl_stack->top().get();
        }
    }

    if (address == hl_frame->hlpc_ptr && is_write) {
        hl_frame->hlpc = value;
        onHighLevelPCUpdate.emit(state, hl_stack.get());

        if (DebugInstructions) {
            s2e().getMessagesStream(state)
                        << llvm::format("HLPC=0x%x", hl_frame->hlpc) << '\n';
        }
    }

    if (state->getPc() == interp_params_.instruction_fetch_pc) {
        if (is_write) {
            s2e().getWarningsStream(state) << "Unexpected memory access: "
                    << llvm::format("EIP=0x%x Addr=0x%x Value=0x%x Size=%d Flags=0x%08x",
                            state->getPc(), address, value, size, flags)
                    << '\n';
        }
        assert(!is_write);
        //assert(!hl_frame->hlpc || address == hl_frame->hlpc);
        hl_frame->hlinst = address;
        onHighLevelInstructionFetch.emit(state, hl_stack.get());
        if (DebugInstructions) {
            s2e().getMessagesStream(state)
                    << llvm::format("Instruction=0x%x", hl_frame->hlinst) << '\n';
        }
    }
}


void InterpreterTracer::onSymbolicDataMemoryAccess(S2EExecutionState *state,
        klee::ref<klee::Expr> address, uint64_t concr_addr, bool &concretize) {
    // TODO Make sure here that no symbolic HLPC updates ever occur
}


void InterpreterTracer::onLowLevelStackFramePush(CallStack *call_stack,
        shared_ptr<CallStackFrame> old_top,
        shared_ptr<CallStackFrame> new_top) {
    updateMemoryTracking(new_top);

    if (new_top->function == interp_params_.interp_loop_function) {
        HighLevelStack *hl_stack = getState(call_stack->s2e_state()).get();
        pushHighLevelFrame(call_stack, hl_stack);
    }
}


void InterpreterTracer::onLowLevelStackFramePopping(CallStack *call_stack,
        shared_ptr<CallStackFrame> old_top,
        shared_ptr<CallStackFrame> new_top) {
    updateMemoryTracking(new_top);

    if (old_top->function == interp_params_.interp_loop_function) {
        // Return from the interpretation frame
        HighLevelStack *hl_stack = getState(call_stack->s2e_state()).get();
        assert(!hl_stack->frames_.empty());
        popHighLevelFrame(call_stack, hl_stack);
    }
}


void InterpreterTracer::onStateSwitch(S2EExecutionState *prev,
        S2EExecutionState *next) {
    CallStack *call_stack = call_tracer_.getState(next).get();
    assert(call_stack->size() > 0);
    updateMemoryTracking(call_stack->top());
}


void InterpreterTracer::updateMemoryTracking(boost::shared_ptr<CallStackFrame> top) {
    if (top->function != interp_params_.interp_loop_function) {
        on_concrete_data_memory_access_.disconnect();
        on_symbolic_data_memory_access_.disconnect();
        return;
    }

    if (!on_concrete_data_memory_access_.connected()) {
        on_concrete_data_memory_access_ = os_tracer_.stream().
            onConcreteDataMemoryAccess.connect(sigc::mem_fun(
                    *this, &InterpreterTracer::onConcreteDataMemoryAccess));
    }
    if (!on_symbolic_data_memory_access_.connected()) {
        on_symbolic_data_memory_access_ = os_tracer_.stream().
            onSymbolicMemoryAddress.connect(sigc::mem_fun(
                    *this, &InterpreterTracer::onSymbolicDataMemoryAccess));
    }
}

} /* namespace s2e */
