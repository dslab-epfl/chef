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

#include "LowLevelStrategy.h"

#include <s2e/S2EExecutor.h>
#include <s2e/Chef/HighLevelExecutor.h>

#include <boost/shared_ptr.hpp>


using boost::shared_ptr;


namespace s2e {

// LowLevelStrategy ////////////////////////////////////////////////////////////

LowLevelStrategy::LowLevelStrategy(HighLevelExecutor &hl_executor)
    : hl_executor_(hl_executor) {
    old_searcher_ = hl_executor_.s2e().getExecutor()->getSearcher();
    hl_executor_.s2e().getExecutor()->setSearcher(this);
}


LowLevelStrategy::~LowLevelStrategy() {
    hl_executor_.s2e().getExecutor()->setSearcher(old_searcher_);
}


klee::ExecutionState &LowLevelStrategy::selectState() {
    LowLevelState *ll_state = selectLowLevelState();
    if (!ll_state) {
        hl_executor_.s2e().getWarningsStream()
                << "LowLevelStrategy: No high-level state registered. "
                << "Resorting to underlying strategy..." << '\n';
        return old_searcher_->selectState();
    }

    return *ll_state->s2e_state();
}


void LowLevelStrategy::update(klee::ExecutionState *current,
        const std::set<klee::ExecutionState*> &addedStates,
        const std::set<klee::ExecutionState*> &removedStates) {
    old_searcher_->update(current, addedStates, removedStates);
}


bool LowLevelStrategy::empty() {
    return old_searcher_->empty();
}

// LowLevelSproutStrategy //////////////////////////////////////////////////////

LowLevelSproutStrategy::LowLevelSproutStrategy(HighLevelExecutor &hl_executor)
    : LowLevelStrategy(hl_executor) {

}

void LowLevelSproutStrategy::updateTargetHighLevelState(
        boost::shared_ptr<HighLevelState> hl_state) {
    target_hl_state_ = hl_state;
}

LowLevelState *LowLevelSproutStrategy::selectLowLevelState() {
    if (!target_hl_state_)
        return NULL;

    assert(!target_hl_state_->segment->low_level_states.empty());
    return (*target_hl_state_->segment->low_level_states.begin()).lock().get();
}

} /* namespace s2e */
