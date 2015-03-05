/*
 * CallTracer.h
 *
 *  Created on: Feb 7, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_CALLTRACER_H_
#define QEMU_S2E_CHEF_CALLTRACER_H_

#include <s2e/Signals/Signals.h>
#include <s2e/Chef/StreamAnalyzer.h>

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

    uint64_t id;

    uint64_t call_site;
    uint64_t function;

    // TODO: Maybe this should be stored separately?
    uint32_t bb_index;

    uint64_t top;
    uint64_t bottom;

    CallStackFrame(boost::shared_ptr<CallStackFrame> p,
            uint64_t i, uint64_t cs, uint64_t fn, uint64_t t, uint64_t b)
        : parent(p), id(i), call_site(cs), function(fn), bb_index(0),
          top(t), bottom(b) {

    }

    CallStackFrame(const CallStackFrame &other)
        : parent(other.parent),
          id(other.id),
          call_site(other.call_site),
          function(other.function),
          bb_index(other.bb_index),
          top(other.top),
          bottom(other.bottom) {

    }

private:
    void operator=(const CallStackFrame&);

    friend class CallStack;
};


class CallTracer;


class CallStack : public StreamAnalyzerState<CallTracer> {
public:
    CallStack(CallTracer &tracer, S2EExecutionState *s2e_state);

    CallStack(const CallStack &other, S2EExecutionState *s2e_state);

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

protected:
    void newFrame(uint64_t call_site, uint64_t function, uint64_t sp);
    void updateFrame(uint64_t sp);
    void updateBasicBlock(uint32_t bb_index);

private:
    typedef std::vector<boost::shared_ptr<CallStackFrame> > FrameVector;
    uint64_t next_id_;
    FrameVector frames_;


    void operator=(const CallStack&);

    friend class CallTracer;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CallStack &cs);


class CallTracer : public StreamAnalyzer<CallStack, CallTracer> {
public:
    CallTracer(OSTracer &os_tracer, int tid);
    ~CallTracer();

    OSTracer &os_tracer() const {
        return os_tracer_;
    }

    int tracked_tid() const {
        return tracked_tid_;
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

private:
    void onTranslateRegisterAccess(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
            uint64_t rmask, uint64_t wmask, bool accessesMemory);
    void onThreadSwitch(S2EExecutionState *state, OSThread* prev, OSThread* next);
    void onStackPointerModification(S2EExecutionState *state, uint64_t pc,
            bool isCall);
    void onCustomInstruction(S2EExecutionState *state, uint64_t opcode);

    OSTracer &os_tracer_;
    int tracked_tid_;

    sigc::connection on_thread_switch_;
    sigc::connection on_translate_register_access_;
    sigc::connection on_custom_instruction_;

    CallTracer(const CallTracer&);
    void operator=(const CallTracer&);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLTRACER_H_ */
