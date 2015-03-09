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
    : hlpc(0) {

}


HighLevelPathSegment::HighLevelPathSegment(uint64_t hlpc_, SharedHLPSRef parent_)
    : hlpc(hlpc_),
      parent(parent_) {

}


HighLevelPathSegment::~HighLevelPathSegment() {

}


boost::shared_ptr<HighLevelPathSegment> HighLevelPathSegment::getNext(uint64_t hlpc) {
    ChildrenMap::iterator it = children.find(hlpc);
    if (it != children.end()) {
        return it->second;
    }

    SharedHLPSRef child = make_shared<HighLevelPathSegment>(hlpc, shared_from_this());
    children.insert(std::make_pair(hlpc, child));
    return child;
}

// HighLevelState //////////////////////////////////////////////////////////////

HighLevelState::HighLevelState(shared_ptr<HighLevelPathSegment> segment_)
    : segment(segment_) {
}


HighLevelState::~HighLevelState() {

}


void HighLevelState::step(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = segment->getNext(hlpc);

    next_segment->high_level_state = shared_from_this();
    segment->high_level_state.reset();

    segment = next_segment;
    segment->parent.reset();
}


shared_ptr<HighLevelState> HighLevelState::fork(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = segment->getNext(hlpc);

    shared_ptr<HighLevelState> clone = make_shared<HighLevelState>(next_segment);
    next_segment->high_level_state = clone;

    next_segment->parent.reset();
    return clone;
}


void HighLevelState::terminate() {
    segment->high_level_state.reset();
}

// LowLevelState ///////////////////////////////////////////////////////////////

LowLevelState::LowLevelState(HighLevelExecutor &analyzer,
        S2EExecutionState *s2e_state)
    : StreamAnalyzerState<LowLevelState, HighLevelExecutor>(analyzer, s2e_state) {

}


shared_ptr<LowLevelState> LowLevelState::clone(S2EExecutionState *s2e_state) {
    shared_ptr<LowLevelState> new_state = shared_ptr<LowLevelState>(
            new LowLevelState(analyzer(), s2e_state));
    new_state->segment = segment;
    new_state->segment->low_level_states.insert(new_state);
    return new_state;
}


void LowLevelState::terminate() {
    segment->low_level_states.erase(shared_from_this());
}


void LowLevelState::step(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = segment->getNext(hlpc);

    next_segment->low_level_states.insert(shared_from_this());
    segment->low_level_states.erase(shared_from_this());

    if (!segment->high_level_state.expired() && segment->low_level_states.empty()) {
        assert(segment->parent.expired());
        shared_ptr<HighLevelState> hl_state = segment->high_level_state.lock();

        if (segment->children.empty()) {
            // High-level state terminated
            analyzer().onHighLevelStateKill.emit(s2e_state(), hl_state.get());

            hl_state->terminate();
            analyzer().high_level_states.erase(hl_state);
            return;
        }
        if (segment->children.size() > 1) {
            typedef HighLevelPathSegment::ChildrenMap::iterator iterator;
            std::vector<HighLevelState*> fork_list;
            for (iterator it = segment->children.begin(),
                    ie = segment->children.end(); it != ie; ++it) {
                if (it->first != hlpc) {
                    shared_ptr<HighLevelState> hl_fork = hl_state->fork(it->first);
                    analyzer().high_level_states.insert(hl_fork);
                    fork_list.push_back(hl_fork.get());
                }
            }
            analyzer().onHighLevelStateFork.emit(s2e_state(), hl_state.get(),
                    fork_list);
            // Fall-through to stepping on the main state
        }

        // Plain step
        hl_state->step(hlpc);
        analyzer().onHighLevelStateStep.emit(s2e_state(), hl_state.get());
    }

    segment = next_segment;
}

// HighLevelExecutor ///////////////////////////////////////////////////////////

HighLevelExecutor::HighLevelExecutor(InterpreterDetector &detector)
    : StreamAnalyzer<LowLevelState>(detector.s2e(), detector.stream()),
      detector_(detector) {
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
    // For each new state created in the system, create a new high-level path.
    shared_ptr<HighLevelPathSegment> segment = make_shared<HighLevelPathSegment>();

    // Create a high-level state at the base of the path.
    shared_ptr<HighLevelState> hl_state = make_shared<HighLevelState>(segment);
    hl_state->segment->high_level_state = hl_state;

    // Keep track of the new high-level state.
    high_level_states.insert(hl_state);

    // Create a low-level state as the support for the HL path.
    shared_ptr<LowLevelState> ll_state = shared_ptr<LowLevelState>(
            new LowLevelState(*this, s2e_state));
    ll_state->segment = segment;
    ll_state->segment->low_level_states.insert(ll_state);

    onHighLevelStateCreate.emit(s2e_state, hl_state.get());

    return ll_state;
}


void HighLevelExecutor::onHighLevelPCUpdate(S2EExecutionState *s2e_state,
        HighLevelStack *hl_stack) {
    shared_ptr<LowLevelState> state = getState(s2e_state);
    uint64_t hlpc = hl_stack->top()->hlpc;
    state->step(hlpc);
}

} /* namespace s2e */
