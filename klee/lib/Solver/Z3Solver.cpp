/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2014, Dependable Systems Laboratory, EPFL
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

#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/SolverStats.h"
#include "klee/Constraints.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "klee/Common.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include "Z3Builder.h"

#include <boost/scoped_ptr.hpp>

#include <z3++.h>

#include <list>
#include <iostream>

using namespace llvm;
using boost::scoped_ptr;


namespace {
enum Z3ArrayConsMode {
    Z3_ARRAY_ITE,
    Z3_ARRAY_STORES,
    Z3_ARRAY_ASSERTS
};

cl::opt<Z3ArrayConsMode> ArrayConsMode("z3-array-cons-mode",
        cl::desc("Array construction mode in Z3"),
        cl::values(
                clEnumValN(Z3_ARRAY_ITE, "ite", "If-then-else expressions over BV variables"),
                clEnumValN(Z3_ARRAY_STORES, "stores", "Nested store expressions"),
                clEnumValN(Z3_ARRAY_ASSERTS, "asserts", "Assertions over array values"),
                clEnumValEnd),
        cl::init(Z3_ARRAY_ASSERTS));
}


namespace klee {


class Z3BaseSolverImpl : public SolverImpl {
public:
    Z3BaseSolverImpl();
    virtual ~Z3BaseSolverImpl();

    bool computeTruth(const Query&, bool &isValid);
    bool computeValue(const Query&, ref<Expr> &result);
    bool computeInitialValues(const Query &query,
                              const std::vector<const Array*> &objects,
                              std::vector<std::vector<unsigned char> > &values,
                              bool &hasSolution);

protected:
    void configureSolver();
    void resetBuilder();

    virtual void preCheck(const Query&) = 0;
    virtual void postCheck(const Query&) = 0;

    bool check(const Query &query,
               const std::vector<const Array*> &objects,
               std::vector<std::vector<unsigned char> > &values,
               bool &hasSolution);

    z3::context context_;
    z3::solver solver_;

    scoped_ptr<Z3Builder> builder_;
};


class Z3IncrementalSolverImpl : public Z3BaseSolverImpl {
public:
    Z3IncrementalSolverImpl();
    virtual ~Z3IncrementalSolverImpl();

protected:
    typedef std::list<ConditionNodeRef> ConditionNodeList;

    virtual void preCheck(const Query&);
    virtual void postCheck(const Query&);

    scoped_ptr<ConditionNodeList> last_constraints_;
};


class Z3SolverImpl : public Z3BaseSolverImpl {
public:
    Z3SolverImpl();
    virtual ~Z3SolverImpl();

protected:
    virtual void preCheck(const Query&);
    virtual void postCheck(const Query&);
};


////////////////////////////////////////////////////////////////////////////////


Z3Solver::Z3Solver(bool incremental)
    : Solver(incremental ?
                (SolverImpl*) new Z3IncrementalSolverImpl() :
                (SolverImpl*) new Z3SolverImpl()) {

}

////////////////////////////////////////////////////////////////////////////////


Z3BaseSolverImpl::Z3BaseSolverImpl()
    : solver_(context_) {

    configureSolver();
    resetBuilder();
}


Z3BaseSolverImpl::~Z3BaseSolverImpl() {

}


void Z3BaseSolverImpl::resetBuilder() {
    switch (ArrayConsMode) {
    case Z3_ARRAY_ITE:
        builder_.reset(new Z3IteBuilder(context_));
        break;
    case Z3_ARRAY_STORES:
        builder_.reset(new Z3ArrayBuilder(context_));
        break;
    case Z3_ARRAY_ASSERTS:
        builder_.reset(new Z3AssertArrayBuilder(solver_));
        break;
    }
}


bool Z3BaseSolverImpl::check(const Query &query,
                             const std::vector<const Array*> &objects,
                             std::vector<std::vector<unsigned char> > &values,
                             bool &hasSolution) {

    // Note the negation, since we're checking for validity
    // (i.e., a counterexample)
    solver_.add(!builder_->construct(query.expr));

    z3::check_result result = solver_.check();

    if (result != z3::sat) {
        hasSolution = false;
        return true;
    }

    z3::model model = solver_.get_model();

#if 0 // TODO: Turn into proper debug logging
    std::stringstream model_ss;
    model_ss << solver_ << '\n';
    model_ss << model; model_ss.flush();
    errs() << "[Z3][Debug] Model: " << '\n' << model_ss.str() << '\n';
#endif

    values.reserve(objects.size());
    for (std::vector<const Array*>::const_iterator it = objects.begin(),
            ie = objects.end(); it != ie; ++it) {
        const Array *array = *it;
        std::vector<unsigned char> data;

#if 0 // TODO: Turn into proper debug logging
        errs() << "[Z3][Debug] Array name: " << array->name << '\n';
#endif

        data.reserve(array->size);
        for (unsigned offset = 0; offset < array->size; ++offset) {
            z3::expr value_ast = model.eval(
                    builder_->getInitialRead(array, offset), true);
            unsigned value_num;

#if 0 // TODO: Turn into proper debug logging
            std::stringstream ss;
            ss << builder_->getInitialRead(array, offset) << " // " << value_ast; ss.flush();
            errs() << "[Z3][Debug] Initial read eval: " << ss.str() << '\n';
#endif

            Z3_bool conv_result = Z3_get_numeral_uint(context_, value_ast,
                    &value_num);
            assert(conv_result == Z3_TRUE && "Could not convert value");
            assert(value_num < (1 << 8*sizeof(unsigned char))
                    && "Invalid model value");

            data.push_back((unsigned char)value_num);
        }
        values.push_back(data);
    }

    hasSolution = true;
    return true;

}


void Z3BaseSolverImpl::configureSolver() {
    // TODO: Turn this into some sort of optional logging...
    errs() << "[Z3] Initializing..." << '\n';

    Z3_param_descrs solver_params = Z3_solver_get_param_descrs(context_, solver_);
    Z3_param_descrs_inc_ref(context_, solver_params);

    errs() << "[Z3] Available parameters:" << '\n';
    errs() << "[Z3]  " << Z3_param_descrs_to_string(context_, solver_params) << '\n';

    z3::params params(context_);
    params.set("array.extensional", false);
    Z3_params_validate(context_, params, solver_params);

    solver_.set(params);

    Z3_param_descrs_dec_ref(context_, solver_params);
}


bool Z3BaseSolverImpl::computeTruth(const Query &query, bool &isValid) {
    std::vector<const Array*> objects;
    std::vector<std::vector<unsigned char> > values;
    bool hasSolution;

    if (!computeInitialValues(query, objects, values, hasSolution))
        return false;

    isValid = !hasSolution;
    return true;
}

// TODO: Use model evaluation in Z3
bool Z3BaseSolverImpl::computeValue(const Query &query, ref<Expr> &result) {
    std::vector<const Array*> objects;
    std::vector<std::vector<unsigned char> > values;
    bool hasSolution;

    findSymbolicObjects(query.expr, objects);
    if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
        return false;
    assert(hasSolution && "state has invalid constraint set");

    Assignment a(objects, values);
    result = a.evaluate(query.expr);

    return true;
}


bool Z3BaseSolverImpl::computeInitialValues(const Query &query,
                                        const std::vector<const Array*> &objects,
                                        std::vector<std::vector<unsigned char> > &values,
                                        bool &hasSolution) {
    ++stats::queries;
    ++stats::queryCounterexamples;

    preCheck(query);
    bool result = check(query, objects, values, hasSolution);
    postCheck(query);

    if (hasSolution) {
        ++stats::queriesInvalid;
    } else {
        ++stats::queriesValid;
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////


Z3IncrementalSolverImpl::Z3IncrementalSolverImpl()
    : Z3BaseSolverImpl(),
      last_constraints_(new ConditionNodeList()) {

}

Z3IncrementalSolverImpl::~Z3IncrementalSolverImpl() {

}


void Z3IncrementalSolverImpl::preCheck(const Query &query) {
    errs() << "====> Query size: " << query.constraints.size() << '\n';

    ConditionNodeList *cur_constraints = new ConditionNodeList();

    for (ConditionNodeRef node = query.constraints.head(),
            root = query.constraints.root();
            node != root; node = node->parent()) {
        // TODO: Handle special case of fast-forward
        cur_constraints->push_front(node);
    }

    ConditionNodeList::iterator cur_it, last_it;
    cur_it = cur_constraints->begin();
    last_it = last_constraints_->begin();

    while (cur_it != cur_constraints->end() &&
            last_it != last_constraints_->end() &&
            *cur_it == *last_it) {
        cur_it++;
        last_it++;
    }

    if (last_it != last_constraints_->end()) {
        unsigned amount = 1 + last_constraints_->back()->depth()
                - (*last_it)->depth();
        errs() << "====> POP x" << amount << '\n';
        solver_.pop(amount);
    }

    if (cur_it != cur_constraints->end()) {
        errs() << "====> PUSH x"
                << (cur_constraints->back()->depth() - (*cur_it)->depth() + 1)
                << '\n';

        while (cur_it != cur_constraints->end()) {
            solver_.push();
            solver_.add(builder_->construct((*cur_it)->expr()));
            cur_it++;
        }
    }

    last_constraints_.reset(cur_constraints);

    solver_.push();
}


void Z3IncrementalSolverImpl::postCheck(const Query&) {
    solver_.pop();
}

////////////////////////////////////////////////////////////////////////////////


Z3SolverImpl::Z3SolverImpl() : Z3BaseSolverImpl() {

}


Z3SolverImpl::~Z3SolverImpl() {

}


void Z3SolverImpl::preCheck(const Query &query) {
    errs() << "====> Query size: " << query.constraints.size() << '\n';

    std::list<ConditionNodeRef> cur_constraints;

    for (ConditionNodeRef node = query.constraints.head(),
            root = query.constraints.root();
            node != root; node = node->parent()) {
        cur_constraints.push_front(node);
    }

    for (std::list<ConditionNodeRef>::iterator it = cur_constraints.begin(),
            ie = cur_constraints.end(); it != ie; ++it) {
        solver_.add(builder_->construct((*it)->expr()));
    }
}


void Z3SolverImpl::postCheck(const Query&) {
    solver_.reset();
    resetBuilder();
}


}
