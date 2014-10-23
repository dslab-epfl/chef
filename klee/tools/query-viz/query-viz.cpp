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
#include <sqlite3.h>
#include <boost/scoped_ptr.hpp>


using namespace llvm;
using namespace klee;
using boost::scoped_ptr;

namespace {

cl::opt<std::string> DatabaseFileName(cl::Positional,
        cl::desc("<collected data file>"),
        cl::Required);

cl::list<unsigned> QueryIDs(cl::Positional,
        cl::desc("<query ids>"),
        cl::OneOrMore);

}

enum QueryType {
    TRUTH = 0,
    VALIDITY = 1,
    VALUE = 2,
    INITIAL_VALUES = 3
};


int main(int argc, char **argv, char **envp) {
    sqlite3 *db;
    int result;

    GOOGLE_PROTOBUF_VERIFY_VERSION;
    cl::ParseCommandLineOptions(argc, argv, "Query visualization");

    std::set<uint64_t> target_query_ids;
    for (unsigned i = 0; i < QueryIDs.size(); ++i) {
        target_query_ids.insert(QueryIDs[i]);
    }
    std::vector<Query> collected_queries;
    std::vector<uint64_t> collected_query_ids;

    result = sqlite3_open(DatabaseFileName.c_str(), &db);
    assert(result == SQLITE_OK);

    const char *select_sql =
            "SELECT q.id, q.type, q.body FROM queries AS q ORDER BY q.id ASC";
    sqlite3_stmt *select_stmt;
    result = sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, NULL);
    assert(result == SQLITE_OK);

    scoped_ptr<ExprBuilder> expr_builder(createDefaultExprBuilder());
    ExprDeserializer expr_deserializer(*expr_builder, std::vector<Array*>());
    QueryDeserializer query_deserializer(expr_deserializer);

    while ((result = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        Query query;
        uint64_t query_id = sqlite3_column_int64(select_stmt, 0);
        QueryType query_type = static_cast<QueryType>(sqlite3_column_int(select_stmt, 1));

        std::string data_blob(
                (const char*)sqlite3_column_blob(select_stmt, 2),
                sqlite3_column_bytes(select_stmt, 2));

        bool deser_result = query_deserializer.Deserialize(data_blob, query);
        assert(deser_result && "Invalid query blob field");

        if (target_query_ids.erase(query_id) > 0) {
            collected_queries.push_back(query);
            collected_query_ids.push_back(query_id);
            if (target_query_ids.empty()) {
                break;
            }
        }
    }

    if (!target_query_ids.empty()) {
        assert(result == SQLITE_DONE);
        for (std::set<uint64_t>::iterator it = target_query_ids.begin(),
                ie = target_query_ids.end(); it != ie; ++it) {
            llvm::errs() << "Could not find query with ID "
                    << *it << " (ignoring)" << '\n';
        }
    }

    DefaultExprDotDecorator decorator;
    ExprVisualizer visualizer(llvm::outs(), decorator);

    visualizer.BeginDrawing();
    for (unsigned i = 0; i < collected_queries.size(); ++i) {
        std::string label;
        raw_string_ostream stream(label);
        stream << "QID: " << collected_query_ids[i];
        decorator.HighlightExpr(collected_queries[i].expr, stream.str());
    }
    for (unsigned i = 0; i < collected_queries.size(); ++i) {
        visualizer.DrawExpr(collected_queries[i].expr);
    }
    visualizer.EndDrawing();

    result = sqlite3_finalize(select_stmt);
    assert(result == SQLITE_OK);
    result = sqlite3_close(db);
    assert(result == SQLITE_OK);

    return 0;
}
