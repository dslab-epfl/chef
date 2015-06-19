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

extern "C" {
#include "config.h"
#include "qemu-common.h"
#include "cpu.h"
}

#include "S2EEventLogger.h"
#include "S2EExecutionState.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/CommandLine.h>


namespace {
    llvm::cl::opt<bool>
    CollectEventStacks("collect-event-stacks",
            llvm::cl::init(true));

    llvm::cl::opt<int>
    CollectEventMaxStackDepth("collect-event-max-stack-depth",
            llvm::cl::init(32));
}


using namespace klee;

namespace s2e {


static const char *callstacks_init_sql =
        "CREATE TABLE IF NOT EXISTS callstacks ("
        "id INTEGER PRIMARY KEY NOT NULL,"
        "state_id INTEGER NOT NULL,"
        "sec_state_id INTEGER,"
        "pc INTEGER NOT NULL,"
        "callstack BLOB,"
        "callstack_decoded TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS debug_info ("
        "pc INTEGER PRIMARY KEY NOT NULL,"
        "module_name TEXT,"
        "fn_name TEXT,"
        "file_name TEXT,"
        "line_number INTEGER"
        ");";

static const char *callstack_insert_sql =
        "INSERT INTO callstacks"
        "(id, state_id, sec_state_id, pc, callstack)"
        "VALUES"
        "(?1, ?2, ?3, ?4, ?5);";

static const char *debug_insert_sql =
        "INSERT OR IGNORE INTO debug_info (pc) VALUES(?1);";


////////////////////////////////////////////////////////////////////////////////


S2EEventLogger::S2EEventLogger(sqlite3 *db)
    : EventLogger(db) {
    int result;
    char *err_msg;

    result = sqlite3_exec(db_, callstacks_init_sql, NULL, NULL, &err_msg);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(db_, callstack_insert_sql, -1,
            &callstack_insert_stmt_, NULL);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(db_, debug_insert_sql, -1,
            &debug_insert_stmt_, NULL);
    assert(result == SQLITE_OK);

    if (CollectEventStacks) {
        callstack_ = new uint64_t[CollectEventMaxStackDepth];
    }
}


S2EEventLogger::~S2EEventLogger() {
    sqlite3_finalize(callstack_insert_stmt_);
    sqlite3_finalize(debug_insert_stmt_);
    delete [] callstack_;
}


uint64_t S2EEventLogger::logEvent(klee::ExecutionState *state,
        unsigned event, uint64_t count) {
    return logStateEvent(state, NULL, event, count);
}


uint64_t S2EEventLogger::logStateEvent(klee::ExecutionState *state,
        klee::ExecutionState *other, unsigned event, uint64_t count) {
    uint64_t event_id = EventLogger::logEvent(state, event, count);
    S2EExecutionState *s2e_state = static_cast<S2EExecutionState*>(state);

    sqlite3_bind_int64(callstack_insert_stmt_, 1, event_id);
    sqlite3_bind_int(callstack_insert_stmt_, 2, s2e_state->getID());

    if (other) {
        sqlite3_bind_int(callstack_insert_stmt_, 3,
                static_cast<S2EExecutionState*>(other)->getID());
    } else {
        sqlite3_bind_null(callstack_insert_stmt_, 3);
    }

    sqlite3_bind_int64(callstack_insert_stmt_, 4, s2e_state->getPc());

    int stack_size;
    if (CollectEventStacks) {
        extractCallStack(s2e_state, stack_size);
        sqlite3_bind_blob(callstack_insert_stmt_, 5, callstack_,
                stack_size * sizeof(callstack_[0]), NULL);
    } else {
        sqlite3_bind_null(callstack_insert_stmt_, 5);
    }

    int result = sqlite3_step(callstack_insert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(callstack_insert_stmt_);

    // TODO: Do this for the callstack, too, in the future...
    sqlite3_bind_int64(debug_insert_stmt_, 1, s2e_state->getPc());
    result = sqlite3_step(debug_insert_stmt_);
    assert(result == SQLITE_DONE);
    sqlite3_reset(debug_insert_stmt_);

    return event_id;
}


void S2EEventLogger::extractCallStack(S2EExecutionState *state,
        int &stack_size) {
#ifdef TARGET_I386
    stack_size = 0;
    target_long frame_pointer;

    callstack_[stack_size++] = state->getPc();

    if (!state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EBP]),
            &frame_pointer, sizeof(frame_pointer))) {
        return;
    }

    // XXX: Hack, hack, hack: Handle the case where EBP was pushed
    // on the stack by a concrete syscall.
    if (!frame_pointer) {
        ref<Expr> head = state->readMemory(state->getSp(), Expr::Int32,
                S2EExecutionState::VirtualAddress);
        if (ConstantExpr *ce = dyn_cast<ConstantExpr>(head)) {
            frame_pointer = ce->getZExtValue();
        } else {
            return;
        }
    }

    while (frame_pointer && stack_size < CollectEventMaxStackDepth) {
        ref<Expr> next_expr = state->readMemory(frame_pointer, Expr::Int32,
                S2EExecutionState::VirtualAddress);
        ref<Expr> retaddr_expr = state->readMemory(frame_pointer + sizeof(target_ulong),
                Expr::Int32, S2EExecutionState::VirtualAddress);

        if (retaddr_expr.isNull())
            return;

        if (ConstantExpr *ce = dyn_cast<ConstantExpr>(retaddr_expr)) {
            callstack_[stack_size++] = ce->getZExtValue();
        } else {
            return;
        }

        if (next_expr.isNull())
            return;

        if (ConstantExpr *ce = dyn_cast<ConstantExpr>(next_expr)) {
            frame_pointer = ce->getZExtValue();
        } else {
            return;
        }
    }
#else
    stack_size = 0;
#endif
}


} /* namespace s2e */
