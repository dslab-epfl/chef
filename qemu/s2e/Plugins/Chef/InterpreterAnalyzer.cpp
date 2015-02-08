/*
 * InterpreterAnalyzer.cpp
 *
 *  Created on: Feb 8, 2015
 *      Author: stefan
 */

#include "InterpreterAnalyzer.h"

#include <s2e/S2E.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/Chef/CallTracer.h>
#include <s2e/ExecutionStream.h>
#include <s2e/Plugins/CorePlugin.h>

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
    os_tracer_.reset(new OSTracer(*s2e(), *s2e()->getCorePlugin()));
    os_tracer_->onThreadCreate.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onThreadCreate));
}

void InterpreterAnalyzer::onThreadCreate(S2EExecutionState *state,
            boost::shared_ptr<OSThread> thread) {
    if (thread->name() == "ls") {
        call_tracer_.reset(new CallTracer(*s2e(), *s2e()->getCorePlugin(),
                *os_tracer_, thread));
    }
}

} /* namespace plugins */

} /* namespace s2e */
