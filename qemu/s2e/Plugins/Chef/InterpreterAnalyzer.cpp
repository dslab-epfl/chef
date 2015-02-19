/*
 * InterpreterAnalyzer.cpp
 *
 *  Created on: Feb 8, 2015
 *      Author: stefan
 */

#include "InterpreterAnalyzer.h"

#include <s2e/S2E.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/ExecutionStream.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/Opcodes.h>
#include <s2e/Chef/S2ESyscallMonitor.h>
#include <s2e/Chef/InterpreterDetector.h>

#include <llvm/Support/Format.h>

#include <algorithm>
#include <vector>
#include <map>

using boost::shared_ptr;

namespace s2e {

namespace plugins {


S2E_DEFINE_PLUGIN(InterpreterAnalyzer,
        "Analyze the structure of an interpreter binary.",
        "");


InterpreterAnalyzer::InterpreterAnalyzer(S2E *s2e) : Plugin(s2e) {

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
            boost::shared_ptr<OSThread> thread) {
    // FIXME: We both agree this is horrible...
    if (thread->name() != "python" && thread->name() != "phantomjs" && thread->name() != "js24" && thread->name() != "lua") {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread created ("
            << thread->name() << ").  Started tracking..." << '\n';

    tracked_thread_ = thread;
    interp_detector_.reset(new InterpreterDetector(*s2e(), *os_tracer_,
            tracked_thread_, smonitor_));
}


void InterpreterAnalyzer::onThreadExit(S2EExecutionState *state,
        boost::shared_ptr<OSThread> thread) {
    if (thread != tracked_thread_) {
        return;
    }

    s2e()->getMessagesStream(state) << "Interpreter thread exited ("
            << tracked_thread_->name() << ")." << '\n';

    tracked_thread_.reset();
    interp_detector_.reset();
}


} /* namespace plugins */

} /* namespace s2e */
