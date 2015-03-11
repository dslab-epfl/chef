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

#include "InterpreterDetector.h"

#include <s2e/S2E.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/Chef/OSTracer.h>
#include <s2e/Chef/S2ESyscallMonitor.h>
#include <s2e/Chef/CallTracer.h>

#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/CommandLine.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include <map>
#include <vector>
#include <set>

namespace {
    llvm::cl::opt<bool>
    DebugInstructions("debug-interp-instructions",
            llvm::cl::desc("Print detected interpreter instructions"),
            llvm::cl::init(false));

    llvm::cl::opt<bool>
    DebugDetection("debug-interp-detection",
            llvm::cl::desc("Print debug info for interpreter detection"),
            llvm::cl::init(false));
}

namespace s2e {


using boost::shared_ptr;
using boost::make_shared;


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
    typedef std::vector<const MemoryOp*> MemSeqVector;
    typedef std::set<uint64_t> ValueSet;
    typedef std::map<uint64_t, shared_ptr<MemSeqVector> > MemSeqVectorMap;
    typedef std::pair<uint64_t, uint64_t> ValueRange;

    MemoryOpAnalyzer(S2E &s2e, S2EExecutionState *state,
            const MemoryOpSequence &mem_ops, int min_opcodes,
            std::pair<uint64_t, uint64_t> memop_range)
        : instrum_function(0),
          instrum_hlpc_update(0),
          instrum_opcode_read(0),
          s2e_(s2e),
          state_(state),
          mem_ops_(mem_ops),
          min_opcodes_(min_opcodes),
          memop_range_(memop_range) {

    }

    bool analyze() {
        MemSeqVectorMap candidate_hlpcs;
        if (!computeCandidateHLPCUpdates(candidate_hlpcs))
            return false;

        MemSeqVectorMap::iterator hlpc_it;
        MemSeqVectorMap instrumentation_pcs;

        for (MemSeqVectorMap::iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie; ++vsit) {
            ValueRange range = std::make_pair(
                    vsit->second->front()->value,
                    vsit->second->back()->value);
            MemSeqVectorMap candidate_pcs;

            computeCandidateHLPCReads(range, vsit->second->front()->frame,
                    candidate_pcs);
            if (candidate_pcs.empty()) {
                continue;
            }
            if (!instrumentation_pcs.empty()) {
                s2e_.getWarningsStream(state_)
                        << "Multiple valid bytecode buffers found. "
                        << "Could not differentiate between them." << '\n';
                return false;
            }
            hlpc_it = vsit;
            instrumentation_pcs = candidate_pcs;
        }

        for (MemSeqVectorMap::iterator it = instrumentation_pcs.begin(),
                ie = instrumentation_pcs.end(); it != ie; ++it) {
            s2e_.getMessagesStream(state_) << "Candidate PC: "
                    << llvm::format("0x%x", it->first) << '\n';
        }

        instrum_function = hlpc_it->second->front()->frame->function;

        for (uint64_t mi = 0; mi < mem_ops_.size(); ++mi) {
            const MemoryOp *memory_op = &mem_ops_[mi];
            if (hlpc_it->second->front()->frame != memory_op->frame)
                continue;

            if (!instrum_hlpc_update && memory_op->is_write && memory_op->address == hlpc_it->first) {
                instrum_hlpc_update = memory_op->pc;
                s2e_.getMessagesStream(state_) << "HLPC update address: "
                        << llvm::format("0x%x", instrum_hlpc_update) << '\n';
            }

            if (!instrum_opcode_read && !memory_op->is_write && instrumentation_pcs.count(memory_op->pc) > 0) {
                instrum_opcode_read = memory_op->pc;
                s2e_.getMessagesStream(state_) << "Opcode read address: "
                        << llvm::format("0x%x", instrum_opcode_read) << '\n';
            }
        }

        if (!instrum_hlpc_update) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect the HLPC update point." << '\n';
            return false;
        }

        if (!instrum_opcode_read) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect the opcode update point." << '\n';
            return false;
        }

        return true;
    }

    uint64_t instrum_function;
    uint64_t instrum_hlpc_update;
    uint64_t instrum_opcode_read;

private:
    void printCandidateHLPC(const MemSeqVectorMap &candidate_hlpcs,
            const ValueSet &discarded_hlpcs) {

        std::string output;
        llvm::raw_string_ostream os(output);

        for (MemSeqVectorMap::const_iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie; ++vsit) {
            os << llvm::format("[HLPC]=0x%x", vsit->first) << " | ";
            const MemSeqVector &vseq = *vsit->second;

            for (unsigned i = 0; i < vseq.size(); ++i) {
                if (i == 0)
                    os << llvm::format("0x%x[EIP=0x%x]",
                            vseq[i]->value, vseq[i]->pc);
                else
                    os << llvm::format("+%d[EIP=0x%x]",
                            vseq[i]->value - vseq[i-1]->value, vseq[i]->pc);
                os << ' ';
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


    bool computeCandidateHLPCUpdates(MemSeqVectorMap &candidate_hlpcs) {
        ValueSet discarded_hlpcs;

        // Phase 1: Find all locations with monotonic value updates

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

            if (!vsit->second->empty() &&
                    (memory_op->value <= vsit->second->back()->value ||
                            memory_op->frame != vsit->second->back()->frame)) {
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

        if (DebugDetection) {
            printCandidateHLPC(candidate_hlpcs, discarded_hlpcs);
        }

        // Phase 2: Filter out non-HLPC access patterns

        for (MemSeqVectorMap::iterator vsit = candidate_hlpcs.begin(),
                vsie = candidate_hlpcs.end(); vsit != vsie;) {
            if (vsit->second->size() < min_opcodes_) {
                discarded_hlpcs.insert(vsit->first);
                candidate_hlpcs.erase(vsit++);
                continue;
            }

            ValueRange current_range = std::make_pair(
                    vsit->second->front()->value,
                    vsit->second->back()->value);

            // We assume a bytecode instruction is no larger than 1KB.
            if (current_range.second - current_range.first > min_opcodes_*1024) {
                discarded_hlpcs.insert(vsit->first);
                candidate_hlpcs.erase(vsit++);
                continue;
            }
            ++vsit;
        }

        if (candidate_hlpcs.empty()) {
            s2e_.getWarningsStream(state_)
                    << "Could not detect interpretation structure: "
                    << "Not enough HLPC variable updates." << '\n';
            return false;
        }

        return true;
    }

    bool computeCandidateHLPCReads(ValueRange range,
            shared_ptr<CallStackFrame> csf, MemSeqVectorMap &candidate_pcs) {
        ValueSet discarded_pcs;

        for (uint64_t mi = memop_range_.first; mi < memop_range_.second; ++mi) {
            const MemoryOp *memory_op = &mem_ops_[mi];
            if (memory_op->is_write) {
                continue;
            }
            if (discarded_pcs.count(memory_op->pc)) {
                continue;
            }
            if (memory_op->frame != csf) {
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

            if (!vsit->second->empty() &&
                    (memory_op->address <= vsit->second->back()->address ||
                            memory_op->frame != vsit->second->back()->frame)) {
                discarded_pcs.insert(memory_op->pc);
                candidate_pcs.erase(vsit);
                continue;
            }

            vsit->second->push_back(memory_op);
        }

        if (candidate_pcs.empty()) {
            return false;
        }

        for (MemSeqVectorMap::iterator vsit = candidate_pcs.begin(),
                vsie = candidate_pcs.end(); vsit != vsie;) {
            if (vsit->second->size() < min_opcodes_) {
                discarded_pcs.insert(vsit->first);
                candidate_pcs.erase(vsit++);
                continue;
            }
            ++vsit;
        }

        if (candidate_pcs.empty()) {
            return false;
        }

        return true;
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

// HighLevelStack //////////////////////////////////////////////////////////////

HighLevelStack::HighLevelStack(InterpreterDetector &detector,
        S2EExecutionState *s2e_state)
    : StreamAnalyzerState<HighLevelStack, InterpreterDetector>(detector, s2e_state) {

}


shared_ptr<HighLevelStack> HighLevelStack::clone(S2EExecutionState *s2e_state) {
    shared_ptr<HighLevelStack> new_state = shared_ptr<HighLevelStack>(
            new HighLevelStack(analyzer(), s2e_state));
    for (FrameVector::const_iterator it = frames_.begin(),
            ie = frames_.end(); it != ie; ++it) {
        shared_ptr<HighLevelFrame> frame = make_shared<HighLevelFrame>(*(*it));
        if (!new_state->frames_.empty()) {
            frame->parent = new_state->frames_.back();
        }
        new_state->frames_.push_back(frame);
    }
    return new_state;
}

// InterpreterDetector /////////////////////////////////////////////////////////

InterpreterDetector::InterpreterDetector(CallTracer &call_tracer,
        boost::shared_ptr<S2ESyscallMonitor> syscall_monitor)
    : StreamAnalyzer<HighLevelStack>(call_tracer.s2e(), call_tracer.stream()),
      os_tracer_(call_tracer.os_tracer()),
      call_tracer_(call_tracer),
      calibrating_(false),
      min_opcode_count_(0),
      instrum_function_(0),
      instrum_hlpc_update_(0),
      instrum_opcode_read_(0) {

    syscall_range_ = syscall_monitor->registerForRange(
            S2E_CHEF_START, S2E_CHEF_END);
    syscall_range_->onS2ESystemCall.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onS2ESyscall));

    on_state_switch_ = stream().onStateSwitch.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onStateSwitch));
}


InterpreterDetector::~InterpreterDetector() {
    syscall_range_->deregister();
    on_concrete_data_memory_access_.disconnect();
    on_symbolic_data_memory_access_.disconnect();
    on_stack_frame_push_.disconnect();
    on_stack_frame_popping_.disconnect();
    on_state_switch_.disconnect();
}


shared_ptr<HighLevelStack> InterpreterDetector::createState(S2EExecutionState *s2e_state) {
    return shared_ptr<HighLevelStack>(new HighLevelStack(*this, s2e_state));
}


void InterpreterDetector::onConcreteDataMemoryAccess(S2EExecutionState *state,
        uint64_t address, uint64_t value, uint8_t size, unsigned flags) {
    OSThread *thread = os_tracer_.getState(state)->getThread(call_tracer_.tracked_tid());

    if (!thread->running() || thread->kernel_mode()) {
        return;
    }

    bool is_write = flags & S2E_MEM_TRACE_FLAG_WRITE;

    shared_ptr<CallStack> ll_stack = call_tracer_.getState(state);

    if (calibrating_) {
        memory_recording_->recordMemoryOp(state->getPc(), ll_stack->top(),
                address, value, size, is_write);
        return;
    }

    shared_ptr<HighLevelStack> hl_stack = getState(state);
    assert(hl_stack->size() > 0);
    HighLevelFrame *hl_frame = hl_stack->top().get();
    assert(ll_stack->top()->id == hl_frame->low_level_frame_id);

    if (state->getPc() == instrum_hlpc_update_) {
        if (!is_write) {
            s2e().getWarningsStream(state) << "Unexpected memory access: "
                    << llvm::format("EIP=0x%x Addr=0x%x Value=0x%x Size=%d Flags=0x%08x",
                            state->getPc(), address, value, size, flags)
                    << '\n';
        }
        assert(is_write);

        if (!hl_frame->hlpc_ptr) {
            hl_frame->hlpc_ptr = address;
        } else {
            assert(hl_frame->hlpc_ptr == address);
        }
    }

    if (address == hl_frame->hlpc_ptr && is_write) {
        hl_frame->hlpc = value;
        onHighLevelPCUpdate.emit(state, hl_stack.get());

        if (DebugInstructions) {
            s2e().getMessagesStream(state)
                        << llvm::format("HLPC=0x%x", hl_frame->hlpc) << '\n';
        }
    }

    if (state->getPc() == instrum_opcode_read_) {
        // XXX: Extra memory reads in symbolic mode.
        assert(!is_write);
        //assert(!hl_frame->hlpc || address == hl_frame->hlpc);
        hl_frame->hlinst = address;
        onHighLevelInstructionFetch.emit(state, hl_stack.get());
        if (DebugInstructions) {
            s2e().getMessagesStream(state)
                    << llvm::format("Instruction=0x%x", hl_frame->hlinst) << '\n';
        }
    }
}


void InterpreterDetector::onSymbolicDataMemoryAccess(S2EExecutionState *state,
        klee::ref<klee::Expr> address, uint64_t concr_addr, bool &concretize) {
    // TODO Make sure here that no symbolic HLPC updates ever occur
}


void InterpreterDetector::onS2ESyscall(S2EExecutionState *state,
        uint64_t syscall_id, uint64_t data, uint64_t size) {
    OSThread *thread = os_tracer_.getState(state)->getThread(call_tracer_.tracked_tid());
    if (!thread->running()) {
        return;
    }

    assert(data == 0);

    switch (syscall_id) {
    case S2E_CHEF_CALIBRATE_START:
        startCalibration(state);
        break;
    case S2E_CHEF_CALIBRATE_CHECKPOINT:
        checkpointCalibration(state, size);
        break;
    case S2E_CHEF_CALIBRATE_END:
        endCalibration(state);
        break;
    default:
        assert(0 && "FIXME");
    }
}


void InterpreterDetector::startCalibration(S2EExecutionState *state) {
    assert(!calibrating_ && "Calibration start attempted while running");
    assert(!instrum_function_ && "Calibration attempted twice on the same interpreter");

    calibrating_ = true;

    s2e().getMessagesStream(state)
            << "Starting interpreter detector calibration." << '\n';

    memory_recording_.reset(new MemoryOpRecorder());
    on_concrete_data_memory_access_ = os_tracer_.stream().
            onConcreteDataMemoryAccess.connect(
                    sigc::mem_fun(*this, &InterpreterDetector::onConcreteDataMemoryAccess));
    on_symbolic_data_memory_access_ = os_tracer_.stream().
            onSymbolicMemoryAddress.connect(
                    sigc::mem_fun(*this, &InterpreterDetector::onSymbolicDataMemoryAccess));

    min_opcode_count_ = 0;
    memop_range_ = std::make_pair(0, 0);
}


void InterpreterDetector::checkpointCalibration(S2EExecutionState *state,
        unsigned count) {
    assert(calibrating_ && "Cannot checkpoint before calibration starts");
    s2e().getMessagesStream(state) << "Calibration checkpoint." << '\n';


    min_opcode_count_ += count;
    if (!memop_range_.first) {
        memop_range_.first = memory_recording_->seq_counter;
    }
    memop_range_.second = memory_recording_->seq_counter;
}


void InterpreterDetector::endCalibration(S2EExecutionState *state) {
    assert(calibrating_ && "Calibration end attempted before start");
    s2e().getMessagesStream(state) << "Calibration ended." << '\n';

    on_concrete_data_memory_access_.disconnect();
    on_symbolic_data_memory_access_.disconnect();
    calibrating_ = false;

    MemoryOpAnalyzer analyzer(s2e(), state, memory_recording_->mem_ops,
                min_opcode_count_, memop_range_);

    bool result = analyzer.analyze();

    memory_recording_.reset();

    if (!result) {
        return;
    }

    instrum_function_ = analyzer.instrum_function;
    instrum_hlpc_update_ = analyzer.instrum_hlpc_update;
    instrum_opcode_read_ = analyzer.instrum_opcode_read;

    startMonitoring(state);
}


void InterpreterDetector::startMonitoring(S2EExecutionState *state) {
    HighLevelStack *hl_stack = getState(state).get();
    CallStack *ll_stack = call_tracer_.getState(state).get();

    hl_stack->frames_.clear();

    for (unsigned i = 0; i < ll_stack->size(); ++i) {
        shared_ptr<CallStackFrame> ll_frame = ll_stack->frame(
                ll_stack->size() - i - 1);
        if (ll_frame->function == instrum_function_) {
            if (hl_stack->frames_.empty()) {
                hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                        ll_frame->id));
            } else {
                hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                        hl_stack->frames_.back(), ll_frame->id));
            }
        }
    }

    on_stack_frame_push_ = call_tracer_.onStackFramePush.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onLowLevelStackFramePush));
    on_stack_frame_popping_ = call_tracer_.onStackFramePopping.connect(
            sigc::mem_fun(*this, &InterpreterDetector::onLowLevelStackFramePopping));
}


void InterpreterDetector::onLowLevelStackFramePush(CallStack *call_stack,
        shared_ptr<CallStackFrame> old_top,
        shared_ptr<CallStackFrame> new_top) {
    updateMemoryTracking(new_top);

    if (new_top->function == instrum_function_) {
        HighLevelStack *hl_stack = getState(call_stack->s2e_state()).get();

        // Enter a new interpretation frame
        if (hl_stack->frames_.empty()) {
            hl_stack->frames_.push_back(make_shared<HighLevelFrame>(new_top->id));
        } else {
            hl_stack->frames_.push_back(make_shared<HighLevelFrame>(
                    hl_stack->frames_.back(), new_top->id));
        }

        onHighLevelFramePush.emit(call_stack->s2e_state(), hl_stack);

#if 1
        s2e().getMessagesStream(call_stack->s2e_state())
                << "Enter high-level frame. Stack size: "
                << hl_stack->frames_.size() << '\n';
#endif
    }
}


void InterpreterDetector::onLowLevelStackFramePopping(CallStack *call_stack,
        shared_ptr<CallStackFrame> old_top,
        shared_ptr<CallStackFrame> new_top) {
    updateMemoryTracking(new_top);

    if (old_top->function == instrum_function_) {
        // Return from the interpretation frame
        HighLevelStack *hl_stack = getState(call_stack->s2e_state()).get();
        assert(!hl_stack->frames_.empty());

        onHighLevelFramePopping.emit(call_stack->s2e_state(), hl_stack);

        hl_stack->frames_.pop_back();

#if 1
        s2e().getMessagesStream(call_stack->s2e_state())
                << "Leaving high-level frame. Stack size: "
                << hl_stack->frames_.size() << '\n';
#endif
    }
}


void InterpreterDetector::onStateSwitch(S2EExecutionState *prev,
        S2EExecutionState *next) {
    CallStack *call_stack = call_tracer_.getState(next).get();
    assert(call_stack->size() > 0);
    updateMemoryTracking(call_stack->top());
}


void InterpreterDetector::updateMemoryTracking(boost::shared_ptr<CallStackFrame> top) {
    if (top->function != instrum_function_) {
        on_concrete_data_memory_access_.disconnect();
        on_symbolic_data_memory_access_.disconnect();
        return;
    }

    if (!on_concrete_data_memory_access_.connected()) {
        on_concrete_data_memory_access_ = os_tracer_.stream().
            onConcreteDataMemoryAccess.connect(sigc::mem_fun(
                    *this, &InterpreterDetector::onConcreteDataMemoryAccess));
    }
    if (!on_symbolic_data_memory_access_.connected()) {
        on_symbolic_data_memory_access_ = os_tracer_.stream().
            onSymbolicMemoryAddress.connect(sigc::mem_fun(
                    *this, &InterpreterDetector::onSymbolicDataMemoryAccess));
    }
}


} /* namespace s2e */
