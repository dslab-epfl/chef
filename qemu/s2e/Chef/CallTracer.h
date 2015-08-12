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

    // TODO: Maybe this should be stored separately, in a basic block descriptor?
    int bb_index;
    int loop_id;
    int loop_depth;
    bool is_header;

    uint64_t top;
    uint64_t bottom;

    CallStackFrame(boost::shared_ptr<CallStackFrame> p,
            uint64_t i, uint64_t cs, uint64_t fn, uint64_t t, uint64_t b)
        : parent(p), id(i), call_site(cs), function(fn),
          bb_index(0), loop_id(0), loop_depth(0), is_header(false),
          top(t), bottom(b) {

    }

    CallStackFrame(const CallStackFrame &other)
        : parent(other.parent),
          id(other.id),
          call_site(other.call_site),
          function(other.function),
          bb_index(other.bb_index),
          loop_id(other.loop_id),
          loop_depth(other.loop_depth),
          is_header(other.is_header),
          top(other.top),
          bottom(other.bottom) {

    }

private:
    void operator=(const CallStackFrame&);

    friend class CallStack;
};


class CallTracer;


class CallStack : public StreamAnalyzerState<CallStack, CallTracer> {
public:
    CallStack(CallTracer &tracer, S2EExecutionState *s2e_state);

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

    StateRef clone(S2EExecutionState *s2e_state);

protected:
    void newFrame(uint64_t call_site, uint64_t function, uint64_t sp);
    void updateFrame(uint64_t sp);
    void updateBasicBlock(int bb_index, int loop_id, int loop_depth, bool is_header,
            bool &schedule_state);

private:
    typedef std::vector<boost::shared_ptr<CallStackFrame> > FrameVector;
    uint64_t next_id_;
    FrameVector frames_;


    void operator=(const CallStack&);

    friend class CallTracer;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CallStack &cs);


class CallTracer : public StreamAnalyzer<CallStack> {
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
                 CallStack*,
                 boost::shared_ptr<CallStackFrame>,
                 boost::shared_ptr<CallStackFrame> >
            onStackFramePush;

    sigc::signal<void,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame>,
                 boost::shared_ptr<CallStackFrame> >
            onStackFramePopping;

    sigc::signal<void,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame> >
            onStackFrameResize;

    sigc::signal<void,
                 CallStack*,
                 boost::shared_ptr<CallStackFrame>,
                 bool&> // Force state scheduler
            onBasicBlockEnter;

protected:
    StateRef createState(S2EExecutionState *s2e_state);

private:
    void onTranslateRegisterAccess(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc,
            uint64_t rmask, uint64_t wmask, bool accessesMemory);
    void onThreadSwitch(S2EExecutionState *state, OSThread* prev, OSThread* next);
    void onStateSwitch(S2EExecutionState *prev, S2EExecutionState *next);
    void onStackPointerModification(S2EExecutionState *state, uint64_t pc,
            bool isCall);
    void onCustomInstruction(S2EExecutionState *state, uint64_t opcode);

    void updateConnections(S2EExecutionState *state, bool flush_tb);

    OSTracer &os_tracer_;
    int tracked_tid_;

    sigc::connection on_thread_switch_;
    sigc::connection on_state_switch_;
    sigc::connection on_translate_register_access_;
    sigc::connection on_custom_instruction_;

    CallTracer(const CallTracer&);
    void operator=(const CallTracer&);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLTRACER_H_ */
