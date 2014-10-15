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
#include "klee/Constraints.h"
#include "klee/data/ExprSerializer.h"
#include "klee/data/QuerySerializer.h"
#include "klee/util/ExprVisitor.h"

#include "S2ESolvers.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TimeValue.h>
#include <llvm/Support/raw_ostream.h>

#include <boost/scoped_ptr.hpp>

#include <sqlite3.h>

#include <map>

using boost::scoped_ptr;
using llvm::sys::TimeValue;

namespace {
    llvm::cl::opt<bool>
    CollectQueryBody("collect-query-body", llvm::cl::init(true));

    llvm::cl::opt<bool>
    CollectQueryAnalysis("collect-query-analysis", llvm::cl::init(true));
}


namespace s2e {


using namespace klee;


// Analyzers ///////////////////////////////////////////////////////////////////


static uint64_t GetExprMultiplicity(const ref<Expr> expr) {
    switch (expr->getKind()) {
    case Expr::And: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        return GetExprMultiplicity(be->left) * GetExprMultiplicity(be->right);
    }
    case Expr::Or: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        return GetExprMultiplicity(be->left) + GetExprMultiplicity(be->right);
    }
    default:
        return 1;
    }
}


static uint64_t GetQueryMultiplicity(const Query &query) {
    ConditionNodeRef node = query.constraints.head();
    uint64_t multiplicity = 1;
    while (node != query.constraints.root()) {
        multiplicity *= GetExprMultiplicity(node->expr());
        node = node->parent();
    }
    return multiplicity;
}


////////////////////////////////////////////////////////////////////////////////

static const char *initialize_sql =
        "CREATE TABLE IF NOT EXISTS queries ("
        "id                INTEGER PRIMARY KEY NOT NULL,"
        "parent_id         INTEGER,"
        "depth             INTEGER NOT NULL,"
        "body              BLOB,"
        "type              INTEGER NOT NULL,"

        "arrays_refd       INTEGER,"
        "const_arrays_refd INTEGER,"
        "sym_reads         INTEGER,"
        "multiplicity      INTEGER,"

        "FOREIGN KEY(parent_id) REFERENCES queries(id)"
        ");"


        "CREATE TABLE IF NOT EXISTS query_results ("
        "id        INTEGER PRIMARY KEY NOT NULL,"
        "query_id  INTEGER NOT NULL,"
        "time_usec INTEGER,"
        "validity  INTEGER,"
        "FOREIGN KEY(query_id) REFERENCES queries(id)"
        ");";


static const char *qinsert_sql =
        "INSERT INTO queries"
        "(id, parent_id, depth, body, type, "
        "arrays_refd, const_arrays_refd, sym_reads, multiplicity)"
        "VALUES"
        "(?1,  ?2,  ?3,  ?4, ?5,"
        " ?10, ?11, ?12, ?13);";


static const char *rinsert_sql =
        "INSERT INTO query_results"
        "(query_id, time_usec, validity)"
        "VALUES"
        "(?1, ?2, ?3);";

////////////////////////////////////////////////////////////////////////////////


class DataCollectorSolver : public SolverImpl {
public:
    DataCollectorSolver(Solver *base_solver, sqlite3 *db);

    bool computeTruth(const Query &query, bool &isValid);
    bool computeValidity(const Query &query, Solver::Validity &validity);
    bool computeValue(const Query &query, ref<Expr> &value);
    bool computeInitialValues(const Query &query,
            const std::vector<const Array*> &objects,
            std::vector<std::vector<unsigned char> > &values,
            bool &hasSolution);

private:
    enum QueryType {
        TRUTH = 0,
        VALIDITY = 1,
        VALUE = 2,
        INITIAL_VALUES = 3
    };

    ExprSerializer es_;
    QuerySerializer serializer_;
    scoped_ptr<Solver> base_solver_;

    sqlite3 *db_;
    sqlite3_stmt *qinsert_stmt_;
    sqlite3_stmt *rinsert_stmt_;

    void LogQueryStats(const Query &query, QueryType type,
            TimeValue start, Solver::Validity validity);
    void BindQueryAnalyses(const Query &query);
};


DataCollectorSolver::DataCollectorSolver(Solver *base_solver,  sqlite3 *db)
        : serializer_(es_),
          base_solver_(base_solver),
          db_(db),
          qinsert_stmt_(0),
          rinsert_stmt_(0) {
    char *err_msg;
    int result;

    if (sqlite3_exec(db_, initialize_sql, NULL, NULL, &err_msg) != SQLITE_OK) {
        llvm::errs() << "Could not initialize solver tables ("
                << err_msg << ")" << '\n';
        sqlite3_free(err_msg);
        ::exit(1);
    }

    if (sqlite3_prepare_v2(db_, qinsert_sql, -1, &qinsert_stmt_, NULL)
            != SQLITE_OK) {
        llvm::errs() << "SQL error in " << qinsert_sql << " ["
                << sqlite3_errmsg(db_) << "]" << '\n';
        ::exit(1);
    }
    result = sqlite3_prepare_v2(db_, rinsert_sql, -1, &rinsert_stmt_, NULL);
    assert(result == SQLITE_OK);
}


void DataCollectorSolver::BindQueryAnalyses(const Query &query) {
#if 0
    ArrayExprAnalyzer arr_analyzer;
    arr_analyzer.visit(query.expr);
    for (ConditionNodeRef node = query.constraints.head(),
            root = query.constraints.root(); node != root;
            node = node->parent()) {
        arr_analyzer.visit(node->expr());
    }

    sqlite3_bind_int(qinsert_stmt_, 10, arr_analyzer.getArrayCount());
    sqlite3_bind_int(qinsert_stmt_, 11, arr_analyzer.getConstArrayCount());
    sqlite3_bind_int(qinsert_stmt_, 12, arr_analyzer.getTotalSymbolicReads());
#endif

    sqlite3_bind_int64(qinsert_stmt_, 13, GetQueryMultiplicity(query));
}


void DataCollectorSolver::LogQueryStats(const Query &query,
        QueryType type, TimeValue start, Solver::Validity validity) {
    int result;

    TimeValue duration = TimeValue::now() - start;
    std::string query_blob;
    std::pair<uint64_t, uint64_t> qids = serializer_.Serialize(query, query_blob);

    // Query structure
    sqlite3_clear_bindings(qinsert_stmt_);

    sqlite3_bind_int64(qinsert_stmt_, 1, qids.first);
    if (qids.second) {
        sqlite3_bind_int64(qinsert_stmt_, 2, qids.second);
    }
    sqlite3_bind_int(qinsert_stmt_, 3, query.constraints.head()->depth());
    sqlite3_bind_blob(qinsert_stmt_, 4,
            query_blob.c_str(), query_blob.size(), NULL);
    sqlite3_bind_int(qinsert_stmt_, 5, static_cast<int>(type));

    // Query analysis
    if (CollectQueryAnalysis) {
        BindQueryAnalyses(query);
    }

    result = sqlite3_step(qinsert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(qinsert_stmt_);

    // Query results
    sqlite3_bind_int64(rinsert_stmt_, 1, qids.first);
    sqlite3_bind_int64(rinsert_stmt_, 2, duration.usec());
    if (type == TRUTH || type == VALIDITY) {
        sqlite3_bind_int(rinsert_stmt_, 3, static_cast<int>(validity));
    }
    result = sqlite3_step(rinsert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(rinsert_stmt_);
}


bool DataCollectorSolver::computeTruth(const Query &query, bool &isValid) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeTruth(query, isValid);
    LogQueryStats(query, TRUTH, start, isValid ? Solver::True : Solver::False);
    return result;
}


bool DataCollectorSolver::computeValidity(const Query &query,
        Solver::Validity &validity) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValidity(query, validity);
    LogQueryStats(query, VALIDITY, start, validity);

    return result;
}


bool DataCollectorSolver::computeValue(const Query &query, ref<Expr> &value) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValue(query, value);
    LogQueryStats(query, VALUE, start, Solver::Unknown);

    return result;
}


bool DataCollectorSolver::computeInitialValues(const Query &query,
        const std::vector<const Array*> &objects,
        std::vector<std::vector<unsigned char> > &values,
        bool &hasSolution) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeInitialValues(query, objects,
            values, hasSolution);

    LogQueryStats(query, INITIAL_VALUES, start, Solver::Unknown);

    return result;
}


Solver *createDataCollectorSolver(Solver *s, sqlite3 *db) {
    return new Solver(new DataCollectorSolver(s, db));
}


} // namespace s2e
