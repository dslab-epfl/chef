/*
 * CallTracer.cpp
 *
 *  Created on: Feb 7, 2015
 *      Author: stefan
 */

#include "CallTracer.h"

extern "C" {
#include "config.h"
#include "qemu-common.h"
}

#include <s2e/S2E.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/S2EExecutionState.h>

#include <llvm/Support/Format.h>

#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {


CallStack::CallStack(uint64_t top, uint64_t sp) : top_(top) {
    frames_.push_back(shared_ptr<CallStackFrame>(
            new CallStackFrame(shared_ptr<CallStackFrame>(), 0, 0, top, sp)));
}


void CallStack::newFrame(S2EExecutionState *state, uint64_t call_site,
        uint64_t function, uint64_t sp) {
    assert(!frames_.empty());
    if (sp >= frames_.back()->bottom) {
        llvm::errs() << "Invalid stack frame start: "
                << llvm::format("ESP=0x%08x Caller=0x%08x Callee=0x%08x",
                        sp, call_site, function)
                << '\n' << *this;
    }
    assert(sp < frames_.back()->bottom);

    shared_ptr<CallStackFrame> old_frame = frames_.back();
    shared_ptr<CallStackFrame> new_frame = make_shared<CallStackFrame>(frames_.back(),
            call_site, function, frames_.back()->bottom, sp);

    frames_.push_back(new_frame);

    onStackFramePush.emit(state, this, old_frame, new_frame);
}


void CallStack::update(S2EExecutionState *state, uint64_t sp) {
    assert(!frames_.empty());

    // Unwind
    while (sp >= frames_.back()->top) {
        onStackFramePopping.emit(state, this, frames_.back(), frames_[frames_.size()-2]);
        frames_.pop_back();
        assert(!frames_.empty());
    }

    // Resize
    if (frames_.back()->bottom != sp) {
        frames_.back()->bottom = sp;
        onStackFrameResize.emit(state, this, frames_.back());
    }
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

CallTracer::CallTracer(S2E &s2e, OSTracer &os_tracer,
        boost::shared_ptr<OSThread> thread)
    : s2e_(s2e),
      exec_stream_(os_tracer.stream()),
      tracked_thread_(thread) {

    // FIXME: Obtain the upper bound from VMAs
    call_stack_ = boost::shared_ptr<CallStack>(
            new CallStack(thread->stack_top() + 8, thread->stack_top()));

    on_thread_switch_ = os_tracer.onThreadSwitch.connect(
            sigc::mem_fun(*this, &CallTracer::onThreadSwitch));
}

CallTracer::~CallTracer() {
    on_thread_switch_.disconnect();
    on_translate_register_access_.disconnect();

    s2e_tb_safe_flush();
}


void CallTracer::onTranslateRegisterAccess(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
        uint64_t rmask, uint64_t wmask, bool accessesMemory) {
    if (tracked_thread_->kernel_mode())
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
            boost::shared_ptr<OSThread> prev, boost::shared_ptr<OSThread> next) {
    if (next == tracked_thread_) {
        on_translate_register_access_ = exec_stream_.onTranslateRegisterAccessEnd.connect(
                sigc::mem_fun(*this, &CallTracer::onTranslateRegisterAccess));

        // TODO: Ideally, this should happen whenever onTranslate* callbacks are
        // modified.
        s2e_tb_safe_flush();
    } else if (prev == tracked_thread_) {
        on_translate_register_access_.disconnect();
    }
}


void CallTracer::onStackPointerModification(S2EExecutionState *state, uint64_t pc,
        bool isCall) {
    uint64_t sp = state->getSp();

#if 0
    s2e_.getMessagesStream(state)
            << llvm::format("ESP=0x%x@pc=0x%x EIP=0x%x Top=0x%x/Size=%d",
                    sp, pc, state->getPc(), tracked_thread_->stack_top(), call_stack_.size())
            << '\n';
#endif

    if (isCall) {
        call_stack_->newFrame(state, pc, state->getPc(), sp);
    } else {
        call_stack_->update(state, sp);
    }
}

} /* namespace s2e */
