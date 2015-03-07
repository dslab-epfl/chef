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

#include "InterpreterAnalyzer.h"

#include <s2e/S2E.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/Chef/ExecutionStream.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/Opcodes.h>
#include <s2e/Chef/S2ESyscallMonitor.h>
#include <s2e/Chef/InterpreterDetector.h>
#include <s2e/Chef/HighLevelExecutor.h>

#include <llvm/Support/Format.h>

using boost::shared_ptr;

namespace s2e {

namespace plugins {


S2E_DEFINE_PLUGIN(InterpreterAnalyzer,
        "Analyze the structure of an interpreter binary.",
        "");


InterpreterAnalyzer::InterpreterAnalyzer(S2E *s2e)
    : Plugin(s2e),
      tracked_tid_(0) {

}


InterpreterAnalyzer::~InterpreterAnalyzer() {

}


void InterpreterAnalyzer::initialize() {
    smonitor_.reset(new S2ESyscallMonitor(*s2e(), *s2e()->getCorePlugin()));
    os_tracer_.reset(new OSTracer(*s2e(), *s2e()->getCorePlugin(), smonitor_));

    os_tracer_->onThreadCreate.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onThreadCreate));
    os_tracer_->onThreadExit.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onThreadExit));
}


void InterpreterAnalyzer::onThreadCreate(S2EExecutionState *state,
            OSThread *thread) {
    // FIXME: We both agree this is horrible...
    if (thread->name() != "python" &&
        thread->name() != "phantomjs" &&
        thread->name() != "js24" &&
        thread->name() != "lua") {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread created ("
            << thread->name() << ").  Started tracking..." << '\n';

    tracked_tid_ = thread->tid();
    interp_detector_.reset(new InterpreterDetector(*os_tracer_, tracked_tid_, smonitor_));
    high_level_executor_.reset(new HighLevelExecutor(*interp_detector_));

    high_level_executor_->onHighLevelStateStep.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateStep));
}


void InterpreterAnalyzer::onThreadExit(S2EExecutionState *state,
        OSThread *thread) {
    if (thread->tid() != tracked_tid_) {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread exited ("
            << thread->name() << ")." << '\n';

    tracked_tid_ = 0;
    high_level_executor_.reset();
    interp_detector_.reset();
}


void InterpreterAnalyzer::onHighLevelStateStep(S2EExecutionState *state,
        HighLevelState *hl_state) {
#if 0
    s2e()->getMessagesStream(state) << "HL state step. HLPC="
            << hl_state->segment->hlpc << '\n';
#endif
}


} /* namespace plugins */

} /* namespace s2e */
