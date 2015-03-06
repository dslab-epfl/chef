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

#ifndef QEMU_S2E_CHEF_STREAMANALYZER_H_
#define QEMU_S2E_CHEF_STREAMANALYZER_H_


#include <s2e/S2E.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Chef/ExecutionStream.h>

#include <llvm/ADT/DenseMap.h>

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace s2e {

template<typename Analyzer>
class StreamAnalyzerState {
public:
    StreamAnalyzerState(Analyzer &analyzer, S2EExecutionState *s2e_state)
        : analyzer_(analyzer), s2e_state_(s2e_state) {

    }

    StreamAnalyzerState(const StreamAnalyzerState<Analyzer> &other,
            S2EExecutionState *s2e_state)
        : analyzer_(other.analyzer_), s2e_state_(s2e_state) {

    }

    virtual ~StreamAnalyzerState() {

    }

    S2E &s2e() const {
        return analyzer_.s2e();
    }

    S2EExecutionState* s2e_state() const {
        return s2e_state_;
    }

protected:
    Analyzer &analyzer() {
        return analyzer_;
    }

private:
    Analyzer &analyzer_;
    S2EExecutionState *s2e_state_;
};


template<typename State, typename Analyzer>
class StreamAnalyzer {
public:
    typedef boost::shared_ptr<State> StateRef;

    StreamAnalyzer(S2E &s2e, ExecutionStream &estream)
        : s2e_(s2e), stream_(estream) {
        on_state_fork_ = stream_.onStateFork.connect(
                sigc::mem_fun(*this, &StreamAnalyzer<State, Analyzer>::onStateFork));
        on_state_kill_ = stream_.onStateKill.connect(
                sigc::mem_fun(*this, &StreamAnalyzer<State, Analyzer>::onStateKill));
    }

    virtual ~StreamAnalyzer() {
        on_state_fork_.disconnect();
        on_state_kill_.disconnect();
    }

    S2E &s2e() const {
        return s2e_;
    }

    ExecutionStream &stream() const {
        return stream_;
    }

    StateRef getState(S2EExecutionState *s2e_state) {
        assert(s2e_state != NULL);

        if (lru_.first == s2e_state) {
            return lru_.second;
        }

        typename StateMap::iterator it = state_map_.find(s2e_state);
        if (it == state_map_.end()) {
            StateRef state = StateRef(new State(*static_cast<Analyzer*>(this),
                    s2e_state));
            lru_ = std::make_pair(s2e_state, state);
            state_map_.insert(lru_);
            return state;
        }

        lru_ = *it;
        return lru_.second;
    }

private:
    typedef std::vector<S2EExecutionState*> S2EStateVector;

    void onStateFork(S2EExecutionState *s2e_state,
            const S2EStateVector &newStates,
            const std::vector<klee::ref<klee::Expr> > &newConditions) {
        typename StateMap::iterator it = state_map_.find(s2e_state);
        if (it == state_map_.end()) {
            return;
        }

        for (S2EStateVector::const_iterator vit = newStates.begin(),
                vie = newStates.end(); vit != vie; ++vit) {
            S2EExecutionState *new_s2e_state = *vit;
            if (new_s2e_state == s2e_state)
                continue;

            StateRef state = boost::make_shared<State>(*it->second,
                    new_s2e_state);
            bool success = state_map_.insert(std::make_pair(new_s2e_state,
                    state)).second;
            assert(success && "Could not insert new state");
        }
    }

    void onStateKill(S2EExecutionState *s2e_state) {
        if (lru_.first == s2e_state) {
            lru_ = std::pair<S2EExecutionState*, StateRef>();
        }
        state_map_.erase(s2e_state);
    }

    typedef llvm::DenseMap<S2EExecutionState*, StateRef> StateMap;

    S2E &s2e_;
    ExecutionStream &stream_;

    StateMap state_map_;
    std::pair<S2EExecutionState*, StateRef> lru_;

    sigc::connection on_state_fork_;
    sigc::connection on_state_kill_;

    // Non-copyable
    StreamAnalyzer(const StreamAnalyzer&);
    void operator=(const StreamAnalyzer&);
};


} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_STREAMANALYZER_H_ */
