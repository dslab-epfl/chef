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
