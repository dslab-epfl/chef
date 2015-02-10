/*
 * CallGraphMonitor.h
 *
 *  Created on: Feb 9, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_CALLGRAPHMONITOR_H_
#define QEMU_S2E_CHEF_CALLGRAPHMONITOR_H_

#include <s2e/Signals/Signals.h>

#include <boost/shared_ptr.hpp>

#include <vector>
#include <stdint.h>

namespace s2e {

class CallStack;
struct CallStackFrame;

class CallGraphMonitor {
public:
    struct Function;

    struct Call {
        // The PC of the call instruction
        uint64_t call_site;
        boost::shared_ptr<Function> function;

        Call(uint64_t cs, boost::shared_ptr<Function> f)
            : call_site(cs),
              function(f) {}
    };

    struct Function {
        uint64_t address;
        std::vector<Call> calls_;

        Function(uint64_t a) : address(a) {}
    };

public:
    CallGraphMonitor(boost::shared_ptr<CallStack> call_stack);
    ~CallGraphMonitor();

    boost::shared_ptr<Function> root() {
        return root_;
    }

private:
    typedef std::vector<Function*> NodeStack;

    void onStackFramePush(CallStack*);
    void onStackFramePop(CallStack*);

    void pushFrame(const CallStackFrame &frame);
    void popFrame();

    boost::shared_ptr<Function> root_;
    NodeStack stack_;

    boost::shared_ptr<CallStack> call_stack_;

    sigc::connection on_stack_frame_push_;
    sigc::connection on_stack_frame_pop_;
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_CALLGRAPHMONITOR_H_ */
