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
#include <s2e/ConfigFile.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/Opcodes.h>

#include <s2e/Selectors.h>

#include <s2e/Chef/OSTracer.h>
#include <s2e/Chef/CallTracer.h>
#include <s2e/Chef/ExecutionStream.h>
#include <s2e/Chef/S2ESyscallMonitor.h>
#include <s2e/Chef/InterpreterDetector.h>
#include <s2e/Chef/InterpreterTracer.h>
#include <s2e/Chef/HighLevelExecutor.h>
#include <s2e/Chef/HighLevelStrategy.h>
#include <s2e/Chef/LowLevelStrategy.h>
#include <s2e/Chef/LowLevelTopoStrategy.h>

#include <llvm/Support/Format.h>
#include <llvm/Support/TimeValue.h>

#include <inttypes.h>

using boost::shared_ptr;

namespace s2e {

namespace plugins {

static const char *valid_interpreters[] = {
        "python",
        "phantomjs",
        "js24",
        "lua"
};


class IAHighLevelStrategyFactory : public HighLevelStrategyFactory {
public:
    IAHighLevelStrategyFactory(const std::string &config) : config_(config) {

    }

    HighLevelStrategy *createStrategy() {
        if (config_ == "dfs") {
           return new SelectorStrategy<DFSSelector<HighLevelStrategy::StateRef> >();
       } else if (config_ == "bfs") {
           return new SelectorStrategy<BFSSelector<HighLevelStrategy::StateRef> >();
       } else {
           return NULL;
       }
    }

private:
    std::string config_;
};


class IALowLevelStrategyFactory : public LowLevelStrategyFactory {
public:
    IALowLevelStrategyFactory(const std::string &config) : config_(config) {

    }

    LowLevelStrategy *createStrategy(HighLevelExecutor &hl_executor) {
        if (config_ == "topo") {
            return new LowLevelTopoStrategy(hl_executor);
        } else if (config_ == "sprout") {
            return new LowLevelSproutStrategy(hl_executor);
        } else {
            return NULL;
        }
    }

private:
    std::string config_;
};

// InterpreterAnalyzer /////////////////////////////////////////////////////////


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


llvm::raw_ostream& InterpreterAnalyzer::getStream(const HighLevelState *hl_state) {
    llvm::raw_ostream &os = s2e()->getMessagesStream();
    if (hl_state) {
        llvm::sys::TimeValue curTime = llvm::sys::TimeValue::now();
        os << (curTime.seconds() - s2e()->getStartTime()) << ' ';
        os << llvm::format("<HLState %d @ 0x%x>", hl_state->id(),
                hl_state->segment->hlpc);
        os << ' ';
    }
    return os;
}


void InterpreterAnalyzer::onThreadCreate(S2EExecutionState *state,
            OSThread *thread) {
    // If there is no interpreter pinned, try to do it now
    if (selected_interpreter_.empty()) {
        for (unsigned i = 0; i < sizeof(valid_interpreters)/sizeof(valid_interpreters[0]); ++i) {
            if (thread->name() == valid_interpreters[i]) {
                selected_interpreter_ = thread->name();
                s2e()->getMessagesStream(state) << "Locked on interpreter: "
                        << selected_interpreter_ << '\n';
                break;
            }
        }
    }

    if (thread->name() != selected_interpreter_) {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread created ("
            << thread->name() << ").  Started tracking..." << '\n';

    tracked_tid_ = thread->tid();
    call_tracer_.reset(new CallTracer(*os_tracer_, tracked_tid_));
    interp_tracer_.reset(new InterpreterTracer(*call_tracer_));
    interp_tracer_->onHighLevelInstructionFetch.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelInstructionFetch));

    if (interp_params_) {
        s2e()->getMessagesStream(state) << "Reusing interpreter structure..." << '\n';
        interp_tracer_->setInterpreterStructureParams(state, *interp_params_);
    } else {
        s2e()->getMessagesStream(state) << "Interpreter structure unknown. "
                << "Registering detector..." << '\n';
        interp_detector_.reset(new InterpreterDetector(*call_tracer_, smonitor_));
        interp_detector_->onInterpreterStructureDetected.connect(
                sigc::mem_fun(*this, &InterpreterAnalyzer::onInterpreterStructureDetected));
    }

    IALowLevelStrategyFactory ll_factory(s2e()->getConfig()->getString(
            getConfigKey() + ".llstrategy", "topo"));
    IAHighLevelStrategyFactory hl_factory(s2e()->getConfig()->getString(
            getConfigKey() + ".hlstrategy", "dfs"));

    high_level_executor_.reset(new HighLevelExecutor(*interp_tracer_,
            hl_factory, ll_factory));

    high_level_executor_->onHighLevelStateCreate.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateCreate));
    high_level_executor_->onHighLevelStateStep.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateStep));
    high_level_executor_->onHighLevelStateKill.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateKill));
    high_level_executor_->onHighLevelStateFork.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateFork));
    high_level_executor_->onHighLevelStateSwitch.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onHighLevelStateSwitch));
}


void InterpreterAnalyzer::onThreadExit(S2EExecutionState *state,
        OSThread *thread) {
    if (thread->tid() != tracked_tid_) {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread exited ("
            << thread->name() << ")." << '\n';

    std::string opcode_stats_str;
    llvm::raw_string_ostream sos(opcode_stats_str);

    for (OpcodeStatsMap::iterator it = opcode_stats_.begin(),
            ie = opcode_stats_.end(); it != ie; ++it) {
        sos << llvm::format("[%d]:%" PRIu64, it->first, it->second) << ' ';
    }

    s2e()->getMessagesStream(state) << "OPCODE STATS: " << sos.str() << '\n';

    tracked_tid_ = 0;

    high_level_executor_.reset();
    strategy_.reset();

    interp_detector_.reset();
    interp_tracer_.reset();
    call_tracer_.reset();
}


void InterpreterAnalyzer::onInterpreterStructureDetected(S2EExecutionState *state,
        int tracked_tid, const InterpreterStructureParams *params) {
    interp_params_.reset(new InterpreterStructureParams(*params));

    s2e()->getMessagesStream(state) << "Interpreter structure detected:" << '\n'
            << "Interpretation function: "
            << llvm::format("0x%x", interp_params_->interp_loop_function) << '\n'
            << "HLPC update point: "
            << llvm::format("0x%x", interp_params_->hlpc_update_pc) << '\n'
            << "Instruction fetch point: "
            << llvm::format("0x%x", interp_params_->instruction_fetch_pc) << '\n';

    interp_tracer_->setInterpreterStructureParams(state, *interp_params_);
}


void InterpreterAnalyzer::onHighLevelInstructionFetch(S2EExecutionState *state,
        HighLevelStack *hl_stack) {
    uint64_t hlpc = hl_stack->top()->hlpc;
    InterpreterInstruction instruction(hlpc);
    SpiderMonkeySemantics semantics;

    if (!semantics.decodeInstruction(state, hlpc, instruction)) {
        s2e()->getWarningsStream(state)
                << "Could not decode instruction at HLPC "
                << llvm::format("0x%x", hlpc) << '\n';
        return;
    }
    opcode_stats_[instruction.opcode] += 1;
}


void InterpreterAnalyzer::onHighLevelStateCreate(HighLevelState *hl_state) {
#if 1
    getStream(hl_state) << "State created." << '\n';
#endif
}


void InterpreterAnalyzer::onHighLevelStateStep(HighLevelState *hl_state) {
#if 1
    getStream(hl_state) << "State step." << '\n';
#endif
}


void InterpreterAnalyzer::onHighLevelStateKill(HighLevelState *hl_state) {
#if 1
    getStream(hl_state) << "State killed." << '\n';
#endif
}


void InterpreterAnalyzer::onHighLevelStateFork(HighLevelState *hl_state,
        const std::vector<HighLevelState*> &forks) {
#if 1
    for (std::vector<HighLevelState*>::const_iterator it = forks.begin(),
            ie = forks.end(); it != ie; ++it) {
        if (*it == hl_state)
            continue;
        getStream(hl_state) << "State " << (*it)->id() << " forked at "
                << llvm::format("0x%x", (*it)->segment->hlpc) << '\n';
    }
#endif
}


void InterpreterAnalyzer::onHighLevelStateSwitch(HighLevelState *prev,
        HighLevelState *next) {
#if 1
    s2e()->getMessagesStream() << "HL state switch" << '\n';
#endif
}


} /* namespace plugins */

} /* namespace s2e */
