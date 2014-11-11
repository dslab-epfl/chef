#include "KleeExecutor.h"
#include "klee/SolverFactory.h"

using namespace klee;

KleeExecutor::KleeExecutor(const InterpreterOptions &opts, InterpreterHandler *ie)
        : Executor(opts, ie,
                new DefaultSolverFactory(ie))
{
}

///

Interpreter *Interpreter::createKleeExecutor(const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new KleeExecutor(opts, ih);
}
