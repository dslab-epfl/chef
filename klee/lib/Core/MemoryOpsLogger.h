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

#ifndef MEMORYOPSLOGGER_H_
#define MEMORYOPSLOGGER_H_

#include "klee/Expr.h"

#include <llvm/Support/TimeValue.h>

#include <sqlite3.h>
#include <stdint.h>

namespace klee {

class EventLogger;
class TimingSolver;
class ExecutionState;

class MemoryOpsLogger {
public:
    MemoryOpsLogger(EventLogger &event_logger, TimingSolver &solver);
    ~MemoryOpsLogger();

    uint64_t logConcreteMemoryOperation(ExecutionState &state,
            bool isWrite, uint64_t address, unsigned width, ref<Expr> value);
    void beginSymbolicMemoryOperation(ExecutionState &state,
            bool isWrite, ref<Expr> address, unsigned width, ref<Expr> value);
    uint64_t endSymbolicMemoryOperation(ExecutionState &state);

private:
    EventLogger &event_logger_;
    TimingSolver &solver_;

    sqlite3_stmt *memops_insert_stmt_;

    llvm::sys::TimeValue sym_start_;

    void prepareMemoryOperationLog(ExecutionState &state, bool isWrite,
            unsigned width, ref<Expr> value);

    bool getValueRange(ExecutionState &state, ref<Expr> value,
            uint64_t &low, uint64_t &high, int &qcount);
};

} /* namespace klee */

#endif /* MEMORYOPSLOGGER_H_ */
