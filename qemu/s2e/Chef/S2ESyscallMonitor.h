/*
 * S2ESystemCallMonitor.h
 *
 *  Created on: Feb 12, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_CHEF_S2ESYSCALLMONITOR_H_
#define QEMU_S2E_CHEF_S2ESYSCALLMONITOR_H_


#include <s2e/Signals/Signals.h>

#include <vector>
#include <stdint.h>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace s2e {


class S2E;
class S2EExecutionState;
class ExecutionStream;


class S2ESyscallMonitor;


class S2ESyscallRange : public boost::enable_shared_from_this<S2ESyscallRange> {
public:
    uint64_t lower_bound() { return lower_bound_; }
    uint64_t upper_bound() { return upper_bound_; }

    bool registered() {
        return registered_;
    }

    void deregister();

    sigc::signal<void, S2EExecutionState*,
        uint64_t, uint64_t, uint64_t> onS2ESystemCall;
private:
    S2ESyscallRange(boost::shared_ptr<S2ESyscallMonitor> monitor,
            uint64_t lower, uint64_t upper);

    boost::weak_ptr<S2ESyscallMonitor> monitor_;
    uint64_t lower_bound_;
    uint64_t upper_bound_;
    bool registered_;

    S2ESyscallRange(const S2ESyscallRange &);
    void operator=(const S2ESyscallRange &);

    friend class S2ESyscallMonitor;
};


class S2ESyscallMonitor : public boost::enable_shared_from_this<S2ESyscallMonitor> {
public:
    S2ESyscallMonitor(S2E &s2e, ExecutionStream &stream);
    ~S2ESyscallMonitor();

    ExecutionStream &stream() {
        return stream_;
    }

    boost::shared_ptr<S2ESyscallRange> registerForRange(uint64_t lower,
            uint64_t upper);

    void deregister(boost::shared_ptr<S2ESyscallRange> range);


private:
    void onCustomInstruction(S2EExecutionState *state, uint64_t arg);

    S2E &s2e_;
    ExecutionStream &stream_;
    sigc::connection on_custom_instruction_;

    typedef std::vector<boost::shared_ptr<S2ESyscallRange> > RangeSet;
    RangeSet range_set_;

    S2ESyscallMonitor(const S2ESyscallMonitor &);
    void operator=(const S2ESyscallMonitor &);
};

} /* namespace s2e */

#endif /* QEMU_S2E_CHEF_S2ESYSCALLMONITOR_H_ */
