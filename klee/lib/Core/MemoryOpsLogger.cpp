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

#include "MemoryOpsLogger.h"
#include "TimingSolver.h"
#include "klee/data/EventLogger.h"

#include <llvm/Support/CommandLine.h>

using llvm::sys::TimeValue;

namespace {
    llvm::cl::opt<bool>
    CollectMemopsValues("collect-memops-values",
            llvm::cl::desc("Collect values in memory operations"),
            llvm::cl::init(false));

    llvm::cl::opt<bool>
    CollectMemopsRanges("collect-memops-ranges",
            llvm::cl::desc("Collect ranges of addresses / values in memory operations"),
            llvm::cl::init(false));
}

namespace klee {

static const char *memops_init_sql =
        "CREATE TABLE IF NOT EXISTS events_memops ("
        "id INTEGER PRIMARY KEY NOT NULL,"
        "is_write INTEGER NOT NULL,"
        "is_symbolic INTEGER NOT NULL,"
        "width INTEGER NOT NULL,"
        "start_range INTEGER,"
        "end_range INTEGER,"
        "resolutions INTEGER,"
        "start_value INTEGER,"
        "end_value INTEGER,"
        "time_usec INTEGER,"
        "range_time_usec INTEGER,"
        "resolve_time_usec INTEGER"
        ");";

static const char *memops_insert_sql =
        "INSERT INTO events_memops"
        "(id, is_write, is_symbolic, width, start_range, end_range, start_value, end_value, time_usec, range_time_usec)"
        "VALUES"
        "(?1, ?2,       ?3,          ?4,    ?5,          ?6,        ?7,          ?8,        ?9,        ?10);";

////////////////////////////////////////////////////////////////////////////////

MemoryOpsLogger::MemoryOpsLogger(EventLogger &event_logger,
        TimingSolver &solver)
    : event_logger_(event_logger),
      solver_(solver),
      sym_start_(TimeValue::ZeroTime) {
    char *err_msg;
    int result;

    result = sqlite3_exec(event_logger_.database(), memops_init_sql, NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);
    result = sqlite3_prepare_v2(event_logger_.database(), memops_insert_sql, -1,
            &memops_insert_stmt_, NULL);
    assert(result == SQLITE_OK);
}

MemoryOpsLogger::~MemoryOpsLogger() {
    sqlite3_finalize(memops_insert_stmt_);
}

void MemoryOpsLogger::prepareMemoryOperationLog(ExecutionState &state,
        bool isWrite, unsigned width, ref<Expr> value) {
    sqlite3_clear_bindings(memops_insert_stmt_);
    sqlite3_bind_int(memops_insert_stmt_, 2, isWrite ? 1 : 0);
    sqlite3_bind_int(memops_insert_stmt_, 4, width);

    if (!(CollectMemopsValues && isWrite))
        return;

    if (ConstantExpr *ce = dyn_cast<ConstantExpr>(value)) {
        sqlite3_bind_int64(memops_insert_stmt_, 7, ce->getZExtValue());
        sqlite3_bind_int64(memops_insert_stmt_, 8, ce->getZExtValue());
    } else {
        if (CollectMemopsRanges) {
            std::pair<ref<Expr>, ref<Expr> > range = solver_.getRange(state, value);
            sqlite3_bind_int64(memops_insert_stmt_, 7,
                    cast<ConstantExpr>(range.first)->getZExtValue());
            sqlite3_bind_int64(memops_insert_stmt_, 8,
                    cast<ConstantExpr>(range.second)->getZExtValue());
        }
    }
}

uint64_t MemoryOpsLogger::logConcreteMemoryOperation(ExecutionState &state,
            bool isWrite, uint64_t address, unsigned width, ref<Expr> value) {
    char *err_msg;
    int result;

    result = sqlite3_exec(event_logger_.database(), "SAVEPOINT memop", NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    uint64_t event_id = event_logger_.logEvent(&state, EVENT_KLEE_MEMORY_OP, 1);

    prepareMemoryOperationLog(state, isWrite, width, value);

    sqlite3_bind_int64(memops_insert_stmt_, 1, event_id);
    sqlite3_bind_int(memops_insert_stmt_, 3, 0);
    sqlite3_bind_int64(memops_insert_stmt_, 5, address);
    sqlite3_bind_int64(memops_insert_stmt_, 6, address);

    result = sqlite3_step(memops_insert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(memops_insert_stmt_);

    result = sqlite3_exec(event_logger_.database(), "RELEASE memop", NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    return event_id;
}

void MemoryOpsLogger::beginSymbolicMemoryOperation(ExecutionState &state,
            bool isWrite, ref<Expr> address, unsigned width, ref<Expr> value) {
    char *err_msg;
    int result;

    result = sqlite3_exec(event_logger_.database(), "SAVEPOINT memop", NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    prepareMemoryOperationLog(state, isWrite, width, value);
    sqlite3_bind_int(memops_insert_stmt_, 3, 1);

    if (CollectMemopsRanges) {
        TimeValue bounds_start = TimeValue::now();
        int range_qcounter;
        uint64_t low, high;
        bool range_result = getValueRange(state, address, low, high, range_qcounter);
        assert(range_result);
        TimeValue bounds_duration = TimeValue::now() - bounds_start;

        sqlite3_bind_int64(memops_insert_stmt_, 5, low);
        sqlite3_bind_int64(memops_insert_stmt_, 6, high);
        sqlite3_bind_int64(memops_insert_stmt_, 10, bounds_duration.usec());
    }

    sym_start_ = TimeValue::now();
}

uint64_t MemoryOpsLogger::endSymbolicMemoryOperation(ExecutionState &state) {
    char *err_msg;
    int result;

    uint64_t event_id = event_logger_.logEvent(&state, EVENT_KLEE_MEMORY_OP, 1);
    TimeValue duration = TimeValue::now() - sym_start_;

    sqlite3_bind_int64(memops_insert_stmt_, 1, event_id);
    sqlite3_bind_int64(memops_insert_stmt_, 9, duration.usec());

    result = sqlite3_step(memops_insert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(memops_insert_stmt_);

    result = sqlite3_exec(event_logger_.database(), "RELEASE memop", NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    return event_id;
}


bool MemoryOpsLogger::getValueRange(ExecutionState &state, ref<Expr> value,
        uint64_t &low, uint64_t &high, int &qcount) {
    Expr::Width width = value->getWidth();
    ref<klee::ConstantExpr> start_expr;
    qcount = 0;

    qcount++;
    if (!solver_.getValue(state, value, start_expr))
        return false;

    low = high = start_expr->getZExtValue();

    bool result;
    qcount++;
    if (!solver_.mustBeTrue(state, EqExpr::create(start_expr, value), result))
        return false;

    if (result) {
        return true;
    }

    // Determining coarse bounds
    uint64_t increment = 4;
    while (increment < (uint64_t)(-1) - high) {
        qcount++;
        if (!solver_.mustBeTrue(state, UleExpr::create(value,
                klee::ConstantExpr::create(high + increment, width)),
                result))
            return false;
        high += increment;
        if (result) {
            // We know for sure the end is between high and high + increment
            break;
        } else {
            increment *= 2;
        }
    }

    increment = 4;
    while (increment < low) {
        qcount++;
        if (!solver_.mustBeTrue(state,
                UleExpr::create(klee::ConstantExpr::create(low - increment, width), value),
                result))
            return false;
        low -= increment;
        if (result) {
            // We know for sure the end is between low - increment and low
            break;
        } else {
            increment *= 2;
        }
    }

    return true;
}

} /* namespace klee */
