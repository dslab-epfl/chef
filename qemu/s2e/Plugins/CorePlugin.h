/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2010, Dependable Systems Laboratory, EPFL
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
 *    Vitaly Chipounov <vitaly.chipounov@epfl.ch>
 *    Volodymyr Kuznetsov <vova.kuznetsov@epfl.ch>
 *
 * All contributors are listed in the S2E-AUTHORS file.
 */

#ifndef S2E_CORE_PLUGIN_H
#define S2E_CORE_PLUGIN_H

#include <s2e/Plugin.h>
#include <klee/Expr.h>

#include <s2e/Signals/Signals.h>
#include <vector>
#include <inttypes.h>
#include <cpu.h>
#include <s2e/s2e_qemu.h>
#include <s2e/Chef/ExecutionStream.h>

#include <llvm/Support/TimeValue.h>

extern "C" {
typedef struct TranslationBlock TranslationBlock;
struct QEMUTimer;
struct Monitor;
struct QDict;
struct QObject;
}

namespace klee {
struct Query;
}

namespace s2e {

class S2EExecutionState;

/** A type of a signal emitted on instruction execution. Instances of this signal
    will be dynamically created and destroyed on demand during translation. */
typedef sigc::signal<void, S2EExecutionState*, uint64_t /* pc */> ExecutionSignal;

/** This is a callback to check whether some port returns symbolic values.
  * An interested plugin can use it. Only one plugin can use it at a time.
  * This is necessary tp speedup checks (and avoid using signals) */
typedef bool (*SYMB_PORT_CHECK)(uint16_t port, void *opaque);
typedef bool (*SYMB_MMIO_CHECK)(uint64_t physaddress, uint64_t size, void *opaque);

class CorePlugin : public Plugin, public ExecutionStream {
    S2E_PLUGIN

private:
    struct QEMUTimer *m_Timer;
    SYMB_PORT_CHECK m_isPortSymbolicCb;
    SYMB_MMIO_CHECK m_isMmioSymbolicCb;
    void *m_isPortSymbolicOpaque;
    void *m_isMmioSymbolicOpaque;

public:
    CorePlugin(S2E* s2e): Plugin(s2e) {
        m_Timer = NULL;
        m_isPortSymbolicCb = NULL;
        m_isMmioSymbolicCb = NULL;
        m_isPortSymbolicOpaque = NULL;
        m_isMmioSymbolicOpaque = NULL;
    }

    void initialize();
    void initializeTimers();

    void setPortCallback(SYMB_PORT_CHECK cb, void *opaque) {
        m_isPortSymbolicCb = cb;
        m_isPortSymbolicOpaque = opaque;
    }

    void setMmioCallback(SYMB_MMIO_CHECK cb, void *opaque) {
        m_isMmioSymbolicCb = cb;
        m_isMmioSymbolicOpaque = opaque;
    }

    void enableMmioCallbacks(bool enable) {
        g_s2e_enable_mmio_checks = enable;
    }

    inline bool isPortSymbolic(uint16_t port) const {
        if (m_isPortSymbolicCb) {
            return m_isPortSymbolicCb(port, m_isPortSymbolicOpaque);
        }
        return false;
    }

    inline bool isMmioSymbolic(uint64_t physAddress, uint64_t size) const {
        if (m_isMmioSymbolicCb) {
            return m_isMmioSymbolicCb(physAddress, size, m_isMmioSymbolicOpaque);
        }
        return false;
    }

    struct QEMUTimer *getTimer() {
        return m_Timer;
    }

    /** Signal that is emitted upon exception */
    sigc::signal<void, S2EExecutionState*, 
            unsigned /* Exception Index */,
            uint64_t /* pc */>
            onException;

    /** Signal that is emitted on each port access */
    sigc::signal<void, S2EExecutionState*,
                 klee::ref<klee::Expr> /* port */,
                 klee::ref<klee::Expr> /* value */,
                 bool /* isWrite */>
            onPortAccess;

    sigc::signal<void> onTimer;

    /**
     * Triggered when S2E wants to generate a test case
     */
    sigc::signal<void,
                 S2EExecutionState*, /* currentState */
                 const std::string& /* message */>
            onTestCaseGeneration;


    /** Signal emitted when spawning a new S2E process */
    sigc::signal<void, bool /* prefork */,
                bool /* ischild */,
                unsigned /* parentProcId */>
            onProcessFork;

    /**
     * Signal emitted when a new S2E process was spawned and all
     * parent states were removed from the child and child states
     * removed from the parent.
     */
    sigc::signal<void, bool /* isChild */> onProcessForkComplete;


    /** Signal that is emitted upon TLB miss */
    sigc::signal<void, S2EExecutionState*, uint64_t, bool> onTlbMiss;

    /** Signal that is emitted upon page fault */
    sigc::signal<void, S2EExecutionState*, uint64_t, bool> onPageFault;

    /** Signal emitted when QEMU is ready to accept registration of new devices */
    sigc::signal<void> onDeviceRegistration;

    /** Signal emitted when QEMU is ready to activate registered devices */
    sigc::signal<void, int /* bus type */,
                 void *> /* bus */
            onDeviceActivation;

    sigc::signal<void, S2EExecutionState *,
                 void * /* PCIDevice * */,
                 int /* bar_index */,
                 uint64_t /* old_addr */
                 >
            onPciDeviceMappingUpdate;

    /**
     * S2E completed initialization and is about to enter
     * the main execution loop for the first time.
     */
    sigc::signal<void,
                 S2EExecutionState* /* current state */>
          onInitializationComplete;

    /**
     * S2E has just received a command from QEMU's
     * QMP monitor interface. The dictionnary contains
     * the S2E-specific command parameters.
     */
    sigc::signal<void,
                Monitor * /* mon */,
                const QDict * /* qdict */,
                QDict * /* ret */>
          onMonitorCommand;

    /**
     * Fired when QEMU generates en event.
     * onMonitorEvent allows plugins to react to them by
     * sending back some data that will be serialized over
     * the QEMU monitor interface (e.g., to JSON).
     */
    sigc::signal<void,
                const QDict * /* event */,
                QDict * /* result */>
          onMonitorEvent;

    /**
     * Fired when a solver query completed.
     */
    sigc::signal<void,
                 const klee::Query&,
                 llvm::sys::TimeValue>
          onSolverQuery;
};

} // namespace s2e

#endif // S2E_CORE_PLUGIN_H
