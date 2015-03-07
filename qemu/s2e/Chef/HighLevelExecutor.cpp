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

#include "HighLevelExecutor.h"
#include <s2e/Chef/InterpreterDetector.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {

// HighLevelPathSegment ////////////////////////////////////////////////////////

HighLevelPathSegment::HighLevelPathSegment()
    : hlpc_(0) {

}


HighLevelPathSegment::HighLevelPathSegment(uint64_t hlpc, SharedHLPSRef parent)
    : parent_(parent),
      hlpc_(hlpc) {

}


HighLevelPathSegment::~HighLevelPathSegment() {

}


boost::shared_ptr<HighLevelPathSegment> HighLevelPathSegment::getNext(uint64_t hlpc) {
    if (children_[hlpc].expired()) {
        SharedHLPSRef child = make_shared<HighLevelPathSegment>(hlpc, shared_from_this());
        children_[hlpc] = child;
        return child;
    }
    return children_[hlpc].lock();
}

// HighLevelState //////////////////////////////////////////////////////////////

HighLevelState::HighLevelState() {

}

HighLevelState::~HighLevelState() {

}

// LowLevelState ///////////////////////////////////////////////////////////////

LowLevelState::LowLevelState(HighLevelExecutor &analyzer,
        S2EExecutionState *s2e_state)
    : StreamAnalyzerState<LowLevelState, HighLevelExecutor>(analyzer, s2e_state) {

}


shared_ptr<LowLevelState> LowLevelState::clone(S2EExecutionState *s2e_state) {
    shared_ptr<LowLevelState> new_state = shared_ptr<LowLevelState>(
            new LowLevelState(analyzer(), s2e_state));
    new_state->segment_ = segment_;
    new_state->segment_->low_level_states.insert(new_state);
    return new_state;
}


void LowLevelState::terminate() {
    segment_->low_level_states.erase(shared_from_this());
}


void LowLevelState::step(uint64_t hlpc) {
    segment_->low_level_states.erase(shared_from_this());
    segment_ = segment_->getNext(hlpc);
    segment_->low_level_states.insert(shared_from_this());
}

// HighLevelExecutor ///////////////////////////////////////////////////////////

HighLevelExecutor::HighLevelExecutor(InterpreterDetector &detector)
    : StreamAnalyzer<LowLevelState>(detector.s2e(), detector.stream()),
      detector_(detector) {
    root_segment_ = make_shared<HighLevelPathSegment>();
    on_high_level_pc_update_ = detector_.onHighLevelPCUpdate.connect(
            sigc::mem_fun(*this, &HighLevelExecutor::onHighLevelPCUpdate));

    s2e().getMessagesStream() << "Constructed high-level executor for tid="
            << detector_.tracked_tid() << '\n';
}


HighLevelExecutor::~HighLevelExecutor() {
    s2e().getMessagesStream() << "High-level executor terminated for tid="
            << detector_.tracked_tid() << '\n';
    on_high_level_pc_update_.disconnect();
}


shared_ptr<LowLevelState> HighLevelExecutor::createState(S2EExecutionState *s2e_state) {
    shared_ptr<LowLevelState> state = shared_ptr<LowLevelState>(
            new LowLevelState(*this, s2e_state));
    state->segment_ = root_segment_;
    state->segment_->low_level_states.insert(state);
    return state;
}


void HighLevelExecutor::onHighLevelPCUpdate(S2EExecutionState *s2e_state,
        HighLevelStack *hl_stack) {
    shared_ptr<LowLevelState> state = getState(s2e_state);
    uint64_t hlpc = hl_stack->top()->hlpc;
    state->step(hlpc);
}

} /* namespace s2e */
