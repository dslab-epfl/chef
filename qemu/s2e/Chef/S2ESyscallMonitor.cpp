/*
 * S2ESystemCallMonitor.cpp
 *
 *  Created on: Feb 12, 2015
 *      Author: stefan
 */

#include "S2ESyscallMonitor.h"

#include <s2e/Chef/ExecutionStream.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/S2E.h>
#include <s2e/Plugins/Opcodes.h>

#include <algorithm>
#include <vector>

using boost::shared_ptr;

namespace s2e {

// S2ESyscallRange /////////////////////////////////////////////////////////////

S2ESyscallRange::S2ESyscallRange(shared_ptr<S2ESyscallMonitor> monitor,
        uint64_t lower, uint64_t upper)
    : monitor_(monitor),
      lower_bound_(lower),
      upper_bound_(upper),
      registered_(true) {

}


void S2ESyscallRange::deregister() {
    assert(registered_);

    monitor_.lock()->deregister(shared_from_this());
    registered_ = false;
}


// S2ESyscallMonitor ///////////////////////////////////////////////////////////

S2ESyscallMonitor::S2ESyscallMonitor(S2E &s2e, ExecutionStream &stream)
        : s2e_(s2e),
          stream_(stream) {
    on_custom_instruction_ = stream.onCustomInstruction.connect(
            sigc::mem_fun(*this, &S2ESyscallMonitor::onCustomInstruction));
}

S2ESyscallMonitor::~S2ESyscallMonitor() {
    on_custom_instruction_.disconnect();
}

shared_ptr<S2ESyscallRange> S2ESyscallMonitor::registerForRange(
        uint64_t lower, uint64_t upper) {
    assert(lower < upper);

    shared_ptr<S2ESyscallRange> range = shared_ptr<S2ESyscallRange>(
            new S2ESyscallRange(shared_from_this(), lower, upper));

    range_set_.push_back(range);

    return range;
}

void S2ESyscallMonitor::deregister(shared_ptr<S2ESyscallRange> range) {
    RangeSet::iterator it = std::find(range_set_.begin(), range_set_.end(),
            range);

    assert(it != range_set_.end());

    range_set_.erase(it);
}

void S2ESyscallMonitor::onCustomInstruction(S2EExecutionState *state,
        uint64_t arg) {
    if (!OPCODE_CHECK(arg, SYSCALL_OPCODE))
        return;

    target_uint syscall_id = 0;
    target_ulong data = 0;
    target_uint size = 0;

    bool success = true;

    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EAX]),
            &syscall_id, sizeof(syscall_id));
    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_ECX]),
            &data, sizeof(data));
    success &= state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EDX]),
            &size, sizeof(size));

    if (!success) {
        s2e_.getWarningsStream(state) << "Could not read syscall data.  Ignoring." << '\n';
        return;
    }

    // Find potential targets based on ID
    // FIXME: Could be optimized for performance...
    for (RangeSet::iterator it = range_set_.begin(), ie = range_set_.end();
            it != ie; ++it) {
        if (syscall_id >= (*it)->lower_bound() &&
                syscall_id < (*it)->upper_bound()) {
            (*it)->onS2ESystemCall.emit(state, syscall_id, data, size);
        }
    }
}

} /* namespace s2e */
