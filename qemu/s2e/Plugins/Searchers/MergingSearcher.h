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

#ifndef S2E_PLUGINS_MERGINGSEARCHER_H
#define S2E_PLUGINS_MERGINGSEARCHER_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/ModuleExecutionDetector.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Plugins/BaseInstructions.h>

#include <llvm/ADT/DenseSet.h>

#include <klee/Searcher.h>

namespace s2e {
namespace plugins {

class IMergingSearcher {
public:
    virtual S2EExecutionState *selectState() = 0;
    virtual void update(klee::ExecutionState *current,
           const std::set<klee::ExecutionState*> &addedStates,
           const std::set<klee::ExecutionState*> &removedStates) = 0;

    virtual void setActive(S2EExecutionState *state, bool active) = 0;
};

class MergingSearcher : public Plugin, public klee::Searcher, public BaseInstructionsPluginInvokerInterface
{
    S2E_PLUGIN

public:
    typedef llvm::DenseSet<S2EExecutionState*> States;

private:
    /* Custom instruction command */
    struct merge_desc_t {
        uint64_t start;
    };

    struct merge_pool_t {
        /* First state that got to the merge_end instruction */
        S2EExecutionState *firstState;

        /* All the states that belong to the pool */
        States states;

        merge_pool_t() {
            firstState = NULL;
        }
    };

    /* maps a group id to the first state */
    typedef std::map<uint64_t, merge_pool_t> MergePools;

    MergePools m_mergePools;
    States m_activeStates;
    S2EExecutionState *m_currentState;
    uint64_t m_nextMergeGroupId;

    IMergingSearcher *m_selector;

    bool m_debug;

public:
    MergingSearcher(S2E* s2e): Plugin(s2e) {}
    void initialize();

    void setCustomSelector(IMergingSearcher *selector) {
        m_selector = selector;
    }

    States& getActiveStates() {
        return m_activeStates;
    }

    virtual klee::ExecutionState& selectState();


    virtual void update(klee::ExecutionState *current,
                        const std::set<klee::ExecutionState*> &addedStates,
                        const std::set<klee::ExecutionState*> &removedStates);

    virtual bool empty();

    bool mergeStart(S2EExecutionState *state);
    bool mergeEnd(S2EExecutionState *state, bool skipOpcode, bool clearTmpFlags);

private:
    void suspend(S2EExecutionState *state);
    void resume(S2EExecutionState *state);


    virtual void handleOpcodeInvocation(S2EExecutionState *state,
                                        uint64_t guestDataPtr,
                                        uint64_t guestDataSize);
};


class MergingSearcherState: public PluginState
{
private:
    uint64_t m_groupId;


public:
    MergingSearcherState();
    virtual ~MergingSearcherState();
    virtual MergingSearcherState* clone() const;
    static PluginState *factory(Plugin *p, S2EExecutionState *s);

    void setGroupId(uint64_t groupId) {
        m_groupId = groupId;
    }

    uint64_t getGroupId() const {
        return m_groupId;
    }
};


} // namespace plugins
} // namespace s2e

#endif
