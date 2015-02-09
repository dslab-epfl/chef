/*
 * CallTracer.h
 *
 *  Created on: Feb 7, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_CALLTRACER_H_
#define QEMU_S2E_CHEF_CALLTRACER_H_

#include <s2e/Signals/Signals.h>
#include <s2e/ExecutionStream.h>

#include <boost/shared_ptr.hpp>

#include <vector>

namespace s2e {

class S2E;
class OSThread;
class OSTracer;

struct StackFrame {
    uint64_t call_site;
    uint64_t top;
    uint64_t bottom;
};

class CallStack {
public:
    CallStack(uint64_t top, uint64_t sp);

    void update(S2EExecutionState *state, uint64_t pc, uint64_t sp,
            bool is_call);

    unsigned size() {
        return frames_.size();
    }

private:
    uint64_t top_;
    std::vector<StackFrame> frames_;

    // Non-copyable
    CallStack(const CallStack&);
    void operator=(const CallStack&);
};


class CallTracer {
public:
    CallTracer(S2E &s2e, ExecutionStream &estream, OSTracer &os_tracer,
            boost::shared_ptr<OSThread> thread);
    ~CallTracer();

private:
    void onTranslateBlockStart(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc);
    void onTranslateRegisterAccess(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
            uint64_t rmask, uint64_t wmask, bool accessesMemory);

    void onThreadPrivilegeChange(S2EExecutionState *state,
            boost::shared_ptr<OSThread> thread, bool kernel_mode);

    void onStackPointerModification(S2EExecutionState *state, uint64_t pc,
            bool isCall);

    S2E &s2e_;
    ExecutionStream &exec_stream_;
    boost::shared_ptr<OSThread> tracked_thread_;

    CallStack call_stack_;

    sigc::connection on_thread_privilege_change_;
    sigc::connection on_translate_block_start_;
    sigc::connection on_translate_register_access_;

    CallTracer(const CallTracer&);
    void operator=(const CallTracer&);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLTRACER_H_ */
