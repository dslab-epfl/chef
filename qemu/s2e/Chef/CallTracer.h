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
#include <boost/enable_shared_from_this.hpp>

#include <vector>

namespace s2e {

class S2E;
class OSThread;
class OSTracer;

struct CallStackFrame;

class CallStack {
public:
    CallStack(uint64_t top, uint64_t sp);

    unsigned size() {
        return frames_.size();
    }

    const CallStackFrame& frame(unsigned index) {
        assert(index < frames_.size());
        return frames_[index];
    }

    const CallStackFrame& top() {
        return frames_.back();
    }

    sigc::signal<void, CallStack*> onStackFramePush;
    sigc::signal<void, CallStack*> onStackFramePop;
    sigc::signal<void, CallStack*> onStackFrameResize;

protected:
    void newFrame(uint64_t call_site, uint64_t function, uint64_t sp);
    void update(uint64_t sp);

private:
    uint64_t top_;
    std::vector<CallStackFrame> frames_;

    // Non-copyable
    CallStack(const CallStack&);
    void operator=(const CallStack&);

    friend class CallTracer;
};


struct CallStackFrame {
    uint64_t call_site;
    uint64_t function;

    uint64_t top;
    uint64_t bottom;

    CallStackFrame()
        : call_site(0),
          function(0),
          top(0),
          bottom(0) {

    }

    CallStackFrame(uint64_t cs, uint64_t fn, uint64_t t, uint64_t b)
        : call_site(cs),
          function(fn),
          top(t),
          bottom(b) {

    }
};


class CallTracer {
public:
    CallTracer(S2E &s2e, ExecutionStream &estream, OSTracer &os_tracer,
            boost::shared_ptr<OSThread> thread);
    ~CallTracer();

    boost::shared_ptr<CallStack> call_stack() {
        return call_stack_;
    }

private:
    void onTranslateRegisterAccess(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
            uint64_t rmask, uint64_t wmask, bool accessesMemory);
    void onThreadSwitch(S2EExecutionState *state,
            boost::shared_ptr<OSThread> prev, boost::shared_ptr<OSThread> next);
    void onStackPointerModification(S2EExecutionState *state, uint64_t pc,
            bool isCall);

    S2E &s2e_;
    ExecutionStream &exec_stream_;
    boost::shared_ptr<OSThread> tracked_thread_;

    boost::shared_ptr<CallStack> call_stack_;

    sigc::connection on_thread_switch_;
    sigc::connection on_translate_register_access_;

    CallTracer(const CallTracer&);
    void operator=(const CallTracer&);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLTRACER_H_ */
