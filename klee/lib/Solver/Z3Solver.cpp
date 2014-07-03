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

#include "llvm/Support/CommandLine.h"

#include "Z3Builder.h"

#include <z3.h>

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
    void printAssertions();

    void push();
    void pop(unsigned n);
    void reset();

    void prepareContext(const Query&);

    Z3_config config_;
    Z3_context context_;
    Z3_solver solver_;

    Z3Builder *builder_;

    ConditionNodeList *last_constraints_;
};


////////////////////////////////////////////////////////////////////////////////


Z3Solver::Z3Solver() : Solver(new Z3SolverImpl()) {

}

////////////////////////////////////////////////////////////////////////////////

namespace {

void error_handler(Z3_context context, Z3_error_code code) {
    Z3_string message = Z3_get_error_msg_ex(context, code);
    printf("Z3 error (%d): %s\n", code, message);
    ::exit(-1);
}

}


Z3SolverImpl::Z3SolverImpl() {

    config_ = Z3_mk_config();

    context_ = Z3_mk_context(config_);
    Z3_set_error_handler(context_, &error_handler);

    solver_ = Z3_mk_solver(context_); // TODO: Make sure this is the right method
    Z3_solver_inc_ref(context_, solver_);

    configureSolver();

    builder_ = new Z3Builder(context_, solver_, ConsConstantArrays);
    last_constraints_ = new ConditionNodeList();
}


Z3SolverImpl::~Z3SolverImpl() {
    delete builder_;
    delete last_constraints_;

    Z3_solver_dec_ref(context_, solver_);
    Z3_del_context(context_);
    Z3_del_config(config_);
}


void Z3SolverImpl::configureSolver() {
    // TODO: Turn this into some sort of optional logging...
    std::cerr << "Initializing Z3..." << '\n';

    Z3_param_descrs solver_params = Z3_solver_get_param_descrs(context_, solver_);
    Z3_param_descrs_inc_ref(context_, solver_params);

    Z3_params params = Z3_mk_params(context_);
    Z3_params_inc_ref(context_, params);

    std::cerr << "Available parameters:" << '\n';
    std::cerr << "  " << Z3_param_descrs_to_string(context_, solver_params) << '\n';

    Z3_params_set_bool(context_, params,
            Z3_mk_string_symbol(context_, "array.extensional"),
            Z3_FALSE);

    Z3_params_validate(context_, params, solver_params);
    Z3_solver_set_params(context_, solver_, params);

    Z3_param_descrs_dec_ref(context_, solver_params);
    Z3_params_dec_ref(context_, params);
}


void Z3SolverImpl::printAssertions() {
    Z3_ast_vector assertions = Z3_solver_get_assertions(context_, solver_);
    Z3_ast_vector_inc_ref(context_, assertions);

    std::cerr << "Assertions:" << '\n';
    std::cerr << Z3_ast_vector_to_string(context_, assertions) << '\n';

    Z3_ast_vector_dec_ref(context_, assertions);
}


void Z3SolverImpl::push() {
    builder_->push();
    Z3_solver_push(context_, solver_);
}


void Z3SolverImpl::pop(unsigned n) {
    builder_->pop(n);
    Z3_solver_pop(context_, solver_, n);
}


void Z3SolverImpl::reset() {
    builder_->reset();
    Z3_solver_reset(context_, solver_);
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
    std::cerr << "====> Query size: " << query.constraints.size() << '\n';

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
        std::cerr << "====> POP x" << amount << '\n';
        pop(amount);
    }

    if (cur_it != cur_constraints->end()) {
        std::cerr << "====> PUSH x"
                << (cur_constraints->back()->depth() - (*cur_it)->depth() + 1)
                << '\n';

        while (cur_it != cur_constraints->end()) {
            push();
            Z3_solver_assert(context_, solver_,
                    builder_->construct((*cur_it)->expr()));
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

    push();

    // Note that we're checking for validity (or a counterexample)
    Z3_solver_assert(context_, solver_,
            Z3_mk_not(context_, builder_->construct(query.expr)));

    //printAssertions();
    Z3_lbool result = Z3_solver_check(context_, solver_);

    if (result == Z3_L_TRUE) {
        Z3_model model = Z3_solver_get_model(context_, solver_);
        Z3_model_inc_ref(context_, model);

        values.reserve(objects.size());
        for (std::vector<const Array*>::const_iterator it = objects.begin(),
                ie = objects.end(); it != ie; ++it) {
            const Array *array = *it;
            std::vector<unsigned char> data;

            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; ++offset) {
                Z3_ast value_ast;
                unsigned value_num;

                Z3_bool eval_result = Z3_model_eval(context_, model,
                        builder_->getInitialRead(array, offset),
                        Z3_TRUE, &value_ast);
                assert(eval_result == Z3_TRUE && "Could not evaluate model");

                Z3_bool conv_result = Z3_get_numeral_uint(context_, value_ast,
                        &value_num);
                assert(conv_result == Z3_TRUE && "Could not convert value");
                assert(value_num < (1 << 8*sizeof(unsigned char))
                        && "Invalid model value");

                data.push_back((unsigned char)value_num);
            }
            values.push_back(data);
        }

        Z3_model_dec_ref(context_, model);
        hasSolution = true;
    } else {
        hasSolution = false;
    }

    pop(1);

    if (hasSolution) {
        ++stats::queriesInvalid;
    } else {
        ++stats::queriesValid;
    }

    return true;
}

}
