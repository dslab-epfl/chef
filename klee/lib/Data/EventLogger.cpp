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

#include "klee/data/EventLogger.h"
#include "klee/ExecutionState.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include <stdlib.h>
#include <execinfo.h>


namespace {
    llvm::cl::opt<bool>
    CollectHostBacktraces("collect-host-backtraces",
            llvm::cl::init(false));
}


namespace klee {

static const char *events_init_sql =
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY NOT NULL,"
        "event INTEGER NOT NULL,"
        "count INTEGER NOT NULL,"
        "host_backtrace BLOB,"
        "host_backtrace_decoded TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS debug_info_host ("
        "pc INTEGER PRIMARY KEY NOT NULL,"
        "module_name TEXT,"
        "fn_name TEXT,"
        "file_name TEXT,"
        "line_number INTEGER"
        ");";

static const char *event_insert_sql =
        "INSERT INTO events"
        "(event, count, host_backtrace)"
        "VALUES"
        "(?1, ?2, ?3);";

////////////////////////////////////////////////////////////////////////////////

EventLogger::EventLogger(sqlite3 *db)
    : db_(db),
      event_insert_stmt_(NULL) {
    char *err_msg;

    if (sqlite3_exec(db_, events_init_sql, NULL, NULL, &err_msg) != SQLITE_OK) {
        llvm::errs() << "Could not initialize event table ("
                << err_msg << ")" << '\n';
        sqlite3_free(err_msg);
        ::exit(1);
    }
    if (sqlite3_prepare_v2(db_, event_insert_sql, -1, &event_insert_stmt_, NULL) != SQLITE_OK) {
        llvm::errs() << "SQL error in " << event_insert_sql << " ["
                << sqlite3_errmsg(db_) << "]" << '\n';
        ::exit(1);
    }
}


EventLogger::~EventLogger() {
    sqlite3_finalize(event_insert_stmt_);
}


uint64_t EventLogger::logEvent(ExecutionState *state, unsigned event,
            uint64_t count) {
    void *host_callstack[32];

    sqlite3_bind_int(event_insert_stmt_, 1, event);
    sqlite3_bind_int64(event_insert_stmt_, 2, count);

    if (!CollectHostBacktraces) {
        sqlite3_bind_null(event_insert_stmt_, 3);
    } else {
        int btrace_size = backtrace(&host_callstack[0], 32);
        sqlite3_bind_blob(event_insert_stmt_, 3, &host_callstack[0],
                btrace_size * sizeof(void*), NULL);
    }

    int result = sqlite3_step(event_insert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(event_insert_stmt_);

    return sqlite3_last_insert_rowid(db_);
}


uint64_t EventLogger::logStateEvent(ExecutionState *state, ExecutionState *other,
            unsigned event, uint64_t count) {
    return logEvent(state, event, count);
}


} /* namespace klee */
