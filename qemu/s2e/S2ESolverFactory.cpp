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

#include "S2ESolverFactory.h"
#include "S2ESolvers.h"
#include "s2e/S2E.h"
#include "s2e/Plugins/CorePlugin.h"

#include "klee/Interpreter.h"
#include "klee/Solver.h"
#include "klee/SolverImpl.h"

#include <boost/scoped_ptr.hpp>
#include <llvm/Support/TimeValue.h>


using namespace klee;
using boost::scoped_ptr;
using llvm::sys::TimeValue;


namespace s2e {


class S2ENotificationSolver : public SolverImpl {
public:
    S2ENotificationSolver(S2E *s2e, Solver *base_solver);

    bool computeTruth(const Query &query, bool &isValid);
    bool computeValidity(const Query &query, Solver::Validity &validity);
    bool computeValue(const Query &query, ref<Expr> &value);
    bool computeInitialValues(const Query &query,
            const std::vector<const Array*> &objects,
            std::vector<std::vector<unsigned char> > &values,
            bool &hasSolution);

private:
    void notify(const Query &query, TimeValue start);

    S2E *s2e_;
    scoped_ptr<Solver> base_solver_;
};


S2ENotificationSolver::S2ENotificationSolver(S2E *s2e, Solver *base_solver)
    : s2e_(s2e),
      base_solver_(base_solver) {

}


void S2ENotificationSolver::notify(const Query &query, TimeValue start) {
    s2e_->getCorePlugin()->onSolverQuery.emit(query, TimeValue::now() - start);
}


bool S2ENotificationSolver::computeTruth(const Query &query, bool &isValid) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeTruth(query, isValid);
    notify(query, start);
    return result;
}


bool S2ENotificationSolver::computeValidity(const Query &query,
        Solver::Validity &validity) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValidity(query, validity);
    notify(query, start);
    return result;
}


bool S2ENotificationSolver::computeValue(const Query &query, ref<Expr> &value) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValue(query, value);
    notify(query, start);
    return result;
}


bool S2ENotificationSolver::computeInitialValues(const Query &query,
        const std::vector<const Array*> &objects,
        std::vector<std::vector<unsigned char> > &values,
        bool &hasSolution) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeInitialValues(query, objects,
            values, hasSolution);
    notify(query, start);
    return result;
}


/*============================================================================*/


S2ESolverFactory::S2ESolverFactory(S2E *s2e, InterpreterHandler *ih)
    : DefaultSolverFactory(ih), s2e_(s2e) {

}


S2ESolverFactory::~S2ESolverFactory() {

}


Solver *S2ESolverFactory::decorateSolver(Solver *end_solver) {
    Solver *solver = DefaultSolverFactory::decorateSolver(end_solver);
    solver = createDataCollectorSolver(solver, s2e_);
    solver = createNotificationSolver(solver, s2e_);
    return solver;
}

/*============================================================================*/

Solver *createNotificationSolver(Solver *s, S2E *s2e) {
    return new Solver(new S2ENotificationSolver(s2e, s));
}


} /* namespace s2e */
