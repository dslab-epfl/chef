/*
 * InterpreterDetector.cpp
 *
 *  Created on: Feb 17, 2015
 *      Author: stefan
 */

#include "InterpreterDetector.h"

#include <s2e/S2E.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/Chef/S2ESyscallMonitor.h>

#include <llvm/Support/Format.h>

#include <boost/shared_ptr.hpp>

#include <map>
#include <vector>
#include <set>

namespace s2e {


using boost::shared_ptr;


enum {
    S2E_CHEF_START = 0x1000,

    S2E_CHEF_CALIBRATE = S2E_CHEF_START,
    S2E_CHEF_CALIBRATE_END,

    S2E_CHEF_END
};


// MemoryOpRecorder ////////////////////////////////////////////////////////////


struct MemoryOp {
    uint64_t seq_no;
    uint64_t pc;
    uint64_t address;
    uint64_t value;
    uint8_t size;
    bool is_write;

    MemoryOp(uint64_t s, uint64_t p, uint64_t a, uint64_t v, uint8_t sz, bool w)
        : seq_no(s),
        pc(p),
        address(a),
        value(v),
        size(sz),
        is_write(w) {

    }
};

typedef std::vector<MemoryOp> MemoryOpSequence;


// TODO: This seems useful beyond interpreter analysis.  Move out publicly.
struct InterpreterDetector::MemoryOpRecorder {
    MemoryOpRecorder() : seq_counter(0) {

    }

    void recordMemoryOp(uint64_t pc, uint64_t address, uint64_t value,
            uint8_t size, bool is_write) {
        mem_ops.push_back(MemoryOp(seq_counter++, pc, address, value, size,
                is_write));
    }

    uint64_t seq_counter;
    MemoryOpSequence mem_ops;

private:
    MemoryOpRecorder(const MemoryOpRecorder&);
    void operator=(const MemoryOpRecorder&);
};


struct MemoryOpAnalyzer {
    MemoryOpAnalyzer(S2E &s2e, S2EExecutionState *state,
            const MemoryOpSequence &mem_ops, int min_opcodes)
        : instrumentation_pc(0),
          s2e_(s2e),
          state_(state),
          mem_ops_(mem_ops),
          min_opcodes_(min_opcodes) {

    }

    bool analyze() {
        if (!computeCandidateHLPC())
            return false;
        return computeInstrumentationPoint();
    }

    typedef std::vector<uint64_t> ValueSequence;
    typedef std::set<uint64_t> ValueSet;
    typedef std::map<uint64_t, shared_ptr<ValueSequence> > ValueSequenceMap;

    ValueSequenceMap candidate_hlpcs;
    ValueSet discarded_hlpcs;
    std::pair<uint64_t, uint64_t> bytecode_range;

    ValueSequenceMap candidate_pcs;
    ValueSet discarded_pcs;

    uint64_t instrumentation_pc;

private:
    bool computeCandidateHLPC() {
        for (MemoryOpSequence::const_iterator mit = mem_ops_.begin(),
                mie = mem_ops_.end(); mit != mie; ++mit) {
            if (!mit->is_write || mit->size != sizeof(target_ulong)) {
                continue;
            }
            if (discarded_hlpcs.count(mit->address) > 0) {
                continue;
            }

            ValueSequenceMap::iterator vsit = candidate_hlpcs.find(mit->address);
            if (vsit == candidate_hlpcs.end()) {
                vsit = candidate_hlpcs.insert(std::make_pair(mit->address,
                        shared_ptr<ValueSequence>(new ValueSequence()))).first;
            }

            if (!vsit->second->empty() && mit->value <= vsit->second->back()) {
                discarded_hlpcs.insert(mit->value);
                candidate_hlpcs.erase(vsit);
                continue;
            }

            vsit->second->push_back(mit->value);
        }

        if (candidate_hlpcs.empty()) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "No candidate HLPC variables detected." << '\n';
            return false;
        }

        unsigned max_update_count = 0;

        for (ValueSequenceMap::iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie; ++vsit) {
            if (vsit->second->size() < min_opcodes_) {
                continue;
            }
            if (vsit->second->size() > max_update_count) {
                max_update_count = vsit->second->size();
                bytecode_range = std::make_pair(vsit->second->front(),
                        vsit->second->back());
            }
        }

        if (max_update_count == 0) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "Not enough HLPC variable updates." << '\n';
            return false;
        }

        return true;
    }

    bool computeInstrumentationPoint() {
        for (MemoryOpSequence::const_iterator mit = mem_ops_.begin(),
                mie = mem_ops_.end(); mit != mie; ++mit) {
            if (mit->is_write) {
                continue;
            }
            if (discarded_pcs.count(mit->pc)) {
                continue;
            }
            if (mit->address < bytecode_range.first
                    || mit->address > bytecode_range.second) {
                continue;
            }

            ValueSequenceMap::iterator vsit = candidate_pcs.find(mit->pc);
            if (vsit == candidate_pcs.end()) {
                vsit = candidate_pcs.insert(std::make_pair(mit->pc,
                        shared_ptr<ValueSequence>(new ValueSequence()))).first;
            }

            if (!vsit->second->empty() && mit->address <= vsit->second->back()) {
                discarded_pcs.insert(mit->pc);
                candidate_pcs.erase(vsit);
                continue;
            }

            vsit->second->push_back(mit->address);
        }

        if (candidate_pcs.empty()) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "No candidate instrumentation points detected." << '\n';
            return false;
        }

        unsigned max_update_count = 0;

        for (ValueSequenceMap::iterator vsit = candidate_pcs.begin(),
                vsie = candidate_pcs.end(); vsit != vsie; ++vsit) {
            if (vsit->second->size() < min_opcodes_) {
                continue;
            }
            if (vsit->second->size() > max_update_count) {
                max_update_count = vsit->second->size();
                instrumentation_pc = vsit->first;
            }
        }

        if (max_update_count == 0) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "Not enough updates for instrumentation points." << '\n';
            return false;
        }

        s2e_.getMessagesStream(state_) << "Instrumentation point detected: "
                << llvm::format("0x%x", instrumentation_pc) << '\n';
        return true;
    }

    S2E &s2e_;
    S2EExecutionState *state_;

    const MemoryOpSequence &mem_ops_;
    int min_opcodes_;

private:
    MemoryOpAnalyzer(const MemoryOpAnalyzer&);
    void operator=(const MemoryOpAnalyzer&);
};

// InterpreterDetector /////////////////////////////////////////////////////////

InterpreterDetector::InterpreterDetector(S2E &s2e, OSTracer &os_tracer,
        boost::shared_ptr<OSThread> thread,
        boost::shared_ptr<S2ESyscallMonitor> syscall_monitor)
    : s2e_(s2e),
      os_tracer_(os_tracer),
      thread_(thread),
      calibrating_(false),
      min_opcode_count_(0) {

    syscall_range_ = syscall_monitor->registerForRange(
            S2E_CHEF_START, S2E_CHEF_END);
    syscall_range_->onS2ESystemCall.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onS2ESyscall));
}


InterpreterDetector::~InterpreterDetector() {
    syscall_range_->deregister();
}


void InterpreterDetector::onConcreteDataMemoryAccess(S2EExecutionState *state,
        uint64_t address, uint64_t value, uint8_t size, unsigned flags) {
    if (!thread_->running() || thread_->kernel_mode()) {
        return;
    }

    memory_recording_->recordMemoryOp(state->getPc(), address, value, size,
            flags & S2E_MEM_TRACE_FLAG_WRITE);
}


void InterpreterDetector::onS2ESyscall(S2EExecutionState *state,
        uint64_t syscall_id, uint64_t data, uint64_t size) {
    switch (syscall_id) {
    case S2E_CHEF_CALIBRATE:
        if (!calibrating_) {
            calibrating_ = true;

            s2e_.getMessagesStream(state)
                    << "Starting interpreter detector calibration." << '\n';

            memory_recording_.reset(new MemoryOpRecorder());
            on_concrete_data_memory_access_ = os_tracer_.stream().
                    onConcreteDataMemoryAccess.connect(
                            sigc::mem_fun(*this, &InterpreterDetector::onConcreteDataMemoryAccess));
            min_opcode_count_ = 0;
        } else {
            s2e_.getMessagesStream(state) << "Calibration checkpoint." << '\n';
            min_opcode_count_++;
        }
        break;
    case S2E_CHEF_CALIBRATE_END:
        assert(calibrating_ && "Calibration end attempted before start");
        s2e_.getMessagesStream(state) << "Calibration ended." << '\n';

        min_opcode_count_++;

        computeCalibration(state);

        memory_recording_.reset();
        on_concrete_data_memory_access_.disconnect();
        calibrating_ = false;
        break;
    default:
        assert(0 && "FIXME");
    }
}


void InterpreterDetector::computeCalibration(S2EExecutionState *state) {
    MemoryOpAnalyzer analyzer(s2e_, state, memory_recording_->mem_ops,
            min_opcode_count_);
    analyzer.analyze();
}


} /* namespace s2e */
