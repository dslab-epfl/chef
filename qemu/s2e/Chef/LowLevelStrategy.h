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

#ifndef QEMU_S2E_CHEF_LOWLEVELSTRATEGY_H_
#define QEMU_S2E_CHEF_LOWLEVELSTRATEGY_H_

#include <klee/Searcher.h>

#include <boost/shared_ptr.hpp>

namespace klee {
class ExecutionState;
}

namespace s2e {

class HighLevelExecutor;
class HighLevelState;
class LowLevelState;

class LowLevelStrategy : public klee::Searcher {
public:
    LowLevelStrategy(HighLevelExecutor &hl_executor);
    virtual ~LowLevelStrategy();

    virtual void updateTargetHighLevelState(boost::shared_ptr<HighLevelState> hl_state) = 0;

    // klee::Searcher
    klee::ExecutionState &selectState();
    void update(klee::ExecutionState *current,
            const std::set<klee::ExecutionState*> &addedStates,
            const std::set<klee::ExecutionState*> &removedStates);
    bool empty();

protected:
    klee::Searcher *old_searcher() const {
        return old_searcher_;
    }

    HighLevelExecutor &hl_executor() const {
        return hl_executor_;
    }

    virtual LowLevelState *selectLowLevelState() = 0;

private:
    HighLevelExecutor &hl_executor_;
    klee::Searcher *old_searcher_;
};


class LowLevelStrategyFactory {
public:
    virtual ~LowLevelStrategyFactory() { }

    virtual LowLevelStrategy *createStrategy(HighLevelExecutor &hl_executor) = 0;
};


class LowLevelSproutStrategy : public LowLevelStrategy {
public:
    LowLevelSproutStrategy(HighLevelExecutor &hl_executor);

    void updateTargetHighLevelState(boost::shared_ptr<HighLevelState> hl_state);

protected:
    LowLevelState *selectLowLevelState();

    boost::shared_ptr<HighLevelState> target_hl_state_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_LOWLEVELSTRATEGY_H_ */
