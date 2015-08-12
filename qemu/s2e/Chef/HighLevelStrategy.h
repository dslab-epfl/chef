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

#ifndef QEMU_S2E_CHEF_HIGHLEVELSTRATEGY_H_
#define QEMU_S2E_CHEF_HIGHLEVELSTRATEGY_H_

#include <vector>
#include <boost/shared_ptr.hpp>

namespace s2e {

class HighLevelState;


class HighLevelStrategy {
public:
    typedef boost::shared_ptr<HighLevelState> StateRef;
public:
    virtual ~HighLevelStrategy() { }

    virtual void addStates(StateRef current,
            const std::vector<StateRef> &states) = 0;
    virtual void killState(StateRef state) = 0;
    virtual void updateState(StateRef state) = 0;

    virtual StateRef selectState() = 0;
};


class HighLevelStrategyFactory {
public:
    virtual ~HighLevelStrategyFactory() { }

    virtual HighLevelStrategy *createStrategy() = 0;
};


class RandomPathStrategy : public HighLevelStrategy {
public:
    RandomPathStrategy();

    void addStates(StateRef current, const std::vector<StateRef> &states);
    void killState(StateRef state);
    void updateState(StateRef state);

    StateRef selectState();
};


template<class Selector>
class SelectorStrategy : public HighLevelStrategy {
public:
    SelectorStrategy() {

    }

    void addStates(StateRef current, const std::vector<StateRef> &states) {
        if (current) {
            bool result = selector_.update(current);
            assert(!result && "Current state was not present in the selector");
        }
        for (std::vector<StateRef>::const_iterator it = states.begin(),
                ie = states.end(); it != ie; ++it) {
            bool result = selector_.update(*it);
            assert(result && "State already added");
        }
    }

    void killState(StateRef state) {
        bool result = selector_.remove(state);
        assert(result && "State killed twice");
    }

    void updateState(StateRef state) {
        bool result = selector_.update(state);
        assert(!result && "Current state was not present in the selector");
    }

    StateRef selectState() {
        return selector_.select();
    }
private:
    Selector selector_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_HIGHLEVELSTRATEGY_H_ */
