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
#include <s2e/Chef/InterpreterSemantics.h>

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
class S2EExecutionState;
class S2E;


class InterpreterDetector {
public:
    InterpreterDetector(CallTracer &call_tracer,
            boost::shared_ptr<S2ESyscallMonitor> syscall_monitor);
    ~InterpreterDetector();

    CallTracer &call_tracer() {
        return call_tracer_;
    }

    const InterpreterStructureParams *detected_params() const {
        return detected_params_.get();
    }

    sigc::signal<void,
                 S2EExecutionState*,
                 int,
                 const InterpreterStructureParams*>
            onInterpreterStructureDetected;

private:
    struct MemoryOpRecorder;

    void onConcreteDataMemoryAccess(S2EExecutionState *state, uint64_t address,
            uint64_t value, uint8_t size, unsigned flags);

    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
            uint64_t data, uint64_t size);
    void startCalibration(S2EExecutionState *state);
    void checkpointCalibration(S2EExecutionState *state, unsigned count);
    void endCalibration(S2EExecutionState *state);


    // Dependencies
    OSTracer &os_tracer_;
    CallTracer &call_tracer_;
    S2E &s2e_;
    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    // Calibration state
    bool calibrating_;
    unsigned min_opcode_count_;
    std::pair<uint64_t, uint64_t> memop_range_;
    boost::scoped_ptr<MemoryOpRecorder> memory_recording_;

    // Calibration results
    boost::scoped_ptr<InterpreterStructureParams> detected_params_;

    // Signals
    sigc::connection on_concrete_data_memory_access_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_ */
