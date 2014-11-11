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

#include <z3++.h>

#include <list>
#include <iostream>


namespace {
    llvm::cl::opt<bool>
    ConsConstantArrays("z3-cons-constant-arrays",
                       llvm::cl::desc("Specify constant arrays as nested store() "
                               "expressions, as opposed to asserting their "
                               "values in the solver."),
                       llvm::cl::init(true));
}


namespace klee {


class Z3SolverImpl : public SolverImpl {
public:
    Z3SolverImpl();
    virtual ~Z3SolverImpl();

    bool computeTruth(const Query&, bool &isValid);
    bool computeValue(const Query&, ref<Expr> &result);
    bool computeInitialValues(const Query &query,
                              const std::vector<const Array*> &objects,
                              std::vector<std::vector<unsigned char> > &values,
                              bool &hasSolution);

private:
    typedef std::list<ConditionNodeRef> ConditionNodeList;

    void configureSolver();
    void prepareContext(const Query&);

    z3::context context_;
    z3::solver solver_;

    Z3Builder *builder_;

    ConditionNodeList *last_constraints_;
};


////////////////////////////////////////////////////////////////////////////////


Z3Solver::Z3Solver() : Solver(new Z3SolverImpl()) {

}

////////////////////////////////////////////////////////////////////////////////


Z3SolverImpl::Z3SolverImpl()
    : solver_(context_, "QF_BV") {

    configureSolver();

    /*if (ConsConstantArrays)
        builder_ = new Z3ArrayBuilder(context_);
    else
        builder_ = new Z3AssertArrayBuilder(solver_);*/
    builder_ = new Z3IteBuilder(context_);
    last_constraints_ = new ConditionNodeList();
}


Z3SolverImpl::~Z3SolverImpl() {
    delete builder_;
    delete last_constraints_;
}


void Z3SolverImpl::configureSolver() {
    // TODO: Turn this into some sort of optional logging...
    llvm::errs() << "[Z3] Initializing..." << '\n';

    Z3_param_descrs solver_params = Z3_solver_get_param_descrs(context_, solver_);
    Z3_param_descrs_inc_ref(context_, solver_params);

    llvm::errs() << "[Z3] Available parameters:" << '\n';
    llvm::errs() << "[Z3]  " << Z3_param_descrs_to_string(context_, solver_params) << '\n';

    z3::params params(context_);
    params.set("array.extensional", false);
    Z3_params_validate(context_, params, solver_params);

    solver_.set(params);

    Z3_param_descrs_dec_ref(context_, solver_params);
}


bool Z3SolverImpl::computeTruth(const Query &query, bool &isValid) {
    std::vector<const Array*> objects;
    std::vector<std::vector<unsigned char> > values;
    bool hasSolution;

    if (!computeInitialValues(query, objects, values, hasSolution))
        return false;

    isValid = !hasSolution;
    return true;
}

// TODO: Use model evaluation in Z3
bool Z3SolverImpl::computeValue(const Query &query, ref<Expr> &result) {
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


void Z3SolverImpl::prepareContext(const Query &query) {
    llvm::errs() << "====> Query size: " << query.constraints.size() << '\n';

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
        llvm::errs() << "====> POP x" << amount << '\n';
        solver_.pop(amount);
    }

    if (cur_it != cur_constraints->end()) {
        llvm::errs() << "====> PUSH x"
                << (cur_constraints->back()->depth() - (*cur_it)->depth() + 1)
                << '\n';

        while (cur_it != cur_constraints->end()) {
            solver_.push();
            solver_.add(builder_->construct((*cur_it)->expr()));
            cur_it++;
        }
    }

    delete last_constraints_;
    last_constraints_ = cur_constraints;
}


bool Z3SolverImpl::computeInitialValues(const Query &query,
                                        const std::vector<const Array*> &objects,
                                        std::vector<std::vector<unsigned char> > &values,
                                        bool &hasSolution) {
    ++stats::queries;
    ++stats::queryCounterexamples;

    prepareContext(query);

    solver_.push();

    // Note that we're checking for validity (or a counterexample)
    solver_.add(!builder_->construct(query.expr));

    z3::check_result result = solver_.check();

    if (result == z3::sat) {
        z3::model model = solver_.get_model();

        values.reserve(objects.size());
        for (std::vector<const Array*>::const_iterator it = objects.begin(),
                ie = objects.end(); it != ie; ++it) {
            const Array *array = *it;
            std::vector<unsigned char> data;

            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; ++offset) {
                z3::expr value_ast = model.eval(
                        builder_->getInitialRead(array, offset), true);
                unsigned value_num;

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
    } else {
        hasSolution = false;
    }

    solver_.pop();

    if (hasSolution) {
        ++stats::queriesInvalid;
    } else {
        ++stats::queriesValid;
    }

    return true;
}

}
