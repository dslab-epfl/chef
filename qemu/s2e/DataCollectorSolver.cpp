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

#include "s2e/S2E.h"
#include "s2e/S2EExecutor.h"
#include "s2e/S2EExecutionState.h"

#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/Constraints.h"
#include "klee/data/EventLogger.h"
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
}


namespace s2e {


using namespace klee;

////////////////////////////////////////////////////////////////////////////////

static const char *initialize_sql =
        "CREATE TABLE IF NOT EXISTS queries ("
        "id                INTEGER PRIMARY KEY NOT NULL,"
        "parent_id         INTEGER,"
        "event_id          INTEGER NOT NULL,"
        "depth             INTEGER NOT NULL,"
        "body              BLOB,"
        "type              INTEGER NOT NULL,"
        "FOREIGN KEY(parent_id) REFERENCES queries(id),"
        "FOREIGN KEY(event_id) REFERENCES events(id)"
        ");"

        "CREATE TABLE IF NOT EXISTS query_results ("
        "query_id  INTEGER NOT NULL,"
        "label     TEXT NOT NULL,"
        "time_usec INTEGER,"
        "validity  INTEGER,"
        "PRIMARY KEY (query_id, label)"
        "FOREIGN KEY (query_id) REFERENCES queries(id)"
        ");";


static const char *qinsert_sql =
        "INSERT INTO queries"
        "(id, parent_id, event_id, depth, body, type)"
        "VALUES"
        "(?1, ?2,        ?14,      ?3,    ?4,   ?5);";


static const char *rinsert_sql =
        "INSERT INTO query_results"
        "(query_id, time_usec, validity, label)"
        "VALUES"
        "(?1, ?2, ?3, 'recorded');";

////////////////////////////////////////////////////////////////////////////////


class DataCollectorSolver : public SolverImpl {
public:
    DataCollectorSolver(Solver *base_solver, EventLogger *event_logger);

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

    void logQueryStats(const Query &query, QueryType type,
            TimeValue start, Solver::Validity validity);

    ExprSerializer es_;
    QuerySerializer serializer_;
    scoped_ptr<Solver> base_solver_;

    EventLogger *event_logger_;
    sqlite3_stmt *qinsert_stmt_;
    sqlite3_stmt *rinsert_stmt_;
};


DataCollectorSolver::DataCollectorSolver(Solver *base_solver, EventLogger *event_logger)
        : serializer_(es_),
          base_solver_(base_solver),
          event_logger_(event_logger),
          qinsert_stmt_(0),
          rinsert_stmt_(0) {
    char *err_msg;
    int result;

    if (sqlite3_exec(event_logger_->database(), initialize_sql, NULL, NULL, &err_msg) != SQLITE_OK) {
        llvm::errs() << "Could not initialize solver tables ("
                << err_msg << ")" << '\n';
        sqlite3_free(err_msg);
        ::exit(1);
    }

    if (sqlite3_prepare_v2(event_logger_->database(), qinsert_sql, -1, &qinsert_stmt_, NULL)
            != SQLITE_OK) {
        llvm::errs() << "SQL error in " << qinsert_sql << " ["
                << sqlite3_errmsg(event_logger_->database()) << "]" << '\n';
        ::exit(1);
    }
    result = sqlite3_prepare_v2(event_logger_->database(), rinsert_sql, -1, &rinsert_stmt_, NULL);
    assert(result == SQLITE_OK);
}


void DataCollectorSolver::logQueryStats(const Query &query,
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
    sqlite3_bind_int64(qinsert_stmt_, 14,
            event_logger_->logEvent(g_s2e_state, EVENT_KLEE_QUERY, 1));

    sqlite3_bind_int(qinsert_stmt_, 3, query.constraints.head()->depth());
    sqlite3_bind_blob(qinsert_stmt_, 4,
            query_blob.c_str(), query_blob.size(), NULL);
    sqlite3_bind_int(qinsert_stmt_, 5, static_cast<int>(type));

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
    logQueryStats(query, TRUTH, start, isValid ? Solver::True : Solver::False);
    return result;
}


bool DataCollectorSolver::computeValidity(const Query &query,
        Solver::Validity &validity) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValidity(query, validity);
    logQueryStats(query, VALIDITY, start, validity);

    return result;
}


bool DataCollectorSolver::computeValue(const Query &query, ref<Expr> &value) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeValue(query, value);
    logQueryStats(query, VALUE, start, Solver::Unknown);

    return result;
}


bool DataCollectorSolver::computeInitialValues(const Query &query,
        const std::vector<const Array*> &objects,
        std::vector<std::vector<unsigned char> > &values,
        bool &hasSolution) {
    TimeValue start = TimeValue::now();
    bool result = base_solver_->impl->computeInitialValues(query, objects,
            values, hasSolution);

    logQueryStats(query, INITIAL_VALUES, start, Solver::Unknown);

    return result;
}


Solver *createDataCollectorSolver(Solver *s, S2E* s2e) {
    return new Solver(new DataCollectorSolver(s, s2e->getEventLogger()));
}


} // namespace s2e
