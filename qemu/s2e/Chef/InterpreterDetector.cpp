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
#include <s2e/Chef/CallTracer.h>

#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

#include <boost/shared_ptr.hpp>

#include <map>
#include <vector>
#include <set>

namespace s2e {


using boost::shared_ptr;


enum {
    S2E_CHEF_START = 0x1000,

    S2E_CHEF_CALIBRATE_START = S2E_CHEF_START,
    S2E_CHEF_CALIBRATE_END,
    S2E_CHEF_CALIBRATE_CHECKPOINT,

    S2E_CHEF_END
};


// MemoryOpRecorder ////////////////////////////////////////////////////////////


struct MemoryOp {
    uint64_t seq_no;

    uint64_t pc;
    shared_ptr<CallStackFrame> frame;

    uint64_t address;
    uint64_t value;
    uint8_t size;
    bool is_write;

    MemoryOp(uint64_t s, uint64_t p, shared_ptr<CallStackFrame> f,
            uint64_t a, uint64_t v, uint8_t sz, bool w)
        : seq_no(s),
          pc(p),
          frame(f),
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

    void recordMemoryOp(uint64_t pc, shared_ptr<CallStackFrame> frame,
            uint64_t address, uint64_t value, uint8_t size, bool is_write) {
        mem_ops.push_back(MemoryOp(seq_counter++, pc, frame, address, value,
                size, is_write));
    }

    uint64_t seq_counter;
    MemoryOpSequence mem_ops;

private:
    MemoryOpRecorder(const MemoryOpRecorder&);
    void operator=(const MemoryOpRecorder&);
};


struct MemoryOpAnalyzer {
    MemoryOpAnalyzer(S2E &s2e, S2EExecutionState *state,
            const MemoryOpSequence &mem_ops, int min_opcodes,
            std::pair<uint64_t, uint64_t> memop_range)
        : s2e_(s2e),
          state_(state),
          mem_ops_(mem_ops),
          min_opcodes_(min_opcodes),
          memop_range_(memop_range) {

    }

    bool analyze() {
        if (!computeCandidateHLPC())
            return false;
        printCandidateHLPC();

        if (!computeCandidateRanges())
            return false;
        printCandidateRanges();

        for (std::vector<ValueRange>::iterator rit = candidate_ranges.begin(),
                rie = candidate_ranges.end(); rit != rie; ++rit) {
            ValueSet pcs;
            computeInstrumentationPoint(*rit, pcs);
            if (pcs.empty()) {
                continue;
            }
            s2e_.getMessagesStream(state_) << "Valid range found: "
                    << llvm::format("0x%x-0x%x", rit->first, rit->second)
                    << '\n';
            if (!instrumentation_pcs.empty()) {
                s2e_.getWarningsStream(state_)
                        << "Multiple valid bytecode buffers found. "
                        << "Could not differentiate between them." << '\n';
                return false;
            }
            instrumentation_pcs = pcs;
        }

        for (ValueSet::iterator it = instrumentation_pcs.begin(),
                ie = instrumentation_pcs.end(); it != ie; ++it) {
            s2e_.getMessagesStream(state_) << "Candidate PC: "
                    << llvm::format("0x%x", *it) << '\n';
        }

        return true;
    }

    typedef std::vector<const MemoryOp*> MemSeqVector;
    typedef std::set<uint64_t> ValueSet;
    typedef std::map<uint64_t, shared_ptr<MemSeqVector> > MemSeqVectorMap;
    typedef std::pair<uint64_t, uint64_t> ValueRange;

    MemSeqVectorMap candidate_hlpcs;
    ValueSet discarded_hlpcs;

    ValueSet instrumentation_pcs;

    std::vector<ValueRange> candidate_ranges;

private:
    void printCandidateHLPC() {
        std::string output;
        llvm::raw_string_ostream os(output);

        for (MemSeqVectorMap::iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie; ++vsit) {
            os << llvm::format("[HLPC]=0x%x", vsit->first) << " | ";
            MemSeqVector &vseq = *vsit->second;

            for (unsigned i = 0; i < vseq.size(); ++i) {
                if (i == 0)
                    os << llvm::format("0x%x", vseq[i]->value) << " ";
                else
                    os << llvm::format("+%d", vseq[i]->value - vseq[i-1]->value) << " ";
            }
            os << '\n';
        }

        s2e_.getMessagesStream(state_) << "Discarded HLPC variables: "
                << discarded_hlpcs.size() << '\n';
        s2e_.getMessagesStream(state_) << "Minimum update length: "
                << min_opcodes_ << '\n';
        s2e_.getMessagesStream(state_) << "Candidate HLPC variables:" << '\n'
                << os.str();
    }

    void printCandidateRanges() {
        std::string output;
        llvm::raw_string_ostream os(output);

        for (std::vector<ValueRange>::iterator it = candidate_ranges.begin(),
                ie = candidate_ranges.end(); it != ie; ++it) {
            os << llvm::format("Range 0x%x-0x%x", it->first, it->second) << '\n';
        }

        s2e_.getMessagesStream(state_) << "Candidate ranges:" << '\n'
                << os.str();
    }

    bool computeCandidateHLPC() {
        for (uint64_t mi = memop_range_.first; mi < memop_range_.second; ++mi) {
            const MemoryOp *memory_op = &mem_ops_[mi];
            if (!memory_op->is_write || memory_op->size != sizeof(target_ulong)) {
                continue;
            }
            if (discarded_hlpcs.count(memory_op->address) > 0) {
                continue;
            }

            MemSeqVectorMap::iterator vsit = candidate_hlpcs.find(memory_op->address);
            if (vsit == candidate_hlpcs.end()) {
                vsit = candidate_hlpcs.insert(std::make_pair(memory_op->address,
                        shared_ptr<MemSeqVector>(new MemSeqVector()))).first;
            }

            if (!vsit->second->empty() && memory_op->value <= vsit->second->back()->value) {
                discarded_hlpcs.insert(memory_op->address);
                candidate_hlpcs.erase(vsit);
                continue;
            }

            vsit->second->push_back(memory_op);
        }

        if (candidate_hlpcs.empty()) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "No candidate HLPC variables detected." << '\n';
            return false;
        }

        return true;
    }

    bool computeCandidateRanges() {
        // XXX: There are cases where the ranges are not merged correctly.
        for (MemSeqVectorMap::iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie; ++vsit) {
            if (vsit->second->size() < min_opcodes_) {
                continue;
            }

            ValueRange current_range = std::make_pair(vsit->second->front()->value,
                    vsit->second->back()->value);

            // We assume a bytecode instruction is no larger than 1KB.
            if (current_range.second - current_range.first > min_opcodes_*1024) {
                continue;
            }

            bool merged = false;

            for (std::vector<ValueRange>::iterator rit = candidate_ranges.begin(),
                    rie = candidate_ranges.end(); rit != rie; ++rit) {
                if (current_range.first <= rit->second && rit->first <= current_range.second) {
                    *rit = std::make_pair(std::min(current_range.first, rit->first),
                            std::max(current_range.second, rit->second));
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                candidate_ranges.push_back(current_range);
            }
        }

        if (candidate_ranges.empty()) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "Not enough HLPC variable updates." << '\n';
            return false;
        }

        return true;
    }

    void computeInstrumentationPoint(ValueRange range, ValueSet &pcs) {
        MemSeqVectorMap candidate_pcs;
        ValueSet discarded_pcs;

        for (uint64_t mi = memop_range_.first; mi < memop_range_.second; ++mi) {
            const MemoryOp *memory_op = &mem_ops_[mi];
            if (memory_op->is_write) {
                continue;
            }
            if (discarded_pcs.count(memory_op->pc)) {
                continue;
            }
            if (memory_op->address < range.first || memory_op->address > range.second) {
                continue;
            }

            MemSeqVectorMap::iterator vsit = candidate_pcs.find(memory_op->pc);
            if (vsit == candidate_pcs.end()) {
                vsit = candidate_pcs.insert(std::make_pair(memory_op->pc,
                        shared_ptr<MemSeqVector>(new MemSeqVector()))).first;
            }

            if (!vsit->second->empty() && memory_op->address <= vsit->second->back()->address) {
                discarded_pcs.insert(memory_op->pc);
                candidate_pcs.erase(vsit);
                continue;
            }

            vsit->second->push_back(memory_op);
        }

        pcs.clear();

        for (MemSeqVectorMap::iterator vsit = candidate_pcs.begin(),
                vsie = candidate_pcs.end(); vsit != vsie; ++vsit) {
            if (vsit->second->size() < min_opcodes_) {
                continue;
            }
            pcs.insert(vsit->first);
        }
    }

    S2E &s2e_;
    S2EExecutionState *state_;

    const MemoryOpSequence &mem_ops_;
    int min_opcodes_;
    std::pair<uint64_t, uint64_t> memop_range_;

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
      call_tracer_(new CallTracer(s2e, os_tracer, thread)),
      thread_(thread),
      calibrating_(false),
      min_opcode_count_(0),
      instrumentation_pc_(0) {

    syscall_range_ = syscall_monitor->registerForRange(
            S2E_CHEF_START, S2E_CHEF_END);
    syscall_range_->onS2ESystemCall.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onS2ESyscall));
}


InterpreterDetector::~InterpreterDetector() {
    resetInstrumentationPc(0);
    syscall_range_->deregister();
    on_concrete_data_memory_access_.disconnect();
}


void InterpreterDetector::onConcreteDataMemoryAccess(S2EExecutionState *state,
        uint64_t address, uint64_t value, uint8_t size, unsigned flags) {
    if (!thread_->running() || thread_->kernel_mode()) {
        return;
    }

    if (calibrating_) {
        memory_recording_->recordMemoryOp(state->getPc(),
                call_tracer_->call_stack()->top(),
                address, value, size,
                flags & S2E_MEM_TRACE_FLAG_WRITE);
    } else {
        // XXX: Could there be any race condition causing the memory access
        // to be deferred (e.g., hw interrupt)?
        assert(state->getPc() == instrumentation_pc_);

        assert(!(flags & S2E_MEM_TRACE_FLAG_WRITE));

#if 0
        s2e_.getMessagesStream(state) << "HLPC: "
                << llvm::format("0x%x", address) << '\n';
#endif

        onHighLevelInstructionStart.emit(state, address);

        on_concrete_data_memory_access_.disconnect();
    }
}


void InterpreterDetector::onS2ESyscall(S2EExecutionState *state,
        uint64_t syscall_id, uint64_t data, uint64_t size) {
    if (!thread_->running()) {
        return;
    }

    assert(data == 0);

    switch (syscall_id) {
    case S2E_CHEF_CALIBRATE_START:
        assert(!calibrating_);
        startCalibration(state);
        break;
    case S2E_CHEF_CALIBRATE_CHECKPOINT:
        s2e_.getMessagesStream(state) << "Calibration checkpoint." << '\n';


        min_opcode_count_ += size;
        if (!memop_range_.first) {
            memop_range_.first = memory_recording_->seq_counter;
        }
        memop_range_.second = memory_recording_->seq_counter;
        break;
    case S2E_CHEF_CALIBRATE_END:
        endCalibration(state);
        break;
    default:
        assert(0 && "FIXME");
    }
}


void InterpreterDetector::resetInstrumentationPc(uint64_t value) {
    instrumentation_pc_ = value;

    if (!instrumentation_pc_) {
        on_translate_instruction_start_.disconnect();
    } else {
        if (!on_translate_instruction_start_.connected()) {
            on_translate_instruction_start_ = os_tracer_.stream().
                    onTranslateInstructionStart.connect(
                            sigc::mem_fun(*this, &InterpreterDetector::onTranslateInstructionStart));
        }
    }

    s2e_tb_safe_flush();
}


void InterpreterDetector::startCalibration(S2EExecutionState *state) {
    assert(!calibrating_ && "Calibration start attempted while running");

    calibrating_ = true;

    resetInstrumentationPc(0);

    s2e_.getMessagesStream(state)
            << "Starting interpreter detector calibration." << '\n';

    memory_recording_.reset(new MemoryOpRecorder());
    on_concrete_data_memory_access_ = os_tracer_.stream().
            onConcreteDataMemoryAccess.connect(
                    sigc::mem_fun(*this, &InterpreterDetector::onConcreteDataMemoryAccess));

    min_opcode_count_ = 0;
    memop_range_ = std::make_pair(0, 0);
}


void InterpreterDetector::endCalibration(S2EExecutionState *state) {
    assert(calibrating_ && "Calibration end attempted before start");
    s2e_.getMessagesStream(state) << "Calibration ended." << '\n';

    computeInstrumentation(state);

    memory_recording_.reset();
    on_concrete_data_memory_access_.disconnect();
    calibrating_ = false;
}


void InterpreterDetector::computeInstrumentation(S2EExecutionState *state) {
    MemoryOpAnalyzer analyzer(s2e_, state, memory_recording_->mem_ops,
            min_opcode_count_, memop_range_);

    if (!analyzer.analyze())
        return;

    // FIXME
    resetInstrumentationPc(0);
}


void InterpreterDetector::onTranslateInstructionStart(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *tb, uint64_t pc) {
    if (!thread_->running()) {
        return;
    }

    assert(instrumentation_pc_ && "Callback should have not been enabled");
    assert(!calibrating_);

    if (pc != instrumentation_pc_) {
        return;
    }

    s2e_.getMessagesStream(state)
            << "Instrumentation point encountered.  Instrumenting..." << '\n';

    signal->connect(
            sigc::mem_fun(*this, &InterpreterDetector::onInstrumentationHit));
}

void InterpreterDetector::onInstrumentationHit(S2EExecutionState *state, uint64_t pc) {
    assert(pc == instrumentation_pc_);
    assert(!on_concrete_data_memory_access_.connected());

    on_concrete_data_memory_access_ = os_tracer_.stream().
            onConcreteDataMemoryAccess.connect(
                    sigc::mem_fun(*this, &InterpreterDetector::onConcreteDataMemoryAccess));
}


} /* namespace s2e */
