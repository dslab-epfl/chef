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

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include <google/protobuf/stubs/common.h>

#include <boost/scoped_ptr.hpp>
#include <boost/circular_buffer.hpp>

#include <sqlite3.h>

#include <fstream>

using namespace llvm;
using namespace klee;
using boost::scoped_ptr;


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
        cl::desc("Re-run the queries through a solver"),
        cl::init(false));

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
    if (sqlite3_open(InputFileName.c_str(), &db) != SQLITE_OK) {
        errs() << "Could not open SQLite DB: " << InputFileName <<
                " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }

    const char *select_sql =
        "SELECT q.id, q.type, q.body, r.validity "
        "FROM queries AS q, query_results AS r "
        "WHERE q.id = r.query_id "
        "ORDER BY q.id ASC";

    sqlite3_stmt *select_stmt;
    result = sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, NULL);
    if (result != SQLITE_OK) {
        errs() << "Could not prepare SQL statement: " << select_sql
                << " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }

    scoped_ptr<ExprBuilder> expr_builder(createDefaultExprBuilder());
    ExprDeserializer expr_deserializer(*expr_builder, std::vector<Array*>());
    QueryDeserializer query_deserializer(expr_deserializer);

    // For replay purposes
    Z3Solver solver;

    while ((result = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        Query query;

        QueryType query_type = static_cast<QueryType>(
                sqlite3_column_int(select_stmt, 1));

        std::string data_blob(
                (const char*)sqlite3_column_blob(select_stmt, 2),
                sqlite3_column_bytes(select_stmt, 2));

        bool deser_result = query_deserializer.Deserialize(data_blob, query);
        assert(deser_result && "Invalid query blob field");

        if (ReplayQueries) {
            outs() << "Replaying query " << sqlite3_column_int64(select_stmt, 0) << '\n';
            switch (query_type) {
            case TRUTH: {
                bool result = false;
                solver.mustBeTrue(query, result);
                assert((sqlite3_column_int(select_stmt, 3) == Solver::True)
                        == result);
                break;
            }
            case VALIDITY: {
                Solver::Validity result = Solver::Unknown;
                solver.evaluate(query, result);
                assert(static_cast<Solver::Validity>(sqlite3_column_int(select_stmt, 3)) == result);
                break;
            }
            case VALUE: {
                ref<ConstantExpr> result;
                solver.getValue(query, result);
                break;
            }
            case INITIAL_VALUES: {
                std::vector<const Array*> objects;
                std::vector<std::vector<unsigned char> > result;
                solver.getInitialValues(query, objects, result);
                break;
            }
            default:
                assert(0 && "Unreachable");
            }
        }
    }
    assert(result == SQLITE_DONE);

    result = sqlite3_finalize(select_stmt);
    assert(result == SQLITE_OK);
    result = sqlite3_close(db);
    assert(result == SQLITE_OK);

#if 0
    typedef boost::circular_buffer<Query> QueryBuffer;
    QueryBuffer last_queries(QueryCount);

    if (VisualizeQueries) {
        DefaultExprDotDecorator decorator;
        ExprVisualizer visualizer(outs(), decorator);

        visualizer.BeginDrawing();
#if 0
        decorator.HighlightExpr(query.expr, "query");
        int pc_counter = 0;
        for (ConditionNodeRef node = query.constraints.head(),
                root = query.constraints.root(); node != root;
                node = node->parent()) {
            std::string label;
            raw_string_ostream stream(label);
            stream << "PC[" << pc_counter++ << "]";
            decorator.HighlightExpr(node->expr(), stream.str());
        }
        visualizer.DrawExpr(query.expr);
        for (ConditionNodeRef node = query.constraints.head(),
                root = query.constraints.root(); node != root;
                node = node->parent()) {
            visualizer.DrawExpr(node->expr());
        }
#endif
        for (int i = 0; i < last_queries.size(); ++i) {
            std::string label;
            raw_string_ostream stream(label);
            stream << "Time: "; //<< last_results[i].time_usec() << " us";
            decorator.HighlightExpr(last_queries[i].expr, stream.str());
        }
        for (int i = 0; i < last_queries.size(); ++i) {
            visualizer.DrawExpr(last_queries[i].expr);
        }

        visualizer.EndDrawing();
    }
#endif

    return 0;
}
