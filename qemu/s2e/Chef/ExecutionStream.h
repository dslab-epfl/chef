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

#ifndef QEMU_S2E_EXECUTIONSTREAM_H_
#define QEMU_S2E_EXECUTIONSTREAM_H_

#include <klee/Expr.h>
#include <s2e/Signals/Signals.h>
#include <stdint.h>

extern "C" {
typedef struct TranslationBlock TranslationBlock;
}


namespace s2e {

class S2EExecutionState;

typedef sigc::signal<void, S2EExecutionState*, uint64_t /* pc */> ExecutionSignal;

class ExecutionStream {
public:
    ExecutionStream() {}
    virtual ~ExecutionStream() {}

    /** Signal that is emitted on beginning and end of code generation
        for each QEMU translation block.
    */
    sigc::signal<void, ExecutionSignal*,
            S2EExecutionState*,
            TranslationBlock*,
            uint64_t /* block PC */>
            onTranslateBlockStart;

    /**
     * Signal that is emitted upon end of a translation block.
     * If the end is a conditional branch, it is emitted for both outcomes.
     */
    sigc::signal<void, ExecutionSignal*,
            S2EExecutionState*,
            TranslationBlock*,
            uint64_t /* ending instruction pc */,
            bool /* static target is valid */,
            uint64_t /* static target pc */>
            onTranslateBlockEnd;

    /**
     * Signal that is emitted when the translator finishes
     * translating the block.
     */
    sigc::signal<void, S2EExecutionState*,
            TranslationBlock*,
            uint64_t /* ending instruction pc */>
            onTranslateBlockComplete;


    /** Signal that is emitted on code generation for each instruction */
    sigc::signal<void, ExecutionSignal*,
            S2EExecutionState*,
            TranslationBlock*,
            uint64_t /* instruction PC */>
            onTranslateInstructionStart, onTranslateInstructionEnd;

    /** Signal that is emitted on code generation for each jump instruction */
    sigc::signal<void, ExecutionSignal*,
            S2EExecutionState*,
            TranslationBlock*,
            uint64_t /* instruction PC */,
            int /* jump_type */>
            onTranslateJumpStart;

    /** Signal that is emitted when custom opcode is detected */
    sigc::signal<void, S2EExecutionState*,
            uint64_t  /* arg */
            >
            onCustomInstruction;

    /**
     * The current execution privilege level was changed (e.g., kernel-mode=>user-mode)
     * previous and current are privilege levels. The meaning of the value may
     * depend on the architecture.
     */
    sigc::signal<void,
                 S2EExecutionState* /* current state */,
                 unsigned /* previous level */,
                 unsigned /* current level */>
          onPrivilegeChange;

    /**
     * The current page directory was changed.
     * This may occur, e.g., when the OS swaps address spaces.
     * The addresses correspond to physical addresses.
     */
    sigc::signal<void,
                 S2EExecutionState* /* current state */,
                 uint64_t /* previous page directory base */,
                 uint64_t /* current page directory base */>
          onPageDirectoryChange;

    /**
     *  Triggered *after* each instruction is translated to notify
     *  plugins of which registers are used by the instruction.
     *  Each bit of the mask corresponds to one of the registers of
     *  the architecture (e.g., R_EAX, R_ECX, etc).
     */
    sigc::signal<void,
                 ExecutionSignal*,
                 S2EExecutionState* /* current state */,
                 TranslationBlock*,
                 uint64_t /* program counter of the instruction */,
                 uint64_t /* registers read by the instruction */,
                 uint64_t /* registers written by the instruction */,
                 bool /* instruction accesses memory */>
          onTranslateRegisterAccessEnd;

    /**
     * Signal emitted before handling a memory address.
     * - The concrete address is one example of an address that satisfies
     * the constraints.
     * - The concretize flag can be set to ask the engine to concretize the address.
     */
    sigc::signal<void, S2EExecutionState*,
                 klee::ref<klee::Expr> /* virtualAddress */,
                 uint64_t /* concreteAddress */,
                 bool & /* concretize */>
            onSymbolicMemoryAddress;

    /* Optimized signal for concrete accesses */
    sigc::signal<void, S2EExecutionState*,
                 uint64_t /* virtualAddress */,
                 uint64_t /* value */,
                 uint8_t /* size */,
                 unsigned /* flags */>
            onConcreteDataMemoryAccess;

    /** Signal emitted when the state is forked */
    sigc::signal<void, S2EExecutionState* /* originalState */,
                 const std::vector<S2EExecutionState*>& /* newStates */,
                 const std::vector<klee::ref<klee::Expr> >& /* newConditions */>
            onStateFork;

    /** Signal that is emitted when two states are merged */
    sigc::signal<void, S2EExecutionState* /* destination */,
                 S2EExecutionState* /* source */>
            onStateMerge;

    sigc::signal<void,
                 S2EExecutionState*, /* currentState */
                 S2EExecutionState*> /* nextState */
            onStateSwitch;

    /**
     * Triggered whenever a state is killed
     */
    sigc::signal<void, S2EExecutionState*> onStateKill;

private:
    ExecutionStream(const ExecutionStream&);
    void operator=(const ExecutionStream&);
};


class ExecutionStreamFilter : public ExecutionStream {
public:
    ExecutionStreamFilter(ExecutionStream &parent, bool coarse = true);
    virtual ~ExecutionStreamFilter();

    void connect();
    void disconnect();

    bool connected() {
        return connected_;
    }

    bool coarse() {
        return coarse_;
    }

protected:
    void onTranslateBlockStartDisp(ExecutionSignal*, S2EExecutionState*,
            TranslationBlock*, uint64_t);
    void onTranslateBlockEndDisp(ExecutionSignal*, S2EExecutionState*,
            TranslationBlock*, uint64_t, bool, uint64_t);
    void onTranslateJumpStartDisp(ExecutionSignal*, S2EExecutionState*,
            TranslationBlock*, uint64_t, int);



private:
    ExecutionStream &parent_;
    bool connected_;
    bool coarse_;

    sigc::connection on_translate_block_start_;
    sigc::connection on_translate_block_end_;
    sigc::connection on_translate_jump_start_;

    ExecutionStreamFilter(const ExecutionStreamFilter&);
    void operator=(const ExecutionStreamFilter&);
};


} /* namespace s2e */

#endif /* QEMU_S2E_EXECUTIONSTREAM_H_ */
