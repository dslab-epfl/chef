/*
 * InterpreterAnalyzer.h
 *
 *  Created on: Feb 8, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_
#define QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_

#include <s2e/Plugin.h>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace s2e {

class OSTracer;
class OSThread;
class CallTracer;
class CallGraphMonitor;

namespace plugins {

class InterpreterAnalyzer : public Plugin {
    S2E_PLUGIN
public:
    InterpreterAnalyzer(S2E *s2e);
    virtual ~InterpreterAnalyzer();

    void initialize();
private:
    void onThreadCreate(S2EExecutionState *state,
            boost::shared_ptr<OSThread> thread);
    void onThreadExit(S2EExecutionState *state,
            boost::shared_ptr<OSThread> thread);

    boost::scoped_ptr<OSTracer> os_tracer_;
    boost::scoped_ptr<CallTracer> call_tracer_;
    boost::scoped_ptr<CallGraphMonitor> call_graph_monitor_;

    boost::shared_ptr<OSThread> tracked_thread_;
};

} /* namespace plugins */

} /* namespace s2e */

#endif /* QEMU_S2E_PLUGINS_CHEF_INTERPRETERANALYZER_H_ */
