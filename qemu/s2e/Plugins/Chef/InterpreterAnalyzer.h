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

#ifndef QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_
#define QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_

#include <s2e/Plugin.h>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace s2e {

class OSTracer;
class OSThread;
class S2ESyscallMonitor;
class InterpreterDetector;

class HighLevelExecutor;
class HighLevelState;

namespace plugins {

class InterpreterAnalyzer : public Plugin {
    S2E_PLUGIN
public:
    InterpreterAnalyzer(S2E *s2e);
    virtual ~InterpreterAnalyzer();

    void initialize();
private:
    void onThreadCreate(S2EExecutionState *state, OSThread* thread);
    void onThreadExit(S2EExecutionState *state, OSThread* thread);

    void onHighLevelStateCreate(S2EExecutionState *state,
            HighLevelState *hl_state);
    void onHighLevelStateStep(S2EExecutionState *state,
            HighLevelState *hl_state);
    void onHighLevelStateKill(S2EExecutionState *state,
            HighLevelState *hl_state);
    void onHighLevelStateFork(S2EExecutionState *state,
            HighLevelState *hl_state,
            const std::vector<HighLevelState*> &forks);
    void onHighLevelStateSwitch(S2EExecutionState *state,
            HighLevelState *prev, HighLevelState *next);

    boost::shared_ptr<S2ESyscallMonitor> smonitor_;
    boost::scoped_ptr<OSTracer> os_tracer_;
    boost::scoped_ptr<InterpreterDetector> interp_detector_;
    boost::scoped_ptr<HighLevelExecutor> high_level_executor_;

    int tracked_tid_;

};

} /* namespace plugins */

} /* namespace s2e */

#endif /* QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_ */
