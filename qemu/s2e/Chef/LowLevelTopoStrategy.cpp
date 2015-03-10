/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2015, Dependable Systems Laboratory, EPFL
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

#include "LowLevelTopoStrategy.h"

#include <s2e/Chef/CallTracer.h>
#include <s2e/Chef/HighLevelExecutor.h>
#include <s2e/Chef/InterpreterDetector.h>

#include <boost/shared_ptr.hpp>

using boost::shared_ptr;

namespace s2e {

// LowLevelTopoStrategy ////////////////////////////////////////////////////////

LowLevelTopoStrategy::LowLevelTopoStrategy(HighLevelExecutor &hl_executor)
    : hl_executor_(hl_executor),
      call_tracer_(hl_executor.detector().call_tracer()){
    on_stack_frame_push_ = call_tracer_.onStackFramePush.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onStackFramePush));
    on_stack_frame_popping_ = call_tracer_.onStackFramePopping.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onStackFramePopping));
    on_basic_block_enter_ = call_tracer_.onBasicBlockEnter.connect(
            sigc::mem_fun(*this, &LowLevelTopoStrategy::onBasicBlockEnter));
}


LowLevelTopoStrategy::~LowLevelTopoStrategy() {
    on_stack_frame_push_.disconnect();
    on_stack_frame_popping_.disconnect();
    on_basic_block_enter_.disconnect();
}


void LowLevelTopoStrategy::onStackFramePush(CallStack *stack,
        boost::shared_ptr<CallStackFrame> old_top,
        boost::shared_ptr<CallStackFrame> new_top) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
}


void LowLevelTopoStrategy::onStackFramePopping(CallStack *stack,
        boost::shared_ptr<CallStackFrame> old_top,
        boost::shared_ptr<CallStackFrame> new_top) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
}


void LowLevelTopoStrategy::onBasicBlockEnter(CallStack *stack,
        boost::shared_ptr<CallStackFrame> top) {
    shared_ptr<LowLevelState> state = hl_executor_.getState(stack->s2e_state());
}

} /* namespace s2e */
