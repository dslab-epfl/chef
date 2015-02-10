/*
 * CallGraphMonitor.cpp
 *
 *  Created on: Feb 9, 2015
 *      Author: stefan
 */

#include "CallGraphMonitor.h"

#include <s2e/Chef/CallTracer.h>

#include <boost/make_shared.hpp>

using boost::shared_ptr;
using boost::make_shared;

namespace s2e {

CallGraphMonitor::CallGraphMonitor(shared_ptr<CallStack> call_stack)
    : call_stack_(call_stack) {
    on_stack_frame_push_ = call_stack_->onStackFramePush.connect(
            sigc::mem_fun(*this, &CallGraphMonitor::onStackFramePush));
    on_stack_frame_pop_ = call_stack_->onStackFramePop.connect(
            sigc::mem_fun(*this, &CallGraphMonitor::onStackFramePop));

    root_ = make_shared<Function>(call_stack_->frame(0).function);
    stack_.push_back(root_.get());

    for (unsigned i = 1; i < call_stack_->size(); ++i) {
        pushFrame(call_stack_->frame(i));
    }
}


CallGraphMonitor::~CallGraphMonitor() {
    on_stack_frame_push_.disconnect();
    on_stack_frame_pop_.disconnect();
}


void CallGraphMonitor::onStackFramePush(CallStack*) {
    pushFrame(call_stack_->top());
}


void CallGraphMonitor::onStackFramePop(CallStack*) {
    popFrame();
}


void CallGraphMonitor::pushFrame(const CallStackFrame &frame) {
    shared_ptr<Function> function = make_shared<Function>(frame.function);
    stack_.back()->calls_.push_back(Call(frame.call_site, function));
    stack_.push_back(function.get());
}


void CallGraphMonitor::popFrame() {
    stack_.pop_back();
}

} /* namespace s2e */
