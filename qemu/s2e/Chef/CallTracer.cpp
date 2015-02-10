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

namespace s2e {


CallStack::CallStack(uint64_t top, uint64_t sp) : top_(top) {
    frames_.push_back(CallStackFrame(0, 0, top, sp));
}


void CallStack::newFrame(uint64_t call_site, uint64_t function, uint64_t sp) {
    assert(!frames_.empty());
    assert(sp < frames_.back().bottom);

    frames_.push_back(CallStackFrame(call_site, function, frames_.back().bottom, sp));
    onStackFramePush.emit(this);
}


void CallStack::update(uint64_t sp) {
    assert(!frames_.empty());
    // Unwind
    while (sp >= frames_.back().top) {
        frames_.pop_back();
        onStackFramePop.emit(this);
        assert(!frames_.empty());
    }

    // Resize
    if (frames_.back().bottom != sp) {
        frames_.back().bottom = sp;
        onStackFrameResize.emit(this);
    }
}


////////////////////////////////////////////////////////////////////////////////

CallTracer::CallTracer(S2E &s2e, ExecutionStream &estream, OSTracer &os_tracer,
        boost::shared_ptr<OSThread> thread)
    : s2e_(s2e),
      exec_stream_(estream),
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
        call_stack_->newFrame(pc, state->getPc(), sp);
    } else {
        call_stack_->update(sp);
    }
}

} /* namespace s2e */
