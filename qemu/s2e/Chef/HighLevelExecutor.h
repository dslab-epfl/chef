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
#include <llvm/ADT/SetVector.h>
//#include <llvm/ADT/DenseSet.h>
//#include <llvm/ADT/SmallSet.h>
#include <set>

namespace s2e {

class InterpreterDetector;
class HighLevelStack;
class LowLevelState;
class HighLevelState;
class HighLevelExecutor;
class HighLevelStrategy;
class LowLevelTopoStrategy;


class HighLevelPathSegment : public boost::enable_shared_from_this<HighLevelPathSegment> {
public:
    typedef boost::shared_ptr<HighLevelPathSegment> SharedHLPSRef;
    typedef boost::weak_ptr<HighLevelPathSegment> WeakHLPSRef;

    typedef llvm::SmallDenseMap<uint64_t, SharedHLPSRef, 2> ChildrenMap;
    // TODO: Replace with DenseSet if slow
    typedef std::set<boost::weak_ptr<LowLevelState> > LowLevelStateSet;
public:
    HighLevelPathSegment();
    HighLevelPathSegment(uint64_t hlpc, SharedHLPSRef parent);
    ~HighLevelPathSegment();

    boost::shared_ptr<HighLevelPathSegment> getNext(uint64_t hlpc);

    uint64_t hlpc;

    WeakHLPSRef parent;
    ChildrenMap children;

    LowLevelStateSet low_level_states;
    boost::weak_ptr<HighLevelState> high_level_state;

private:
    // Non-copyable
    HighLevelPathSegment(const HighLevelPathSegment&);
    void operator=(const HighLevelPathSegment&);
};


/**
 * Invariant: A high-level state is at the lowest point in the low-level
 * execution trace.  It always has at least one low-level state active.
 * When the last low-level state moves down in the tree, the HL state is
 * advanced (and potentially forked).
 */
class HighLevelState : public boost::enable_shared_from_this<HighLevelState> {
public:
    HighLevelState(boost::shared_ptr<HighLevelPathSegment> segment);
    virtual ~HighLevelState();

    int id() const {
        return id_;
    }

    void step(uint64_t hlpc);
    boost::shared_ptr<HighLevelState> fork(uint64_t hlpc);
    void terminate();

    boost::shared_ptr<HighLevelPathSegment> segment;

private:
    int id_;

    // Non-copyable
    HighLevelState(const HighLevelState&);
    void operator=(const HighLevelState&);

    friend class HighLevelExecutor;
};


struct TopologicNode : public boost::enable_shared_from_this<TopologicNode> {
    typedef llvm::SetVector<boost::shared_ptr<LowLevelState> > StateSet;

    TopologicNode(int bb, int ci, bool cb);

    int basic_block;
    int call_index;
    bool is_call_base;

    boost::shared_ptr<TopologicNode> next;
    boost::shared_ptr<TopologicNode> down;
    StateSet states;

    boost::shared_ptr<TopologicNode> getDown(bool cb);
    boost::shared_ptr<TopologicNode> getNext(int bb, int ci);

private:
    // Non-copyable
    TopologicNode(const TopologicNode&);
    void operator=(const TopologicNode&);
};


typedef std::vector<boost::shared_ptr<TopologicNode> > TopologicIndex;


class LowLevelState : public StreamAnalyzerState<LowLevelState, HighLevelExecutor>,
                      public boost::enable_shared_from_this<LowLevelState> {
public:
    void step(uint64_t hlpc);

    boost::shared_ptr<LowLevelState> clone(S2EExecutionState *s2e_state);
    void terminate();

    boost::shared_ptr<HighLevelPathSegment> segment;

    // This is updated only by the strategies that need it
    // (currently, LowLevelTopoStrategy)
    TopologicIndex topo_index;

private:
    LowLevelState(HighLevelExecutor &analyzer, S2EExecutionState *s2e_state);

    friend class HighLevelExecutor;
};


class HighLevelExecutor : public StreamAnalyzer<LowLevelState> {
public:
    typedef std::set<boost::shared_ptr<HighLevelState> > HighLevelStateSet;

public:
    HighLevelExecutor(InterpreterDetector &detector,
            HighLevelStrategy &strategy);
    virtual ~HighLevelExecutor();

    InterpreterDetector &detector() {
        return detector_;
    }

    sigc::signal<void,
                 HighLevelState*>
        onHighLevelStateCreate;

    sigc::signal<void,
                 HighLevelState*>
        onHighLevelStateStep;

    sigc::signal<void,
                 HighLevelState*,
                 const std::vector<HighLevelState*>& >
        onHighLevelStateFork;

    sigc::signal<void,
                 HighLevelState*>
        onHighLevelStateKill;

    sigc::signal<void,
                 HighLevelState*,
                 HighLevelState*>
        onHighLevelStateSwitch;

protected:
    boost::shared_ptr<LowLevelState> createState(S2EExecutionState *s2e_state);

private:
    void onHighLevelPCUpdate(S2EExecutionState *s2e_state,
            HighLevelStack *hl_stack);

    void registerHighLevelState(boost::shared_ptr<HighLevelState> hl_state);
    void deregisterHighLevelState(boost::shared_ptr<HighLevelState> hl_state);

    void tryUpdateSelectedState();
    bool doUpdateSelectedState();

    InterpreterDetector &detector_;
    HighLevelStrategy &hl_strategy_;
    boost::scoped_ptr<LowLevelTopoStrategy> ll_strategy_;
    sigc::connection on_high_level_pc_update_;

    HighLevelStateSet high_level_states_;
    boost::shared_ptr<HighLevelState> selected_state_;
    int id_counter_;

    friend class LowLevelState;
};


} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_HIGHLEVELEXECUTOR_H_ */
