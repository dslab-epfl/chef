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
    StackFrame root;
    root.call_site = 0;
    root.top = top;
    root.bottom = sp;

    frames_.push_back(root);
}

void CallStack::update(S2EExecutionState *state, uint64_t pc, uint64_t sp,
        bool is_call) {
    assert(!frames_.empty());

    if (is_call) {
        assert(sp < frames_.back().bottom);

        StackFrame frame;
        frame.call_site = pc;
        frame.top = frames_.back().bottom;
        frame.bottom = sp;

        frames_.push_back(frame);
    }

    // Unwind
    while (sp >= frames_.back().top) {
        frames_.pop_back();
        assert(!frames_.empty());
    }

    // Resize
    frames_.back().bottom = sp;
}


////////////////////////////////////////////////////////////////////////////////

CallTracer::CallTracer(S2E &s2e, ExecutionStream &estream, OSTracer &os_tracer,
        boost::shared_ptr<OSThread> thread)
    : s2e_(s2e),
      exec_stream_(estream),
      tracked_thread_(thread),
      call_stack_(thread->stack_top() + 8, thread->stack_top()) { // FIXME

    on_thread_privilege_change_ = os_tracer.onThreadPrivilegeChange.connect(
            sigc::mem_fun(*this, &CallTracer::onThreadPrivilegeChange));
}

CallTracer::~CallTracer() {
    // XXX: Bug, the per-translation block signals are never disconnected.
    on_thread_privilege_change_.disconnect();
    on_translate_block_start_.disconnect();
    on_translate_register_access_.disconnect();
    s2e_tb_safe_flush();
}


void CallTracer::onTranslateBlockStart(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *tb, uint64_t pc) {
    s2e_.getMessagesStream(state) << "Block translated at " << llvm::format("0x%x", pc) << '\n';
}


void CallTracer::onTranslateRegisterAccess(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
        uint64_t rmask, uint64_t wmask, bool accessesMemory) {
    // The stack is manipulated by operating the ESP register (either by a CALL
    // instruction or by explicit register write).

    // s2e_.getMessagesStream(state) << "Register access: " << llvm::format("pc=0x%x wmask=0x%016llx", pc, wmask) << '\n';

    if (!(wmask & (1 << R_ESP))) {
        return;
    }

    bool isCall = (tb->s2e_tb_type == TB_CALL || tb->s2e_tb_type == TB_CALL_IND);

    signal->connect(sigc::bind(sigc::mem_fun(
            *this, &CallTracer::onStackPointerModification), isCall));
}


void CallTracer::onThreadPrivilegeChange(S2EExecutionState *state,
        boost::shared_ptr<OSThread> thread, bool kernel_mode) {
    // TODO: Use a filter object, create per-thread connectors, or filtered
    // execution streams.  Execution streams could also be specified as a
    // hierarchy (user stream -> machine stream).

    if (thread != tracked_thread_) {
        // Not the thread of interest
        return;
    }

    if (kernel_mode) {
        s2e_.getMessagesStream(state) << "Entering kernel mode." << '\n';
        // The current user process goes out of scope
        on_translate_block_start_.disconnect();
        on_translate_register_access_.disconnect();

        // TODO: Do this only when switching context between threads.
        return;
    }

    s2e_.getMessagesStream(state) << "Entering user mode." << '\n';

    on_translate_block_start_ = exec_stream_.onTranslateBlockStart.connect(
            sigc::mem_fun(*this, &CallTracer::onTranslateBlockStart));
    on_translate_register_access_ = exec_stream_.onTranslateRegisterAccessEnd.connect(
            sigc::mem_fun(*this, &CallTracer::onTranslateRegisterAccess));

    // TODO: Ideally, this should happen whenever onTranslate* callbacks are
    // modified.
    s2e_tb_safe_flush();

}

void CallTracer::onStackPointerModification(S2EExecutionState *state, uint64_t pc,
        bool isCall) {
    uint64_t sp = state->getSp();
    s2e_.getMessagesStream(state)
            << llvm::format("ESP=0x%x@pc=0x%x EIP=0x%x Top=0x%x/Size=%d",
                    sp, pc, state->getPc(), tracked_thread_->stack_top(), call_stack_.size())
            << '\n';
    call_stack_.update(state, pc, sp, isCall);
}

} /* namespace s2e */
