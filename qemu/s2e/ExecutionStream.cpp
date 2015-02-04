/*
 * ExecutionStream.cpp
 *
 *  Created on: Feb 4, 2015
 *      Author: stefan
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
