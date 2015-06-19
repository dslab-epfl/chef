#include "KleeExecutor.h"
#include "klee/SolverFactory.h"
#include "klee/data/EventLogger.h"

using namespace klee;

KleeExecutor::KleeExecutor(const InterpreterOptions &opts, InterpreterHandler *ie)
        : Executor(opts, ie,
		   new DefaultSolverFactory(ie),
		   new EventLogger(ie->getDataStore()))
{
}

///

Interpreter *Interpreter::createKleeExecutor(const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new KleeExecutor(opts, ih);
}
