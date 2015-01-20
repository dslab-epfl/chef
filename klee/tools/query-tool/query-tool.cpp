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


#include "klee/ExprBuilder.h"
#include "klee/data/ExprDeserializer.h"
#include "klee/data/QueryDeserializer.h"
#include "klee/data/ExprVisualizer.h"
#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/SolverFactory.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TimeValue.h>
#include <llvm/Support/Format.h>

#include <google/protobuf/stubs/common.h>

#include <boost/scoped_ptr.hpp>
#include <boost/circular_buffer.hpp>

#include <sqlite3.h>

#include <fstream>

using namespace llvm;
using namespace klee;
using boost::scoped_ptr;
using llvm::sys::TimeValue;


namespace {

cl::opt<std::string> InputFileName(cl::Positional,
    cl::desc("<input query log file>"),
    cl::Required);

cl::opt<unsigned> QueryCount("query-count",
        cl::desc("Number of queries to process"),
        cl::init(5));

cl::opt<bool> VisualizeQueries("visualize",
        cl::desc("Output query structure in Graphviz format"),
        cl::init(false));

cl::opt<bool> ReplayQueries("replay",
        cl::desc("Re-run the queries through a solver (expensive!)"),
        cl::init(false));

}


class ArrayExprAnalyzer : public ExprVisitor {
protected:
    virtual Action visitExpr(const Expr& e) {
        total_nodes_++;
        return Action::doChildren();
    }

    virtual Action visitRead(const ReadExpr &re) {
        for (const UpdateNode *un = re.updates.head; un; un = un->next) {
            visit(un->index);
            visit(un->value);
        }

        ArrayStatsMap::iterator it = array_stats.find(re.updates.root);
        if (it == array_stats.end()) {
            it = array_stats.insert(std::make_pair(re.updates.root,
                    make_shared<ArrayStats>())).first;
        }

        it->second->sym_reads[re.updates.getSize()]++;
        it->second->total_sym_reads++;
        total_sym_reads_++;
        return Action::doChildren();
    }

    virtual Action visitSelect(const SelectExpr &se) {
        total_select_++;
        return Action::doChildren();
    }

public:
    ArrayExprAnalyzer()
        : total_nodes_(0),
          total_sym_reads_(0),
          total_sym_writes_(0),
          total_select_(0) {

    }

    llvm::raw_ostream& printResults(llvm::raw_ostream &os) {
        for (ArrayStatsMap::iterator it = array_stats.begin(),
                ie = array_stats.end(); it != ie; ++it) {
            const Array *array = it->first;
            shared_ptr<ArrayStats> stats = it->second;

            os << "[" << array->name << "] "
                    << stats->total_sym_reads << " symbolic reads (";
            for (std::map<int, int>::iterator dit = stats->sym_reads.begin(),
                    die = stats->sym_reads.end(); dit != die; ++dit) {
                if (dit != stats->sym_reads.begin())
                    os << ' ';
                os << dit->first << ":" << dit->second;
            }
            os << "): ";

            if (array->isSymbolicArray()) {
                os << "SYMBOLIC ARRAY";
            } else {
                os << "[ ";
                for (unsigned i = 0; i < array->size; ++i) {
                    os << array->constantValues[i]->getAPValue() << ' ';
                }
                os << "]";
            }

            os << '\n';
        }

        return os;
    }

    int getArrayCount() const {
        return array_stats.size();
    }

    int getConstArrayCount() const {
        int counter = 0;
        for (ArrayStatsMap::const_iterator it = array_stats.begin(),
                ie = array_stats.end(); it != ie; ++it) {
            counter += it->first->isConstantArray() ? 1 : 0;
        }
        return counter;
    }

    int getTotalSymbolicReads() const {
        return total_sym_reads_;
    }

    int getTotalSelects() const {
        return total_select_;
    }

    int getTotalNodes() const {
        return total_nodes_;
    }

private:
    struct ArrayStats {
        int total_sym_reads;
        std::map<int, int> sym_reads;

        ArrayStats() : total_sym_reads(0) {}
    };

    int total_nodes_;
    int total_sym_reads_;
    int total_sym_writes_;
    int total_select_;

    typedef std::map<const Array*, shared_ptr<ArrayStats> > ArrayStatsMap;
    ArrayStatsMap array_stats;
};


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


enum QueryType {
    TRUTH = 0,
    VALIDITY = 1,
    VALUE = 2,
    INITIAL_VALUES = 3
};


int main(int argc, char **argv, char **envp) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    cl::ParseCommandLineOptions(argc, argv, "Query analysis");

    sqlite3 *db;
    int result;
    char *err_msg;
    if (sqlite3_open(InputFileName.c_str(), &db) != SQLITE_OK) {
        errs() << "Could not open SQLite DB: " << InputFileName <<
                " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }

    const char *init_sql =
        "DROP TABLE IF EXISTS query_stats;"
        "CREATE TABLE query_stats ("
        "query_id INTEGER PRIMARY KEY NOT NULL,"
        "arrays_refd       INTEGER,"
        "const_arrays_refd INTEGER,"
        "node_count        INTEGER,"
        "max_depth         INTEGER,"
        "sym_write_count   INTEGER,"
        "sym_read_count    INTEGER,"
        "select_count      INTEGER,"
        "multiplicity      INTEGER"
        ");";

    const char *select_sql =
        "SELECT q.id, q.type, q.body, r.validity, r.time_usec "
        "FROM queries AS q, query_results AS r "
        "WHERE q.id = r.query_id "
        "ORDER BY q.id ASC";

    const char *insert_sql =
            "INSERT INTO query_stats "
            "(query_id, arrays_refd, const_arrays_refd, node_count, max_depth, sym_write_count, sym_read_count, select_count, multiplicity)"
            "VALUES"
            "(?1,       ?2,          ?3,               ?4,         ?5,        ?6,              ?7,             ?8,           ?9)";

    result = sqlite3_exec(db, init_sql, NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    sqlite3_stmt *select_stmt;
    sqlite3_stmt *insert_stmt;
    result = sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, NULL);
    if (result != SQLITE_OK) {
        errs() << "Could not prepare SQL statement: " << select_sql
                << " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }
    result = sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL);
    if (result != SQLITE_OK) {
        errs() << "Could not prepare SQL statement: " << insert_sql
                << " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }

    scoped_ptr<ExprBuilder> expr_builder(createDefaultExprBuilder());
    ExprDeserializer expr_deserializer(*expr_builder, std::vector<Array*>());
    QueryDeserializer query_deserializer(expr_deserializer);

    // Solvers used for replay
    DefaultSolverFactory solver_factory(NULL);
    Solver *klee_solver = solver_factory.createEndSolver();
    klee_solver = solver_factory.decorateSolver(klee_solver);
    Z3Solver z3_solver(true);
    //STPSolver stp_solver(false);

    TimeValue total_recorded(TimeValue::ZeroTime);
    TimeValue total_replayed(TimeValue::ZeroTime);

    while ((result = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        Query query;

        QueryType query_type = static_cast<QueryType>(
                sqlite3_column_int(select_stmt, 1));

        std::string data_blob(
                (const char*)sqlite3_column_blob(select_stmt, 2),
                sqlite3_column_bytes(select_stmt, 2));

        bool deser_result = query_deserializer.Deserialize(data_blob, query);
        assert(deser_result && "Invalid query blob field");

        outs() << "Processing query " << sqlite3_column_int64(select_stmt, 0) << '\n';

        ArrayExprAnalyzer arr_analyzer;
        arr_analyzer.visit(query.expr);
        for (ConditionNodeRef node = query.constraints.head(),
                root = query.constraints.root(); node != root;
                node = node->parent()) {
            arr_analyzer.visit(node->expr());
        }

        sqlite3_clear_bindings(insert_stmt);
        sqlite3_bind_int64(insert_stmt, 1, sqlite3_column_int64(select_stmt, 0));
        sqlite3_bind_int(insert_stmt, 2, arr_analyzer.getArrayCount());
        sqlite3_bind_int(insert_stmt, 3, arr_analyzer.getConstArrayCount());
        sqlite3_bind_int(insert_stmt, 4, arr_analyzer.getTotalNodes());
        sqlite3_bind_int(insert_stmt, 7, arr_analyzer.getTotalSymbolicReads());
        sqlite3_bind_int(insert_stmt, 8, arr_analyzer.getTotalSelects());
        sqlite3_bind_int64(insert_stmt, 9, GetQueryMultiplicity(query));
        result = sqlite3_step(insert_stmt);
        assert(result == SQLITE_DONE);
        sqlite3_reset(insert_stmt);

        if (ReplayQueries) {
            TimeValue start_replay = TimeValue::now();
            switch (query_type) {
            case TRUTH: {
                bool result = false;
                z3_solver.impl->computeTruth(query, result);
                assert((sqlite3_column_int(select_stmt, 3) == Solver::True)
                        == result);
                break;
            }
            case VALIDITY: {
                Solver::Validity result = Solver::Unknown;
                z3_solver.impl->computeValidity(query, result);
                assert(static_cast<Solver::Validity>(sqlite3_column_int(select_stmt, 3)) == result);
                break;
            }
            case VALUE: {
                ref<Expr> result;
                z3_solver.impl->computeValue(query, result);
                break;
            }
            case INITIAL_VALUES: {
                std::vector<const Array*> objects;
                std::vector<std::vector<unsigned char> > result;
                bool hasSolution;
                z3_solver.impl->computeInitialValues(query, objects, result,
                        hasSolution);
                break;
            }
            default:
                assert(0 && "Unreachable");
            }

            TimeValue replayed_duration = TimeValue::now() - start_replay;
            int64_t recorded_usec = sqlite3_column_int64(select_stmt, 4);
            TimeValue recorded_duration = TimeValue(recorded_usec / 1000000L,
                    (recorded_usec % 1000000L) * TimeValue::NANOSECONDS_PER_MICROSECOND);

            total_recorded += recorded_duration;
            total_replayed += replayed_duration;

            outs() << "[Total/" << arr_analyzer.getTotalSymbolicReads() << "/" << arr_analyzer.getTotalSelects() << "]"
                    << " Recorded: " << total_recorded.usec()
                    << " Replayed: " << total_replayed.usec()
                    << " Speedup: " << format("%.1fx", float(total_recorded.usec()) / float(total_replayed.usec()))
                    << '\n';
        }
    }
    assert(result == SQLITE_DONE);

    result = sqlite3_finalize(select_stmt);
    assert(result == SQLITE_OK);
    result = sqlite3_finalize(insert_stmt);
    assert(result == SQLITE_OK);
    result = sqlite3_close(db);
    assert(result == SQLITE_OK);
    return 0;
}
