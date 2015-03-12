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

#include "LowLevelTopoStrategy.h"

// TODO: This is a bit messy; encapsulate somewhere else.
extern "C" {
#include "config.h"
#include "qemu-common.h"
#include "cpu.h"
extern CPUX86State *env;
}

#include <s2e/S2EExecutor.h>

#include <s2e/Chef/CallTracer.h>
#include <s2e/Chef/HighLevelExecutor.h>
#include <s2e/Chef/InterpreterDetector.h>

#include <llvm/Support/CommandLine.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace {

llvm::cl::opt<bool>
DebugLowLevelScheduler("debug-low-level-scheduler",
        llvm::cl::desc("Print debug info for the low-level Chef scheduler"),
        llvm::cl::init(false));

}

namespace s2e {

// TODO: This can be encapsulated in an iterator, together with the entire
// topologic space.

static bool stepCursor(TopologicIndex &cursor) {
    if (cursor.empty())
        return false;
    if (!cursor.back()->down.expired()) {
        cursor.push_back(cursor.back()->down.lock());
    } else if (!cursor.back()->next.expired()) {
        cursor.back() = cursor.back()->next.lock();
    } else {
        while(!cursor.empty() && cursor.back()->next.expired()) {
            cursor.pop_back();
        }
        if (!cursor.empty()) {
            assert(!cursor.back()->next.expired());
            cursor.back() = cursor.back()->next.lock();
        }
    }
    return true;
}

static shared_ptr<LowLevelState> findNextState(int path_id, TopologicIndex &cursor,
        long int &counter) {
    counter = 0;
    while (!cursor.empty()) {
        if (!cursor.back()->states.empty()) {
            for (TopologicNode::StateSet::iterator it = cursor.back()->states.begin(),
                    ie = cursor.back()->states.end(); it != ie; ++it) {
                if ((*it)->segment->path_id == path_id) {
                    return *it;
                }
            }
        }
        stepCursor(cursor);
        counter++;
    }
    return shared_ptr<LowLevelState>();
}

static int countAccessibleStates(const TopologicIndex &cursor) {
    TopologicIndex moving_cursor = cursor;
    int count = 0;
    while (!moving_cursor.empty()) {
        count += moving_cursor.back()->states.size();
        stepCursor(moving_cursor);
    }
    return count;
}

// LowLevelTopoStrategy ////////////////////////////////////////////////////////

LowLevelTopoStrategy::LowLevelTopoStrategy(HighLevelExecutor &hl_executor)
    : hl_executor_(hl_executor),
      call_tracer_(hl_executor.detector().call_tracer()){

    on_stack_frame_push_ = call_tracer_.onStackFramePush.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onStackFramePush));
    on_stack_frame_popping_ = call_tracer_.onStackFramePopping.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onStackFramePopping));
    on_basic_block_enter_ = call_tracer_.onBasicBlockEnter.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onBasicBlockEnter));

    old_searcher_ = hl_executor_.s2e().getExecutor()->getSearcher();
    hl_executor_.s2e().getExecutor()->setSearcher(this);
}


void LowLevelTopoStrategy::updateTargetHighLevelState(
        boost::shared_ptr<HighLevelState> hl_state) {
    assert(hl_state);

    if (target_hl_state_ != hl_state) {
        if (target_hl_state_) {
            if (DebugLowLevelScheduler) {
                hl_executor_.s2e().getMessagesStream()
                        << "Saving old cursor at " << active_cursor_ << '\n';
            }

            // The state might have already terminated, but it's fine, since
            // the shared pointer pins the structure in memory.
            target_hl_state_->cursor = active_cursor_;
        }

        target_hl_state_ = hl_state;
        active_cursor_ = target_hl_state_->cursor;

        if (DebugLowLevelScheduler) {
            hl_executor_.s2e().getMessagesStream()
                    << "New cursor at " << active_cursor_ << '\n';
            hl_executor_.s2e().getMessagesStream()
                    << "Accessible states: " << countAccessibleStates(active_cursor_)
                    << '\n';
        }
    }

    trySchedule();
}


LowLevelTopoStrategy::~LowLevelTopoStrategy() {
    on_stack_frame_push_.disconnect();
    on_stack_frame_popping_.disconnect();
    on_basic_block_enter_.disconnect();

    hl_executor_.s2e().getExecutor()->setSearcher(old_searcher_);
}


void LowLevelTopoStrategy::onStackFramePush(CallStack *stack,
        boost::shared_ptr<CallStackFrame> old_top,
        boost::shared_ptr<CallStackFrame> new_top) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
    assert(!state->topo_index.empty());

    // TODO: Move all this stuff in a separate TopologicIndex class
    shared_ptr<TopologicNode> slot = state->topo_index.back();
    shared_ptr<TopologicNode> next_slot = slot->getDown(true);
    state->topo_index.push_back(next_slot);

    next_slot->states.insert(state);
    slot->states.remove(state);
}


void LowLevelTopoStrategy::onStackFramePopping(CallStack *stack,
        boost::shared_ptr<CallStackFrame> old_top,
        boost::shared_ptr<CallStackFrame> new_top) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
    assert(!state->topo_index.empty());

    shared_ptr<TopologicNode> slot = state->topo_index.back();
    shared_ptr<TopologicNode> next_slot;

    while (!state->topo_index.empty() && !state->topo_index.back()->is_call_base) {
        state->topo_index.pop_back();
    }
    assert(state->topo_index.size() > 1);
    state->topo_index.pop_back();

    next_slot = state->topo_index.back()->getNext(
                state->topo_index.back()->basic_block,
                state->topo_index.back()->call_index + 1);
    state->topo_index.back() = next_slot;

    next_slot->states.insert(state);
    slot->states.remove(state);
}


void LowLevelTopoStrategy::onBasicBlockEnter(CallStack *stack,
        boost::shared_ptr<CallStackFrame> top,
        bool &schedule_state) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
    assert(!state->topo_index.empty());

    int bb = top->bb_index;

#if 0
    static int dbg_counter = 0;
    if (dbg_counter > 100) {
        hl_executor_.s2e().getMessagesStream(stack->s2e_state())
                    << "Hit BB: " << bb << ' '
                    << "Stack topo depth: " << state->topo_index.size() << '\n';
        dbg_counter = 0;
    }
    dbg_counter++;
#endif

    stepBasicBlock(state, bb);

    // TODO: This part is a bit rough. Think it through later.

    TopologicNode::StateSet &state_set = state->topo_index.back()->states;
    if (state_set.size() > 1) {
        hl_executor_.s2e().getMessagesStream(stack->s2e_state())
                << "Merging opportunity for "
                << state_set.size() << " states." << '\n';

        {
            S2EExecutionState *s2e_state = state->s2e_state();
            // Skip the current instruction, since we'll throw at the end
            s2e_state->regs()->write<target_ulong>(CPU_OFFSET(eip), s2e_state->getPc() + S2E_CUSTOM_INSTRUCTION_SIZE);

            // XXX: Not sure why we clear these.  Ask Vitaly next time...
            s2e_state->regs()->write(CPU_OFFSET(cc_op), 0);
            s2e_state->regs()->write(CPU_OFFSET(cc_src), 0);
            s2e_state->regs()->write(CPU_OFFSET(cc_dst), 0);
            s2e_state->regs()->write(CPU_OFFSET(cc_tmp), 0);

            // What is the performance impact of this?
            // Do we ever need a TLB in symbolic mode?
            tlb_flush(env, 1);
        }

        // Find the first state that we can merge with
        bool success = false;
        for (TopologicNode::StateSet::iterator it = state_set.begin(),
                ie = state_set.end(); it != ie; ++it) {
            shared_ptr<LowLevelState> other_state = *it;
            if (other_state == state) {
                continue;
            }

            success = hl_executor_.s2e().getExecutor()->merge(
                    *other_state->s2e_state(),
                    *state->s2e_state());

            if (success) {
                hl_executor_.s2e().getMessagesStream(stack->s2e_state())
                        << "*** MERGE SUCCESSFUL ***" << '\n';
                break;
            } else {
                hl_executor_.s2e().getMessagesStream(stack->s2e_state())
                        << "*** MERGE FAIL, moving on ***" << '\n';
            }
        }
        if (success) {
            // We don't call the scheduler on this path because it will
            // be invoked by the code that handles the state termination.
            hl_executor_.s2e().getExecutor()->terminateStateEarly(
                    *state->s2e_state(), "Killed by merge");
            assert(0 && "Unreachable");
        } else {
            trySchedule();
            hl_executor_.s2e().getExecutor()->yieldState(*state->s2e_state());
            throw CpuExitException();
        }
    }

    // Check if a new state should be scheduled
    schedule_state = trySchedule();
}


void LowLevelTopoStrategy::stepBasicBlock(shared_ptr<LowLevelState> state,
        int bb) {
    shared_ptr<TopologicNode> slot = state->topo_index.back();
    shared_ptr<TopologicNode> next_slot;

    if (bb <= state->topo_index.back()->basic_block) {
        next_slot = state->topo_index.back()->getDown(false);
        next_slot = next_slot->getNext(bb, 0);
        state->topo_index.push_back(next_slot);
    } else {
        while(!state->topo_index.back()->is_call_base) {
            shared_ptr<TopologicNode> prev = *(state->topo_index.end() - 2);
            if (bb <= prev->basic_block)
                break;
            state->topo_index.pop_back();
        }
        next_slot = state->topo_index.back()->getNext(bb, 0);
        state->topo_index.back() = next_slot;
    }

    next_slot->states.insert(state);
    slot->states.remove(state);
}


bool LowLevelTopoStrategy::trySchedule() {
    if (!target_hl_state_) {
        current_ll_state_.reset();
        hl_executor_.s2e().getWarningsStream()
                << "LowLevelTopoStrategy: No high-level state registered. "
                << "Resorting to underlying strategy..." << '\n';
        return false;
    }

    long int counter;
    shared_ptr<LowLevelState> next_state = findNextState(
                target_hl_state_->id(), active_cursor_, counter);
    assert(next_state && "Could not find next state. Perhaps wrong cursor position?");

    if (next_state == current_ll_state_) {
        return false;
    }

    current_ll_state_ = next_state;
    return true;
}


klee::ExecutionState &LowLevelTopoStrategy::selectState() {
    if (!current_ll_state_) {
        return old_searcher_->selectState();
    }

    return *current_ll_state_->s2e_state();
}

void LowLevelTopoStrategy::update(klee::ExecutionState *current,
            const std::set<klee::ExecutionState*> &addedStates,
            const std::set<klee::ExecutionState*> &removedStates) {
    old_searcher_->update(current, addedStates, removedStates);
}

bool LowLevelTopoStrategy::empty() {
    return old_searcher_->empty();
}

} /* namespace s2e */
