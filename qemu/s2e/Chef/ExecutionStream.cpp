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

#include "ExecutionStream.h"



namespace s2e {

ExecutionStreamFilter::ExecutionStreamFilter(ExecutionStream &parent,
        bool coarse)
    : parent_(parent),
      connected_(false),
      coarse_(coarse) {

}

ExecutionStreamFilter::~ExecutionStreamFilter() {
    if (connected_) {
        disconnect();
    }
}

void ExecutionStreamFilter::connect() {
    assert(!connected_);

    if (coarse_) {
        on_translate_block_start_ = parent_.onTranslateBlockStart.connect(
                sigc::mem_fun(*this, &ExecutionStreamFilter::onTranslateBlockStartDisp));
        on_translate_block_end_ = parent_.onTranslateBlockEnd.connect(
                sigc::mem_fun(*this, &ExecutionStreamFilter::onTranslateBlockEndDisp));
    }

    connected_ = true;
}

void ExecutionStreamFilter::disconnect() {
    assert(connected_);

    if (coarse_) {
        on_translate_block_start_.disconnect();
        on_translate_block_end_.disconnect();
        on_translate_jump_start_.disconnect();
    }

    connected_ = false;
}

void ExecutionStreamFilter::onTranslateBlockStartDisp(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *block, uint64_t pc) {
    if (connected_) {
        onTranslateBlockStart.emit(signal, state, block, pc);
    }
}

void ExecutionStreamFilter::onTranslateBlockEndDisp(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *block, uint64_t pc, bool b, uint64_t u) {
    if (connected_) {
        onTranslateBlockEnd.emit(signal, state, block, pc, b, u);
    }
}

void ExecutionStreamFilter::onTranslateJumpStartDisp(ExecutionSignal *signal,
        S2EExecutionState *state, TranslationBlock *block, uint64_t pc, int i) {
    if (connected_) {
        onTranslateJumpStart.emit(signal, state, block, pc, i);
    }
}

}
