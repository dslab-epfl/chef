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

#ifndef QEMU_S2E_CHEF_LOWLEVELTOPOSTRATEGY_H_
#define QEMU_S2E_CHEF_LOWLEVELTOPOSTRATEGY_H_


#include <boost/shared_ptr.hpp>
#include <s2e/Signals/Signals.h>

namespace s2e {

class CallTracer;
class CallStack;
class CallStackFrame;
class HighLevelExecutor;


class LowLevelTopoStrategy {
public:
    LowLevelTopoStrategy(HighLevelExecutor &hl_executor);
    ~LowLevelTopoStrategy();

private:
    void onStackFramePush(CallStack *stack,
            boost::shared_ptr<CallStackFrame> old_top,
            boost::shared_ptr<CallStackFrame> new_top);

    void onStackFramePopping(CallStack *stack,
            boost::shared_ptr<CallStackFrame> old_top,
            boost::shared_ptr<CallStackFrame> new_top);

    void onBasicBlockEnter(CallStack *stack,
            boost::shared_ptr<CallStackFrame> top);

    HighLevelExecutor &hl_executor_;
    CallTracer &call_tracer_;

    sigc::connection on_stack_frame_push_;
    sigc::connection on_stack_frame_popping_;
    sigc::connection on_basic_block_enter_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_LOWLEVELTOPOSTRATEGY_H_ */
