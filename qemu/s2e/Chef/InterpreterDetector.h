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

#ifndef QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_
#define QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_

#include <s2e/Signals/Signals.h>
#include <s2e/Chef/StreamAnalyzer.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <stdint.h>

namespace s2e {


class OSTracer;
class OSThread;

class CallTracer;
class CallStack;
struct CallStackFrame;

class ExecutionStream;
class S2ESyscallMonitor;
class S2ESyscallRange;

struct HighLevelFrame {
    boost::shared_ptr<HighLevelFrame> parent;
    uint64_t low_level_frame_id;

    // The frame-specific address of the HLPC pointer
    uint64_t hlpc_ptr;
    // Latest HLPC value
    uint64_t hlpc;
    // Latest HLPC value of an opcode fetch
    uint64_t hlinst;

    HighLevelFrame(boost::shared_ptr<HighLevelFrame> p, uint64_t l)
        : parent(p),
          low_level_frame_id(l),
          hlpc_ptr(0),
          hlpc(0),
          hlinst(0) {

    }

    HighLevelFrame(uint64_t l)
        : low_level_frame_id(l),
          hlpc_ptr(0),
          hlpc(0),
          hlinst(0) {

    }

private:
    HighLevelFrame(const HighLevelFrame&);
    void operator=(const HighLevelFrame&);
};


class InterpreterDetector;


class HighLevelStack : public StreamAnalyzerState<HighLevelStack, InterpreterDetector> {
public:
    HighLevelStack(InterpreterDetector &detector, S2EExecutionState *s2e_state);

    unsigned size() const {
        return frames_.size();
    }

    boost::shared_ptr<HighLevelFrame> frame(unsigned index) const {
        assert(index < frames_.size());
        return frames_[index];
    }

    boost::shared_ptr<HighLevelFrame> top() const {
        return frames_.back();
    }

    StateRef clone(S2EExecutionState *s2e_state);
private:
    std::vector<boost::shared_ptr<HighLevelFrame> > frames_;

    friend class InterpreterDetector;
};


class InterpreterDetector : public StreamAnalyzer<HighLevelStack> {
public:
    InterpreterDetector(OSTracer &os_tracer, int tid,
            boost::shared_ptr<S2ESyscallMonitor> syscall_monitor);
    ~InterpreterDetector();

    sigc::signal<void,
                 S2EExecutionState*,
                 HighLevelStack*>
            onHighLevelFramePush;

    sigc::signal<void,
                 S2EExecutionState*,
                 HighLevelStack*>
            onHighLevelFramePopping;

    sigc::signal<void,
                 S2EExecutionState*,
                 HighLevelStack*>
            onHighLevelInstructionFetch;

    sigc::signal<void,
                 S2EExecutionState*,
                 HighLevelStack*>
            onHighLevelPCUpdate;

protected:
    StateRef createState(S2EExecutionState *s2e_state);

private:
    struct MemoryOpRecorder;

    void onConcreteDataMemoryAccess(S2EExecutionState *state, uint64_t address,
            uint64_t value, uint8_t size, unsigned flags);

    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
            uint64_t data, uint64_t size);
    void startCalibration(S2EExecutionState *state);
    void checkpointCalibration(S2EExecutionState *state, unsigned count);
    void endCalibration(S2EExecutionState *state);
    void startMonitoring(S2EExecutionState *state);

    void onLowLevelStackFramePush(S2EExecutionState *state,
            CallStack *call_stack, boost::shared_ptr<CallStackFrame> old_top,
            boost::shared_ptr<CallStackFrame> new_top);
    void onLowLevelStackFramePopping(S2EExecutionState *state,
            CallStack *call_stack, boost::shared_ptr<CallStackFrame> old_top,
            boost::shared_ptr<CallStackFrame> new_top);

    // Dependencies
    OSTracer &os_tracer_;
    boost::scoped_ptr<CallTracer> call_tracer_;
    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    // Tracked thread
    int tracked_tid_;

    // Calibration state
    bool calibrating_;
    unsigned min_opcode_count_;
    std::pair<uint64_t, uint64_t> memop_range_;
    boost::scoped_ptr<MemoryOpRecorder> memory_recording_;

    // Calibration results
    uint64_t instrum_function_;
    uint64_t instrum_hlpc_update_;
    uint64_t instrum_opcode_read_;

    // Signals
    sigc::connection on_stack_frame_push_;
    sigc::connection on_stack_frame_popping_;
    sigc::connection on_concrete_data_memory_access_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_ */
