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

#include "CallTracer.h"

extern "C" {
#include "config.h"
#include "qemu-common.h"
}

#include <s2e/S2E.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Plugins/Opcodes.h>

#include <llvm/Support/Format.h>

#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {


CallStack::CallStack(CallTracer &call_tracer, S2EExecutionState *s2e_state)
        : StreamAnalyzerState<CallTracer>(call_tracer, s2e_state),
          next_id_(1) {
    OSThread *thread = call_tracer.os_tracer().getState(s2e_state)->getThread(
            call_tracer.tracked_tid());

    // FIXME: Obtain the upper bound from VMAs
    frames_.push_back(make_shared<CallStackFrame>(shared_ptr<CallStackFrame>(),
            next_id_++, 0, 0, thread->stack_top() + 8, thread->stack_top()));
}


CallStack::CallStack(const CallStack &other, S2EExecutionState *s2e_state)
        : StreamAnalyzerState<CallTracer>(other, s2e_state),
          next_id_(other.next_id_) {
    for (FrameVector::const_iterator it = other.frames_.begin(),
            ie = other.frames_.end(); it != ie; ++it) {
        shared_ptr<CallStackFrame> frame = make_shared<CallStackFrame>(*(*it));
        if (!frames_.empty()) {
            frame->parent = frames_.back();
        }
        frames_.push_back(frame);
    }
}


void CallStack::newFrame(uint64_t call_site, uint64_t function, uint64_t sp) {
    assert(!frames_.empty());
    if (sp >= frames_.back()->bottom) {
        llvm::errs() << "Invalid stack frame start: "
                << llvm::format("ESP=0x%08x Caller=0x%08x Callee=0x%08x",
                        sp, call_site, function)
                << '\n' << *this;
    }
    assert(sp < frames_.back()->bottom);

    shared_ptr<CallStackFrame> old_frame = frames_.back();
    shared_ptr<CallStackFrame> new_frame = make_shared<CallStackFrame>(old_frame,
            next_id_++, call_site, function, frames_.back()->bottom, sp);

    frames_.push_back(new_frame);

    analyzer().onStackFramePush.emit(s2e_state(), this, old_frame, new_frame);
}


void CallStack::updateFrame(uint64_t sp) {
    assert(!frames_.empty());

    // Unwind
    while (sp >= frames_.back()->top) {
        analyzer().onStackFramePopping.emit(s2e_state(), this, frames_.back(),
                frames_[frames_.size()-2]);
        frames_.pop_back();
        assert(!frames_.empty());
    }

    // Resize
    if (frames_.back()->bottom != sp) {
        frames_.back()->bottom = sp;
        analyzer().onStackFrameResize.emit(s2e_state(), this, frames_.back());
    }
}


void CallStack::updateBasicBlock(uint32_t bb_index) {
    assert(!frames_.empty());

    frames_.back()->bb_index = bb_index;
    analyzer().onBasicBlockEnter.emit(s2e_state(), this, frames_.back());
}


llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CallStack &cs) {
    for (unsigned i = 0; i < cs.size(); ++i) {
        os << llvm::format("#%u 0x%08x in 0x%08x [Frame: 0x%08x-0x%08x]",
                i,
                (i == 0) ? 0 : cs.frame(i-1)->call_site,
                cs.frame(i)->function,
                cs.frame(i)->bottom,
                cs.frame(i)->top);
        os << '\n';
    }
    return os;
}


////////////////////////////////////////////////////////////////////////////////

CallTracer::CallTracer(OSTracer &os_tracer, int tid)
    : StreamAnalyzer<CallStack, CallTracer>(os_tracer.s2e(), os_tracer.stream()),
      os_tracer_(os_tracer),
      tracked_tid_(tid) {

    on_thread_switch_ = os_tracer.onThreadSwitch.connect(
            sigc::mem_fun(*this, &CallTracer::onThreadSwitch));
}

CallTracer::~CallTracer() {
    on_thread_switch_.disconnect();
    on_translate_register_access_.disconnect();
    on_custom_instruction_.disconnect();

    s2e_tb_safe_flush();
}


void CallTracer::onTranslateRegisterAccess(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
        uint64_t rmask, uint64_t wmask, bool accessesMemory) {
    OSThread *thread = os_tracer_.getState(state)->getThread(tracked_tid_);
    if (thread->kernel_mode())
        return;

    // s2e_.getMessagesStream(state) << "Register access: " << llvm::format("pc=0x%x wmask=0x%016llx", pc, wmask) << '\n';

    if (!(wmask & (1 << R_ESP))) {
        return;
    }

    bool isCall = (tb->s2e_tb_type == TB_CALL || tb->s2e_tb_type == TB_CALL_IND);

    signal->connect(sigc::bind(sigc::mem_fun(
            *this, &CallTracer::onStackPointerModification), isCall));
}


void CallTracer::onThreadSwitch(S2EExecutionState *state,
            OSThread* prev, OSThread* next) {
    if (next->tid() == tracked_tid_) {
        on_custom_instruction_ = stream().onCustomInstruction.connect(
                sigc::mem_fun(*this, &CallTracer::onCustomInstruction));
        on_translate_register_access_ = stream().onTranslateRegisterAccessEnd.connect(
                sigc::mem_fun(*this, &CallTracer::onTranslateRegisterAccess));

        // TODO: Ideally, this should happen whenever onTranslate* callbacks are
        // modified.
        s2e_tb_safe_flush();
    } else if (prev && prev->tid() == tracked_tid_) {
        on_translate_register_access_.disconnect();
        on_custom_instruction_.disconnect();
    }
}


void CallTracer::onStackPointerModification(S2EExecutionState *state, uint64_t pc,
        bool isCall) {
    uint64_t sp = state->getSp();

#if 0
    s2e_.getMessagesStream(state)
            << llvm::format("ESP=0x%x@pc=0x%x EIP=0x%x Top=0x%x/Size=%d",
                    sp, pc, state->getPc(), tracked_tid_->stack_top(), call_stack_.size())
            << '\n';
#endif

    if (isCall) {
        getState(state)->newFrame(pc, state->getPc(), sp);
    } else {
        getState(state)->updateFrame(sp);
    }
}


void CallTracer::onCustomInstruction(S2EExecutionState *state, uint64_t opcode) {
    if (!OPCODE_CHECK(opcode, BASIC_BLOCK_OPCODE))
        return;

    OSThread *thread = os_tracer_.getState(state)->getThread(tracked_tid_);

    if (thread->kernel_mode())
        return;

    uint32_t bb_index = (uint32_t)((opcode >> 16) & 0xFFFF);
    getState(state)->updateBasicBlock(bb_index);
}

} /* namespace s2e */
