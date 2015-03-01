/*
 * InterpreterDetector.h
 *
 *  Created on: Feb 17, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_
#define QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_

#include <s2e/Signals/Signals.h>
#include <s2e/ExecutionStream.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <stdint.h>

namespace s2e {


class S2EExecutionState;
class S2E;
class OSTracer;
class OSThread;
class CallTracer;
class ExecutionStream;
class S2ESyscallMonitor;
class S2ESyscallRange;

class InterpreterDetector {
public:
    InterpreterDetector(S2E &s2e, OSTracer &os_tracer,
            boost::shared_ptr<OSThread> thread,
            boost::shared_ptr<S2ESyscallMonitor> syscall_monitor);
    ~InterpreterDetector();

    uint64_t instrumentation_pc() const {
        return instrumentation_pc_;
    }

    sigc::signal<void,
            S2EExecutionState *,
            uint64_t   /* high-level PC */>
            onHighLevelInstructionStart;

private:
    struct MemoryOpRecorder;

    void onConcreteDataMemoryAccess(S2EExecutionState *state, uint64_t address,
            uint64_t value, uint8_t size, unsigned flags);
    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
            uint64_t data, uint64_t size);

    void onTranslateInstructionStart(ExecutionSignal *signal,
            S2EExecutionState *state, TranslationBlock *tb, uint64_t pc);
    void onInstrumentationHit(S2EExecutionState *state, uint64_t pc);

    void startCalibration(S2EExecutionState *state);
    void endCalibration(S2EExecutionState *state);
    void computeInstrumentation(S2EExecutionState *state);

    void resetInstrumentationPc(uint64_t value);

    S2E &s2e_;
    OSTracer &os_tracer_;
    boost::scoped_ptr<CallTracer> call_tracer_;
    boost::shared_ptr<OSThread> thread_;
    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    // Calibration state
    bool calibrating_;
    unsigned min_opcode_count_;
    std::pair<uint64_t, uint64_t> memop_range_;
    boost::scoped_ptr<MemoryOpRecorder> memory_recording_;
    uint64_t instrumentation_pc_;

    // Signals
    sigc::connection on_concrete_data_memory_access_;
    sigc::connection on_translate_instruction_start_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_ */
