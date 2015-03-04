/*
 * CallTracer.h
 *
 *  Created on: Feb 7, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_CALLTRACER_H_
#define QEMU_S2E_CHEF_CALLTRACER_H_

#include <s2e/Signals/Signals.h>
#include <s2e/Chef/ExecutionStream.h>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <llvm/Support/raw_ostream.h>

#include <vector>

namespace s2e {

class S2E;
class OSThread;
class OSTracer;

struct CallStackFrame {
    boost::shared_ptr<CallStackFrame> parent;

    uint64_t call_site;
    uint64_t function;

    // TODO: Maybe this should be stored separately?
    uint32_t bb_index;

    uint64_t top;
    uint64_t bottom;

    CallStackFrame()
        : call_site(0),
          function(0),
          bb_index(0),
          top(0),
          bottom(0) {

    }

    CallStackFrame(boost::shared_ptr<CallStackFrame> p,
            uint64_t cs, uint64_t fn, uint64_t t, uint64_t b)
        : parent(p),
          call_site(cs),
          function(fn),
          bb_index(0),
          top(t),
          bottom(b) {

    }

private:
    CallStackFrame(const CallStackFrame&);
    void operator=(const CallStackFrame&);
};


class CallStack {
public:
    CallStack(uint64_t top, uint64_t sp);

    unsigned size() const {
        return frames_.size();
    }

    boost::shared_ptr<CallStackFrame> frame(unsigned index) const {
        assert(index < frames_.size());
        return frames_[frames_.size() - index - 1];
    }

    boost::shared_ptr<CallStackFrame> top() const {
        return frames_.back();
    }

    sigc::signal<void,
                 S2EExecutionState*,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame>,
                 boost::shared_ptr<CallStackFrame> >
            onStackFramePush;

    sigc::signal<void,
                 S2EExecutionState*,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame>,
                 boost::shared_ptr<CallStackFrame> >
            onStackFramePopping;

    sigc::signal<void,
                 S2EExecutionState*,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame> >
            onStackFrameResize;

    sigc::signal<void,
                 S2EExecutionState*,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame> >
            onBasicBlockEnter;

protected:
    void newFrame(S2EExecutionState *state, uint64_t call_site,
            uint64_t function, uint64_t sp);
    void updateFrame(S2EExecutionState *state, uint64_t sp);
    void updateBasicBlock(S2EExecutionState *state, uint32_t bb_index);

private:
    uint64_t top_;
    std::vector<boost::shared_ptr<CallStackFrame> > frames_;

    // Non-copyable
    CallStack(const CallStack&);
    void operator=(const CallStack&);

    friend class CallTracer;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CallStack &cs);


class CallTracer {
public:
    CallTracer(S2E &s2e, OSTracer &os_tracer,
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
    void onCustomInstruction(S2EExecutionState *state, uint64_t opcode);

    S2E &s2e_;
    ExecutionStream &exec_stream_;
    boost::shared_ptr<OSThread> tracked_thread_;

    boost::shared_ptr<CallStack> call_stack_;

    sigc::connection on_thread_switch_;
    sigc::connection on_translate_register_access_;
    sigc::connection on_custom_instruction_;

    CallTracer(const CallTracer&);
    void operator=(const CallTracer&);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLTRACER_H_ */
