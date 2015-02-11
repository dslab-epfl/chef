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
#include <s2e/Chef/CallGraphMonitor.h>
#include <s2e/ExecutionStream.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/Plugins/Opcodes.h>

#include <llvm/Support/Format.h>

#include <algorithm>
#include <vector>

namespace s2e {

namespace plugins {


S2E_DEFINE_PLUGIN(InterpreterAnalyzer,
        "Analyze the structure of an interpreter binary.",
        "");

enum {
    S2E_CHEF_CALIBRATE = 0x1000,
    S2E_CHEF_CALIBRATE_END = 0x1001
};


class CallCountAnalyzer {
public:
    struct Caller {
        uint64_t address;
        unsigned count;

        Caller(uint64_t a, unsigned c) : address(a), count(c) {}
    };

    typedef std::vector<Caller> CallerList;

private:
    struct caller_count_desc {
        bool operator()(const Caller &l, const Caller &r) {
            return l.count > r.count;
        }
    };

public:
    CallCountAnalyzer(CallGraphMonitor *cgm) : cgm_(cgm) {}

    void getTopCallers(CallerList &callers) {
        callers.clear();
        populateCallers(callers, cgm_->root().get());
        std::sort(callers.begin(), callers.end(), caller_count_desc());
    }
private:
    void populateCallers(CallerList &callers,
            CallGraphMonitor::Function *fn) {
        callers.push_back(Caller(fn->address, fn->calls_.size()));
        for (unsigned i = 0 ; i < fn->calls_.size(); ++i) {
            populateCallers(callers, fn->calls_[i].function.get());
        }
    }

    CallGraphMonitor *cgm_;
};


// InterpreterAnalyzer /////////////////////////////////////////////////////////


InterpreterAnalyzer::InterpreterAnalyzer(S2E *s2e) : Plugin(s2e) {

}


InterpreterAnalyzer::~InterpreterAnalyzer() {

}


void InterpreterAnalyzer::initialize() {
    os_tracer_.reset(new OSTracer(*s2e(), *s2e()->getCorePlugin()));

    os_tracer_->onThreadCreate.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onThreadCreate));
    os_tracer_->onThreadExit.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onThreadExit));
}


void InterpreterAnalyzer::onThreadCreate(S2EExecutionState *state,
            boost::shared_ptr<OSThread> thread) {
    if (thread->name() != "python") {
        return;
    }

    tracked_thread_ = thread;

    call_tracer_.reset(new CallTracer(*s2e(), *s2e()->getCorePlugin(),
            *os_tracer_, thread));
    call_graph_monitor_.reset(new CallGraphMonitor(call_tracer_->call_stack()));

    on_custom_instruction_ = s2e()->getCorePlugin()->onCustomInstruction.connect(
            sigc::mem_fun(*this, &InterpreterAnalyzer::onCustomInstruction));

}


void InterpreterAnalyzer::onThreadExit(S2EExecutionState *state,
        boost::shared_ptr<OSThread> thread) {
    if (thread != tracked_thread_) {
        return;
    }

    on_custom_instruction_.disconnect();

    CallCountAnalyzer cca(call_graph_monitor_.get());
    CallCountAnalyzer::CallerList cl;
    cca.getTopCallers(cl);

    for (unsigned i = 0; i < 10; ++i) {
        s2e()->getMessagesStream(state) << "Top Caller: "
                << llvm::format("0x%x: %d times", cl[i].address, cl[i].count)
                << '\n';
    }

}


void InterpreterAnalyzer::onCustomInstruction(S2EExecutionState *state,
        uint64_t arg) {
    if (!OPCODE_CHECK(arg, SYSCALL_OPCODE))
        return;

    // FIXME: Offer a customInstruction hook in the thread object.
    if (!tracked_thread_->running() || tracked_thread_->kernel_mode()) {
        return;
    }

    target_uint syscall_id = 0;

    if (!state->readCpuRegisterConcrete(CPU_OFFSET(regs[R_EAX]), &syscall_id, sizeof(syscall_id))) {
        s2e()->getWarningsStream(state) << "Could not read syscall parameters" << '\n';
        return;
    }

    switch (syscall_id) {
    case S2E_CHEF_CALIBRATE:
        s2e()->getMessagesStream(state) << "Calibration checkpoint." << '\n';
        break;
    case S2E_CHEF_CALIBRATE_END:
        s2e()->getMessagesStream(state) << "Calibration ended." << '\n';
        break;
    }
}

} /* namespace plugins */

} /* namespace s2e */
