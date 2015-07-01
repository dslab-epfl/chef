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
#include "klee/util/ExprSMTLIBPrinter.h"
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

#include <sstream>

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

cl::opt<bool> ComputeQueryStats("compute-query-stats",
        cl::desc("Compute query statistics (somewhat expensive)"),
        cl::init(false));

enum SMTLIBOutputMode {
    SMTLIB_OUT_NONE,
    SMTLIB_OUT_SINGLE_FILE,
    SMTLIB_OUT_SEPARATE_FILES
};

cl::opt<SMTLIBOutputMode> DumpSMTLIB("dump-smtlib",
        cl::desc("Dump the queries in SMTLIB format"),
        cl::values(
                clEnumValN(SMTLIB_OUT_NONE, "none", "No SMTLIB dumping"),
                clEnumValN(SMTLIB_OUT_SINGLE_FILE, "single", "Dump in single SMTLIB file"),
                clEnumValN(SMTLIB_OUT_SEPARATE_FILES, "separate", "Dump in one SMTLIB file per query"),
                clEnumValEnd),
        cl::init(SMTLIB_OUT_NONE));

cl::opt<std::string> DumpSMTLIBPath("dump-smtlib-path",
        cl::desc("Output path for SMT-Lib dumps"),
        cl::init(""));

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


// QueryListener ///////////////////////////////////////////////////////////////


class QueryListener {
public:
    virtual ~QueryListener() {}

    virtual void onQueryDecoded(const Query &query, int64_t qid,
            QueryType qtype, Solver::Validity rec_validity,
            int64_t rec_time_usec) = 0;
};


// QueryStatsRecorder //////////////////////////////////////////////////////////

class QueryStatsRecorder : public QueryListener {
public:
    QueryStatsRecorder(sqlite3 *db);
    virtual ~QueryStatsRecorder();

    virtual void onQueryDecoded(const Query &query, int64_t qid,
            QueryType qtype, Solver::Validity rec_validity,
            int64_t rec_time_usec);

private:
    sqlite3 *db_;
    sqlite3_stmt *insert_stmt_;
};


QueryStatsRecorder::QueryStatsRecorder(sqlite3 *db)
    : db_(db) {
    const char *init_sql =
        "DROP TABLE IF EXISTS query_stats;"
        "CREATE TABLE query_stats (\n"
        " query_id INTEGER PRIMARY KEY NOT NULL,\n"
        " arrays_refd       INTEGER,\n"
        " const_arrays_refd INTEGER,\n"
        " node_count        INTEGER,\n"
        " max_depth         INTEGER,\n"
        " sym_write_count   INTEGER,\n"
        " sym_read_count    INTEGER,\n"
        " select_count      INTEGER,\n"
        " multiplicity      INTEGER\n"
        ");";

    const char *insert_sql =
        "INSERT INTO query_stats "
        "(query_id, arrays_refd, const_arrays_refd, node_count, max_depth, sym_write_count, sym_read_count, select_count, multiplicity)"
        "VALUES"
        "(?1,       ?2,          ?3,               ?4,         ?5,        ?6,              ?7,             ?8,           ?9)";

    char *err_msg;
    int result = sqlite3_exec(db_, init_sql, NULL, NULL, &err_msg);
    if (result != SQLITE_OK) {
        errs() << "Could not execute SQL statement: " << init_sql
                << " (" << err_msg << ")\n";
        ::exit(1);
    }

    result = sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, NULL);
    if (result != SQLITE_OK) {
        errs() << "Could not prepare SQL statement: " << insert_sql
                << " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }
}


QueryStatsRecorder::~QueryStatsRecorder() {
    int result = sqlite3_finalize(insert_stmt_);
    assert(result == SQLITE_OK);
}


void QueryStatsRecorder::onQueryDecoded(const Query &query, int64_t qid,
        QueryType qtype, Solver::Validity rec_validity,
        int64_t rec_time_usec) {
    ArrayExprAnalyzer arr_analyzer;
    arr_analyzer.visit(query.expr);

    for (ConditionNodeRef node = query.constraints.head(),
            root = query.constraints.root(); node != root;
            node = node->parent()) {
        arr_analyzer.visit(node->expr());
    }

    sqlite3_clear_bindings(insert_stmt_);
    sqlite3_bind_int64(insert_stmt_, 1, qid);
    sqlite3_bind_int(insert_stmt_, 2, arr_analyzer.getArrayCount());
    sqlite3_bind_int(insert_stmt_, 3, arr_analyzer.getConstArrayCount());
    sqlite3_bind_int(insert_stmt_, 4, arr_analyzer.getTotalNodes());
    sqlite3_bind_int(insert_stmt_, 7, arr_analyzer.getTotalSymbolicReads());
    sqlite3_bind_int(insert_stmt_, 8, arr_analyzer.getTotalSelects());
    sqlite3_bind_int64(insert_stmt_, 9, GetQueryMultiplicity(query));

    int result = sqlite3_step(insert_stmt_);
    assert(result == SQLITE_DONE);

    sqlite3_reset(insert_stmt_);
}


// QueryReplayer ///////////////////////////////////////////////////////////////


class QueryReplayer : public QueryListener {
public:
    QueryReplayer();
    virtual ~QueryReplayer();

    virtual void onQueryDecoded(const Query &query, int64_t qid,
            QueryType qtype, Solver::Validity rec_validity,
            int64_t rec_time_usec);

private:
    Solver *solver_;

    TimeValue total_recorded_;
    TimeValue total_replayed_;
};


QueryReplayer::QueryReplayer()
    : total_recorded_(TimeValue::ZeroTime),
      total_replayed_(TimeValue::ZeroTime) {

    DefaultSolverFactory solver_factory(NULL);
    solver_ = solver_factory.createEndSolver();
    solver_ = solver_factory.decorateSolver(solver_);
}


QueryReplayer::~QueryReplayer() {
    // TODO: Destruct the solver chain...
}


void QueryReplayer::onQueryDecoded(const Query &query, int64_t qid,
        QueryType qtype, Solver::Validity rec_validity,
        int64_t rec_time_usec) {

    TimeValue start_replay = TimeValue::now();
    switch (qtype) {
    case TRUTH: {
       bool result = false;
       solver_->impl->computeTruth(query, result);
       assert((rec_validity == Solver::True) == result);
       break;
    }
    case VALIDITY: {
       Solver::Validity result = Solver::Unknown;
       solver_->impl->computeValidity(query, result);
       assert(rec_validity == result);
       break;
    }
    case VALUE: {
       ref<Expr> result;
       solver_->impl->computeValue(query, result);
       break;
    }
    case INITIAL_VALUES: {
       std::vector<const Array*> objects;
       std::vector<std::vector<unsigned char> > result;
       bool hasSolution;
       solver_->impl->computeInitialValues(query, objects, result, hasSolution);
       break;
    }
    default:
       assert(0 && "Unreachable");
    }

    TimeValue replayed_duration = TimeValue::now() - start_replay;
    TimeValue recorded_duration = TimeValue(rec_time_usec / 1000000L,
           (rec_time_usec % 1000000L) * TimeValue::NANOSECONDS_PER_MICROSECOND);

    total_recorded_ += recorded_duration;
    total_replayed_ += replayed_duration;

    outs() << "[Replay]"
           << " Recorded: " << total_recorded_.usec()
           << " Replayed: " << total_replayed_.usec()
           << " Speedup: " << format("%.1fx", float(total_recorded_.usec()) / float(total_replayed_.usec()))
           << '\n';
}


// QueryDumper /////////////////////////////////////////////////////////////////


class QueryDumper : public QueryListener {
public:
    QueryDumper();
    virtual ~QueryDumper();

    virtual void onQueryDecoded(const Query &query, int64_t qid,
        QueryType qtype, Solver::Validity rec_validity,
        int64_t rec_time_usec);

private:
    ExprSMTLIBPrinter printer_;
};


QueryDumper::QueryDumper() {
    printer_.setConstantDisplayMode(ExprSMTLIBPrinter::DECIMAL);
    printer_.setLogic(ExprSMTLIBPrinter::QF_ABV);
}

QueryDumper::~QueryDumper() {

}


void QueryDumper::onQueryDecoded(const Query &query, int64_t qid,
        QueryType qtype, Solver::Validity rec_validity,
        int64_t rec_time_usec) {
    std::stringstream ss;

    printer_.setOutput(ss);
    printer_.setQuery(query);
    printer_.generateOutput();

    ss.flush();
    outs() << "[Print]"
           << " Size: " << ss.str().size() << " bytes"
           << '\n';
}


// QueryDecoder ////////////////////////////////////////////////////////////////


class QueryDecoder {
public:
    QueryDecoder(sqlite3 *db);
    ~QueryDecoder();

    void addQueryListener(QueryListener *listener) {
        query_listeners_.push_back(listener);
    }

    int64_t getQueryCount();
    void decodeQueries();

private:
    typedef std::vector<QueryListener*> QueryListenerSet;

    sqlite3 *db_;
    sqlite3_stmt *select_stmt_;

    QueryListenerSet query_listeners_;

    TimeValue total_recorded_;
};


QueryDecoder::QueryDecoder(sqlite3 *db)
    : db_(db),
      total_recorded_(TimeValue::ZeroTime) {

    const char *select_sql =
        "SELECT q.id, q.type, q.body, r.validity, r.time_usec "
        "FROM queries AS q, query_results AS r "
        "WHERE q.id = r.query_id "
        "ORDER BY q.id ASC";

    int result = sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt_, NULL);
    if (result != SQLITE_OK) {
        errs() << "Could not prepare SQL statement: " << select_sql
                << " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }
}


QueryDecoder::~QueryDecoder() {
    int result = sqlite3_finalize(select_stmt_);
    assert(result == SQLITE_OK);
}


int64_t QueryDecoder::getQueryCount() {
    int result;
    const char *sql = "SELECT COUNT(q.id) "
                      "FROM queries AS q, query_results as r "
                      "WHERE q.id = r.query_id";

    sqlite3_stmt *stmt;
    result = sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL);
    assert(result == SQLITE_OK);
    result = sqlite3_step(stmt);
    assert(result == SQLITE_ROW);

    int64_t qcount = sqlite3_column_int64(stmt, 0);

    result = sqlite3_finalize(stmt);
    assert(result == SQLITE_OK);

    return qcount;
}


void QueryDecoder::decodeQueries() {
    int result;

    scoped_ptr<ExprBuilder> expr_builder(createDefaultExprBuilder());
    ExprDeserializer expr_deserializer(*expr_builder, std::vector<Array*>());
    QueryDeserializer query_deserializer(expr_deserializer);

    while ((result = sqlite3_step(select_stmt_)) == SQLITE_ROW) {
        Query query;

        QueryType query_type = static_cast<QueryType>(
                sqlite3_column_int(select_stmt_, 1));

        std::string data_blob(
                (const char*)sqlite3_column_blob(select_stmt_, 2),
                sqlite3_column_bytes(select_stmt_, 2));

        bool deser_result = query_deserializer.Deserialize(data_blob, query);
        assert(deser_result && "Invalid query blob field");

        int64_t rec_time_usec = sqlite3_column_int64(select_stmt_, 4);
        TimeValue recorded_duration = TimeValue(rec_time_usec / 1000000L,
               (rec_time_usec % 1000000L) * TimeValue::NANOSECONDS_PER_MICROSECOND);
        total_recorded_ += recorded_duration;

        outs() << "[Decode " << format("%06d", sqlite3_column_int64(select_stmt_, 0)) << "]"
               << " Recorded: " << recorded_duration.usec()
               << " Total: " << total_recorded_.usec() << '\n';

        for (QueryListenerSet::iterator it = query_listeners_.begin(),
                ie = query_listeners_.end(); it != ie; ++it) {
            QueryListener *listener = *it;
            listener->onQueryDecoded(query,
                    sqlite3_column_int64(select_stmt_, 0),
                    query_type,
                    static_cast<Solver::Validity>(sqlite3_column_int(
                            select_stmt_, 3)),
                    rec_time_usec);
        }
    }

    assert(result == SQLITE_DONE);
}


// main ////////////////////////////////////////////////////////////////////////


static void decodeQueries(sqlite3 *db) {
    QueryDecoder decoder(db);
    scoped_ptr<QueryListener> stats_recorder;
    scoped_ptr<QueryListener> query_replayer;
    scoped_ptr<QueryListener> query_printer;

    if (ComputeQueryStats) {
        stats_recorder.reset(new QueryStatsRecorder(db));
        decoder.addQueryListener(stats_recorder.get());
    }

    if (ReplayQueries) {
        query_replayer.reset(new QueryReplayer());
        decoder.addQueryListener(query_replayer.get());
    }

    if (DumpSMTLIB != SMTLIB_OUT_NONE) {
        query_printer.reset(new QueryDumper());
        decoder.addQueryListener(query_printer.get());
    }

    outs() << "[Header] Decoding " << decoder.getQueryCount()
            << " queries" << '\n';

    decoder.decodeQueries();
}


int main(int argc, char **argv, char **envp) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    cl::ParseCommandLineOptions(argc, argv, "Query analysis");

    sqlite3 *db;

    if (sqlite3_open(InputFileName.c_str(), &db) != SQLITE_OK) {
        errs() << "Could not open SQLite DB: " << InputFileName <<
                " (" << sqlite3_errmsg(db) << ")" << '\n';
        ::exit(1);
    }

    decodeQueries(db);

#if 0
    char *smtlib_dump_dir, *smtlib_dump_file;
    if (DumpSMTLIB) {
        std::stringstream dump_path;
        if (DumpSMTLIBPath.length() == 0) {
            char *dbfilepath = strdup(InputFileName.c_str());
            dump_path << dirname(dbfilepath) << '/';
            free(dbfilepath);
        } else {
            dump_path << DumpSMTLIBPath << '/';
        }
        smtlib_dump_dir = strdup(dump_path.str().c_str());
        if (SMTLIBMonolithic) {
            dump_path << "dump.smt";
            smtlib_dump_file = strdup(dump_path.str().c_str());
            if (remove(smtlib_dump_file) != 0)
                std::cerr << "Warning: could not delete " << smtlib_dump_file << '\n';
        }
    }

    for (int i = 0; (result = sqlite3_step(select_stmt)) == SQLITE_ROW
            && SMTLIBDumpLimit > 0 && i < SMTLIBDumpLimit; ++i) {

        if (DumpSMTLIB) {
            int id = sqlite3_column_int64(select_stmt, 0);
            int64_t recorded_usec = sqlite3_column_int64(select_stmt, 4);

            std::ofstream file;
            if (SMTLIBMonolithic) {
                file.open(smtlib_dump_file, std::ios_base::out | std::ios_base::app);
                outs() << "   Appending to " << smtlib_dump_file << '\n';
            } else {
                std::stringstream dump_path;
                dump_path << smtlib_dump_dir << std::setfill('0') << std::setw(4) << id << ".smt";
                smtlib_dump_file = strdup(dump_path.str().c_str());
                file.open(smtlib_dump_file);
                outs() << "   Writing to " << smtlib_dump_file << '\n';
                free(smtlib_dump_file);
            }
            file << "; " << recorded_usec << " usec\n";

            printer.setOutput(file);
            printer.setQuery(query);
            printer.generateOutput();
            file.close();
        }
    }

    if (DumpSMTLIB && SMTLIBMonolithic)
        free(smtlib_dump_file);
#endif

    int result = sqlite3_close(db);
    assert(result == SQLITE_OK);

    return 0;
}
