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

#ifndef EVENTLOGGER_H_
#define EVENTLOGGER_H_

#include <sqlite3.h>
#include <stdint.h>

namespace klee {

class ExecutionState;

enum {
    EVENT_KLEE_MEMORY_OP = 100,
    EVENT_KLEE_FORK = 101,
    EVENT_KLEE_MERGE = 103,
    EVENT_KLEE_FAILED_MERGE = 104,
    EVENT_KLEE_STATE_LEAVE = 105,
    EVENT_KLEE_STATE_RESUME = 106,
    EVENT_KLEE_TRACE = 107,
    EVENT_KLEE_STATE_KILLED = 108,
    EVENT_KLEE_QUERY = 109
};

class EventLogger {
public:
    EventLogger(sqlite3 *db);
    virtual ~EventLogger();

    virtual uint64_t logEvent(ExecutionState *state, unsigned event,
            uint64_t count);

    virtual uint64_t logStateEvent(ExecutionState *state,
            ExecutionState *other, unsigned event, uint64_t count);

    sqlite3 *database() const {
        return db_;
    }

protected:
    sqlite3 *db_;

private:
    sqlite3_stmt *event_insert_stmt_;
};

} /* namespace klee */

#endif /* EVENTLOGGER_H_ */
