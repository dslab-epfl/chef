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
#include <s2e/Chef/HighLevelStrategy.h>
#include <s2e/Chef/CallTracer.h>

#include <s2e/Chef/LowLevelTopoStrategy.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {


// HighLevelPathTracer /////////////////////////////////////////////////////////

HighLevelPathTracer::HighLevelPathTracer()
    : path_id_counter_(0) {

}

SharedHLPSRef HighLevelPathTracer::createRootSegment() {
    return make_shared<HighLevelPathSegment>(path_id_counter_++);
}

SharedHLPSRef HighLevelPathTracer::getNextSegment(SharedHLPSRef segment, uint64_t next_hlpc) {
    HighLevelPathSegment::ChildrenMap::iterator it = segment->children.find(next_hlpc);
    if (it != segment->children.end()) {
        return it->second;
    }

    SharedHLPSRef child = make_shared<HighLevelPathSegment>(next_hlpc, segment,
            segment->children.empty() ? segment->path_id : path_id_counter_++);
    segment->children.insert(std::make_pair(next_hlpc, child));
    return child;
}


// HighLevelPathSegment ////////////////////////////////////////////////////////

HighLevelPathSegment::HighLevelPathSegment(int path_id_)
    : hlpc(0),
      path_id(path_id_) {

}


HighLevelPathSegment::HighLevelPathSegment(uint64_t hlpc_, SharedHLPSRef parent_,
        int path_id_)
    : hlpc(hlpc_),
      path_id(path_id_),
      parent(parent_) {

}


HighLevelPathSegment::~HighLevelPathSegment() {

}

// HighLevelState //////////////////////////////////////////////////////////////

HighLevelState::HighLevelState(HighLevelExecutor &analyzer,
        shared_ptr<HighLevelPathSegment> segment_)
    : segment(segment_),
      analyzer_(analyzer) {
}


HighLevelState::~HighLevelState() {

}


void HighLevelState::step(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = analyzer_.path_tracer_.getNextSegment(segment, hlpc);

    next_segment->high_level_state = shared_from_this();
    segment->high_level_state.reset();

    segment = next_segment;
    segment->parent.reset();
}


shared_ptr<HighLevelState> HighLevelState::fork(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = analyzer_.path_tracer_.getNextSegment(segment, hlpc);
    assert(next_segment->path_id != segment->path_id);

    shared_ptr<HighLevelState> clone = make_shared<HighLevelState>(boost::ref(analyzer_), next_segment);
    clone->cursor = cursor;
    next_segment->high_level_state = clone;

    next_segment->parent.reset();
    return clone;
}


void HighLevelState::terminate() {
    segment->high_level_state.reset();
}

// TopologicNode ///////////////////////////////////////////////////////////////

TopologicNode::TopologicNode(shared_ptr<TopologicNode> p, int bb, int ci, bool cb)
    : parent(p),
      basic_block(bb),
      call_index(ci),
      is_call_base(cb) {

}

TopologicNode::TopologicNode()
    : basic_block(-1),
      call_index(0),
      is_call_base(true) {

}

shared_ptr<TopologicNode> TopologicNode::getDown(bool cb) {
    if (!down.expired())
        return down.lock();

    shared_ptr<TopologicNode> node = make_shared<TopologicNode>(
            shared_from_this(), -1, 0, cb);
    down = node;
    return node;
}

shared_ptr<TopologicNode> TopologicNode::getNext(int bb, int ci) {
    assert(bb > basic_block || (bb == basic_block && ci > call_index));
    shared_ptr<TopologicNode> previous = shared_from_this();
    shared_ptr<TopologicNode> current = previous->next.lock();

    while (current) {
        if (bb == current->basic_block && ci == current->call_index)
            return current;
        if (bb < current->basic_block || (bb == current->basic_block && ci < current->call_index)) {
            shared_ptr<TopologicNode> node = make_shared<TopologicNode>(
                    previous, bb, ci, is_call_base);
            previous->next = node;

            current->parent = node;
            node->next = current;

            return node;
        }
        previous = current;
        current = current->next.lock();
    }

    shared_ptr<TopologicNode> node = make_shared<TopologicNode>(previous, bb, ci, is_call_base);
    previous->next = node;
    return node;
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

    if (!topo_index.empty()) {
        new_state->topo_index = topo_index;
        new_state->topo_index.back()->states.insert(new_state);
    }
    return new_state;
}


void LowLevelState::terminate() {
    segment->low_level_states.erase(shared_from_this());

    if (!topo_index.empty()) {
        topo_index.back()->states.remove(shared_from_this());
    }

    analyzer().tryUpdateSelectedState();
}


void LowLevelState::step(uint64_t hlpc) {
    shared_ptr<HighLevelPathSegment> next_segment = analyzer().path_tracer_.getNextSegment(segment, hlpc);

    next_segment->low_level_states.insert(shared_from_this());
    segment->low_level_states.erase(shared_from_this());

    analyzer().tryUpdateSelectedState();

    segment = next_segment;
}

// HighLevelExecutor ///////////////////////////////////////////////////////////

HighLevelExecutor::HighLevelExecutor(InterpreterDetector &detector,
        HighLevelStrategy &strategy)
    : StreamAnalyzer<LowLevelState>(detector.s2e(), detector.stream()),
      detector_(detector),
      hl_strategy_(strategy) {
    on_high_level_pc_update_ = detector_.onHighLevelPCUpdate.connect(
            sigc::mem_fun(*this, &HighLevelExecutor::onHighLevelPCUpdate));

    ll_strategy_.reset(new LowLevelTopoStrategy(*this));

    s2e().getMessagesStream() << "Constructed high-level executor for tid="
            << detector_.call_tracer().tracked_tid() << '\n';
}


HighLevelExecutor::~HighLevelExecutor() {
    s2e().getMessagesStream() << "High-level executor terminated for tid="
            << detector_.call_tracer().tracked_tid() << '\n';
    on_high_level_pc_update_.disconnect();
}


shared_ptr<LowLevelState> HighLevelExecutor::createState(S2EExecutionState *s2e_state) {
    // For each new state created in the system, create a new high-level path.
    shared_ptr<HighLevelPathSegment> segment = path_tracer_.createRootSegment();

    // Create a high-level state at the base of the path.
    shared_ptr<HighLevelState> hl_state = make_shared<HighLevelState>(boost::ref(*this), segment);
    hl_state->segment->high_level_state = hl_state;

    // Keep track of the new high-level state.
    high_level_states_.insert(hl_state);

    // Create a low-level state as the support for the HL path.
    shared_ptr<LowLevelState> ll_state = shared_ptr<LowLevelState>(
            new LowLevelState(*this, s2e_state));
    ll_state->segment = segment;
    ll_state->segment->low_level_states.insert(ll_state);

    // Bootstrap the topologic index computation
    ll_state->topo_index = ll_strategy_->cursor_;
    ll_state->topo_index.back()->states.insert(ll_state);

    onHighLevelStateCreate.emit(hl_state.get());

    // Add the state to the strategy
    std::vector<shared_ptr<HighLevelState> > statev;
    statev.push_back(hl_state);
    hl_strategy_.addStates(shared_ptr<HighLevelState>(), statev);

    // Invoke again the strategy
    selected_state_ = hl_strategy_.selectState();

    return ll_state;
}


void HighLevelExecutor::onHighLevelPCUpdate(S2EExecutionState *s2e_state,
        HighLevelStack *hl_stack) {
    shared_ptr<LowLevelState> state = getState(s2e_state);
    uint64_t hlpc = hl_stack->top()->hlpc;
    state->step(hlpc);
}


void HighLevelExecutor::tryUpdateSelectedState() {
    for (;;) {
        if (!selected_state_)
            break;
        if (!doUpdateSelectedState())
            break;
    }
}


bool HighLevelExecutor::doUpdateSelectedState() {
    shared_ptr<HighLevelPathSegment> segment = selected_state_->segment;
    if (!segment->low_level_states.empty())
        return false;

    assert(segment->parent.expired());

    // TODO: Might be nice to have a "wildcard" type of check, to simulate
    // strategy relinquish (e.g., update the selected_state_ when a low-level
    // state makes some progress.

    if (segment->children.empty()) {
        // High-level state terminated
        onHighLevelStateKill.emit(selected_state_.get());
        hl_strategy_.killState(selected_state_);

        selected_state_->terminate();
        high_level_states_.erase(selected_state_);
    } else if (segment->children.size() > 1) {
        typedef HighLevelPathSegment::ChildrenMap::iterator iterator;

        std::vector<HighLevelState*> fork_list;
        fork_list.push_back(selected_state_.get());

        std::vector<shared_ptr<HighLevelState> > add_list;

        for (iterator it = segment->children.begin(),
                ie = segment->children.end(); it != ie; ++it) {
            if (it->second->path_id == segment->path_id) {
                selected_state_->step(it->first);
            } else {
                shared_ptr<HighLevelState> hl_fork = selected_state_->fork(it->first);
                high_level_states_.insert(hl_fork);
                fork_list.push_back(hl_fork.get());
                add_list.push_back(hl_fork);
            }
        }

        onHighLevelStateFork.emit(selected_state_.get(), fork_list);
        hl_strategy_.addStates(selected_state_, add_list);
    } else {
        // Plain step
        selected_state_->step(segment->children.begin()->first);
        onHighLevelStateStep.emit(selected_state_.get());
        hl_strategy_.updateState(selected_state_);
    }

    // Query the strategy for another state
    selected_state_ = hl_strategy_.selectState();
    return true;
}


} /* namespace s2e */
