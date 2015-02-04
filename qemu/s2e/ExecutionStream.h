/*
 * ExecutionStream.h
 *
 *  Created on: Jan 31, 2015
 *      Author: stefan
 */

#ifndef QEMU_S2E_EXECUTIONSTREAM_H_
#define QEMU_S2E_EXECUTIONSTREAM_H_


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
