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

#ifndef QEMU_S2E_CHEF_HIGHLEVELEXECUTOR_H_
#define QEMU_S2E_CHEF_HIGHLEVELEXECUTOR_H_

#include <s2e/Chef/StreamAnalyzer.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <llvm/ADT/DenseMap.h>
//#include <llvm/ADT/DenseSet.h>
//#include <llvm/ADT/SmallSet.h>
#include <set>

namespace s2e {


class HighLevelStack;

class HLExecutorState;


class HighLevelState {

};


class HighLevelPathSegment : public boost::enable_shared_from_this<HighLevelPathSegment> {
public:
    typedef boost::shared_ptr<HighLevelPathSegment> SharedHLPSRef;
public:
    HighLevelPathSegment();
    HighLevelPathSegment(uint64_t hlpc, SharedHLPSRef parent);
    ~HighLevelPathSegment();

    uint64_t hlpc() const {
        return hlpc_;
    }

    boost::shared_ptr<HighLevelPathSegment> parent() {
        return parent_;
    }

    boost::shared_ptr<HighLevelPathSegment> getNext(uint64_t hlpc);

public:
    // TODO: Replace with DenseSet if slow
    typedef std::set<boost::shared_ptr<HLExecutorState> > StateSet;
    StateSet states;

private:
    typedef boost::weak_ptr<HighLevelPathSegment> WeakHLPSRef;
    typedef llvm::SmallDenseMap<uint64_t, WeakHLPSRef> ChildrenMap;

    SharedHLPSRef parent_;
    ChildrenMap children_;

    uint64_t hlpc_;

    // Non-copyable
    HighLevelPathSegment(const HighLevelPathSegment&);
    void operator=(const HighLevelPathSegment&);
};


class HighLevelExecutor;


class HLExecutorState : public StreamAnalyzerState<HLExecutorState, HighLevelExecutor>,
                        public boost::enable_shared_from_this<HLExecutorState> {
public:
    void step(uint64_t hlpc);

    boost::shared_ptr<HLExecutorState> clone(S2EExecutionState *s2e_state);

private:
    HLExecutorState(HighLevelExecutor &analyzer, S2EExecutionState *s2e_state);

    boost::shared_ptr<HighLevelPathSegment> segment_;

    friend class HighLevelExecutor;
};


class HighLevelExecutor : public StreamAnalyzer<HLExecutorState> {
public:
    HighLevelExecutor(S2E &s2e, ExecutionStream &stream);
    virtual ~HighLevelExecutor();

    boost::shared_ptr<HighLevelPathSegment> root_segment() {
        return root_segment_;
    }

protected:
    boost::shared_ptr<HLExecutorState> createState(S2EExecutionState *s2e_state);

private:
    void onHighLevelPCUpdate(S2EExecutionState *s2e_state,
            HighLevelStack *hl_stack);

    boost::shared_ptr<HighLevelPathSegment> root_segment_;
};


} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_HIGHLEVELEXECUTOR_H_ */
