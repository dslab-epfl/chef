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

#include <llvm/Support/Format.h>

namespace s2e {

CallTracer::CallTracer(S2E &s2e, ExecutionStream &estream, OSTracer &os_tracer,
        boost::shared_ptr<OSThread> thread)
    : s2e_(s2e),
      exec_stream_(estream),
      tracked_thread_(thread) {

    os_tracer.onThreadPrivilegeChange.connect(
            sigc::mem_fun(*this, &CallTracer::onThreadPrivilegeChange));
}

CallTracer::~CallTracer() {

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
        // The current user process goes out of scope
        on_translate_block_start_.disconnect();
        on_translate_register_access_.disconnect();
        return;
    }

    on_translate_block_start_ = exec_stream_.onTranslateBlockStart.connect(
            sigc::mem_fun(*this, &CallTracer::onTranslateBlockStart));
    on_translate_register_access_ = exec_stream_.onTranslateRegisterAccessEnd.connect(
            sigc::mem_fun(*this, &CallTracer::onTranslateRegisterAccess));

}

void CallTracer::onStackPointerModification(S2EExecutionState *state, uint64_t pc,
        bool isCall) {
    s2e_.getMessagesStream(state) << "ESP modified at " << llvm::format("0x%x", pc) << '\n';
}

} /* namespace s2e */
