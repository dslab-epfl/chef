/*
 * StreamAnalyzer.h
 *
 *  Created on: Mar 4, 2015
 *      Author: stefan
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

template<typename State>
class StreamAnalyzer {
public:
    typedef boost::shared_ptr<State> StateRef;

    StreamAnalyzer(S2E &s2e, ExecutionStream &estream)
        : s2e_(s2e), stream_(estream) {
        on_state_fork_ = stream_.onStateFork.connect(
                sigc::mem_fun(*this, StreamAnalyzer<State>::onStateFork));
    }

    virtual ~StreamAnalyzer() {
        on_state_fork_.disconnect();
    }

    S2E &s2e() {
        return s2e_;
    }

    ExecutionStream &stream() {
        return stream_;
    }

    StateRef getState(S2EExecutionState *s2e_state) {
        typename StateMap::iterator it = state_map_.find(s2e_state);
        if (it == state_map_.end()) {
            StateRef state = boost::make_shared<State>(s2e_state);
            state_map_.insert(std::make_pair(s2e_state, state));
            return state;
        }

        return it->second;
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

            StateRef state = boost::make_shared<State>(it->second);
            bool success = state_map_.insert(std::make_pair(new_s2e_state,
                    state)).second;
            assert(success && "Could not insert new state");
        }
    }

    typedef llvm::DenseMap<S2EExecutionState*, StateRef> StateMap;

    S2E &s2e_;
    ExecutionStream &stream_;

    StateMap state_map_;

    sigc::connection on_state_fork_;

    // Non-copyable
    StreamAnalyzer(const StreamAnalyzer&);
    void operator=(const StreamAnalyzer&);
};


} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_STREAMANALYZER_H_ */
