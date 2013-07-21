/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2013, Dependable Systems Laboratory, EPFL
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
 * All contributors are listed in the S2E-AUTHORS file.
 */


extern "C" {
#include "config.h"
#include "qemu-common.h"
#include "cpu.h"
extern CPUX86State *env;
}


#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>
#include <s2e/Utils.h>
#include <s2e/S2EExecutor.h>

#include <iostream>

#include "MergingSearcher.h"

namespace s2e {
namespace plugins {

using namespace llvm;

S2E_DEFINE_PLUGIN(MergingSearcher, "Searcher to be used with state merging",
                  "MergingSearcher");

void MergingSearcher::initialize()
{
    s2e()->getExecutor()->setSearcher(this);
    m_currentState = NULL;
    m_nextMergeGroupId = 1;
}

klee::ExecutionState& MergingSearcher::selectState()
{
    S2EExecutionState *state = m_currentState;
    if (state) {
        return *state;
    }

    assert(!m_activeStates.empty());

    state = *m_activeStates.begin();
    m_currentState = state;
    return *state;
}


void MergingSearcher::update(klee::ExecutionState *current,
                    const std::set<klee::ExecutionState*> &addedStates,
                    const std::set<klee::ExecutionState*> &removedStates)
{
    States states;
    foreach2(it, addedStates.begin(), addedStates.end()) {
        S2EExecutionState *state = static_cast<S2EExecutionState*>(*it);
        states.insert(state);
    }

    foreach2(it, removedStates.begin(), removedStates.end()) {
        S2EExecutionState *state = static_cast<S2EExecutionState*>(*it);
        states.erase(state);
        m_activeStates.erase(state);

        DECLARE_PLUGINSTATE(MergingSearcherState, state);
        if (plgState->getGroupId()) {
            m_mergePools[plgState->getGroupId()].states.erase(state);
        }

        if (state == m_currentState) {
            m_currentState = NULL;
        }
    }

    foreach2(it, states.begin(), states.end()) {
        S2EExecutionState *state = static_cast<S2EExecutionState*>(*it);
        m_activeStates.insert(state);

        DECLARE_PLUGINSTATE(MergingSearcherState, state);
        if (plgState->getGroupId()) {
            m_mergePools[plgState->getGroupId()].states.insert(state);
        }
    }
}

bool MergingSearcher::empty()
{
    return m_activeStates.empty();
}

void MergingSearcher::suspend(S2EExecutionState *state)
{
    s2e()->getDebugStream(NULL) << "MergingSearcher: "
            << "suspending state " << state->getID() << "\n";

    if (m_currentState == state) {
        m_currentState = NULL;
    }

    m_activeStates.erase(state);
}

void MergingSearcher::resume(S2EExecutionState *state)
{
    s2e()->getDebugStream(NULL) << "MergingSearcher: "
            << "resuming state " << state->getID() << "\n";
    m_activeStates.insert(state);
}

bool MergingSearcher::mergeStart(S2EExecutionState *state)
{
    DECLARE_PLUGINSTATE(MergingSearcherState, state);

    if (plgState->getGroupId() != 0) {
        s2e()->getWarningsStream(state) << "MergingSearcher: state id already has group id "
                << plgState->getGroupId() << "\n";
        return false;
    }

    uint64_t id = m_nextMergeGroupId++;

    s2e()->getWarningsStream(state) <<
            "MergingSearcher: starting merge group " << id << "\n";

    plgState->setGroupId(id);
    m_mergePools[id].states.insert(state);
    state->setPinned(true);
    return true;
}

bool MergingSearcher::mergeEnd(S2EExecutionState *state, bool skipOpcode, bool clearTmpFlags)
{
    DECLARE_PLUGINSTATE(MergingSearcherState, state);
    s2e()->getWarningsStream(state) << "MergingSearcher: merging state\n";

    MergePools::iterator it = m_mergePools.find(plgState->getGroupId());
    if (it == m_mergePools.end()) {
        s2e()->getWarningsStream(state) << "MergingSearcher: state does not belong to a merge group\n";
        return false;
    }

    merge_pool_t &mergePool = (*it).second;

    mergePool.states.erase(state);
    if (mergePool.states.empty() && !mergePool.firstState) {
        //No states forked in the merge pool when the merge point was reached,
        //so there is nothing to merge and therefore we return.
        plgState->setGroupId(0);
        m_mergePools.erase(it);
        state->setPinned(false);
        return true;
    }

    // Skip the opcode
    if (skipOpcode) {
        state->regs()->write<target_ulong>(CPU_OFFSET(eip), state->getPc() + 10);
    }

    // Clear temp flags.
    // This assumes we were called through the custom instructions,
    // implying that the flags can be clobbered.
    // XXX: is it possible that these can be symbolic?
    if (clearTmpFlags) {
        state->regs()->write(CPU_OFFSET(cc_op), 0);
        state->regs()->write(CPU_OFFSET(cc_src), 0);
        state->regs()->write(CPU_OFFSET(cc_dst), 0);
        state->regs()->write(CPU_OFFSET(cc_tmp), 0);
    }

    //The TLB state must be identical when we merge
    tlb_flush(env, 1);

    if (!mergePool.firstState) {
        //state is the first to reach merge_end.
        //all other states that reach merge_end will be merged with it and destroyed
        //first_state accumulates all the merges
        mergePool.firstState = state;
        suspend(state);
        g_s2e->getExecutor()->yieldState(*state);
        assert(false && "Can't get here");
        return false;
    }


    bool success = g_s2e->getExecutor()->merge(*mergePool.firstState, *state);

    if (mergePool.states.empty()) {
        resume(mergePool.firstState);
        DECLARE_PLUGINSTATE(MergingSearcherState, mergePool.firstState);
        plgState->setGroupId(0);
        mergePool.firstState->setPinned(false);
        m_mergePools.erase(it);
    }

    if (success) {
        g_s2e->getExecutor()->terminateStateEarly(*state, "Killed by merge");
    }

    //Symbolic state may be changed, need to restart
    throw CpuExitException();
}

void MergingSearcher::handleOpcodeInvocation(S2EExecutionState *state,
                                    uint64_t guestDataPtr,
                                    uint64_t guestDataSize)
{
    merge_desc_t command;

    if (guestDataSize != sizeof(command)) {
        s2e()->getWarningsStream(state) <<
                "MergingSearcher: mismatched merge_desc_t size\n";
        return;
    }

    if (!state->mem()->readMemoryConcrete(guestDataPtr, &command, guestDataSize)) {
        s2e()->getWarningsStream(state) <<
                "MergingSearcher: could not read transmitted data\n";
        return;
    }

    if (command.start) {
        mergeStart(state);
    } else {
        mergeEnd(state, true, true);
    }
}

MergingSearcherState::MergingSearcherState()
{
    m_groupId = 0;
}

MergingSearcherState::~MergingSearcherState()
{

}

MergingSearcherState* MergingSearcherState::clone() const
{
    return new MergingSearcherState(*this);
}

PluginState *MergingSearcherState::factory(Plugin *p, S2EExecutionState *s)
{
    return new MergingSearcherState();
}

} // namespace plugins
} // namespace s2e
