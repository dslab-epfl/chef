/*
 * InterpreterDetector.h
 *
 *  Created on: Feb 17, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_
#define QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_

#include <s2e/Signals/Signals.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <stdint.h>

namespace s2e {


class S2EExecutionState;
class S2E;
class OSTracer;
class OSThread;
class ExecutionStream;
class S2ESyscallMonitor;
class S2ESyscallRange;

class InterpreterDetector {
public:
    InterpreterDetector(S2E &s2e, OSTracer &os_tracer,
            boost::shared_ptr<OSThread> thread,
            boost::shared_ptr<S2ESyscallMonitor> syscall_monitor);
    ~InterpreterDetector();

private:
    struct MemoryOpRecorder;

    void onConcreteDataMemoryAccess(S2EExecutionState *state, uint64_t address,
            uint64_t value, uint8_t size, unsigned flags);
    void onS2ESyscall(S2EExecutionState *state, uint64_t syscall_id,
            uint64_t data, uint64_t size);

    void computeCalibration(S2EExecutionState *state);

    S2E &s2e_;
    OSTracer &os_tracer_;
    boost::shared_ptr<OSThread> thread_;
    boost::shared_ptr<S2ESyscallRange> syscall_range_;

    // Calibration state
    bool calibrating_;
    unsigned min_opcode_count_;
    sigc::connection on_concrete_data_memory_access_;
    boost::scoped_ptr<MemoryOpRecorder> memory_recording_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_INTERPRETERDETECTOR_H_ */
