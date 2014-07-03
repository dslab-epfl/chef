/*
 * STPSolver.cpp
 *
 *  Created on: Jun 3, 2014
 *      Author: stefan
 */

#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/SolverStats.h"
#include "klee/Common.h"
#include "klee/Constraints.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "klee/TimerStatIncrementer.h"

#include "llvm/Support/CommandLine.h"

#include "STPBuilder.h"

#ifndef __MINGW32__
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include <errno.h>
#include <signal.h>


#define vc_bvBoolExtract IAMTHESPAWNOFSATAN

namespace {
  llvm::cl::opt<bool>
  ReinstantiateSolver("reinstantiate-solver",
                      llvm::cl::init(false));

  llvm::cl::opt<bool>
  EnableTimingLog("stp-enable-timing",
                  llvm::cl::desc("Measures execution times of various parts of S2E"),  llvm::cl::init(false));

#ifdef HAVE_EXT_STP
  enum SatSolver {
    MINISAT_SOLVER, SIMPLIFYING_MINISAT_SOLVER,
    CRYPTOMINISAT_SOLVER, MINISAT_PROPAGATORS
  };

  llvm::cl::opt<SatSolver>
  SatSolverType("sat-solver-type",
                llvm::cl::desc("SAT solver to use:"),
                llvm::cl::values(
                        clEnumValN(MINISAT_SOLVER, "minisat", "Minisat"),
                        clEnumValN(SIMPLIFYING_MINISAT_SOLVER, "simplifying-minisat", "Simplifying minisat"),
                        clEnumValN(CRYPTOMINISAT_SOLVER, "cryptominisat", "Cryptominisat"),
                        clEnumValN(MINISAT_PROPAGATORS, "minisat-propagators", "Minisat propagators"),
                        clEnumValEnd
                ), llvm::cl::init(CRYPTOMINISAT_SOLVER));
#endif
}



namespace klee {

class STPSolverImpl : public SolverImpl {
private:
  /// The solver we are part of, for access to public information.
  STPSolver *solver;
  VC vc;
  STPBuilder *builder;
  double timeout;
  bool useForkedSTP;

  void reinstantiate();

public:
  STPSolverImpl(STPSolver *_solver, bool _useForkedSTP);
  ~STPSolverImpl();

  char *getConstraintLog(const Query&);
  void setTimeout(double _timeout) { timeout = _timeout; }

  bool computeTruth(const Query&, bool &isValid);
  bool computeValue(const Query&, ref<Expr> &result);
  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution);
};

static unsigned char *shared_memory_ptr;
static const unsigned shared_memory_size = 1<<20;
static int shared_memory_id;

static void stp_error_handler(const char* err_msg) {
  fprintf(stderr, "error: STP Error: %s\n", err_msg);
  exit(-1);
}

STPSolverImpl::STPSolverImpl(STPSolver *_solver, bool _useForkedSTP)
  : solver(_solver),
    vc(vc_createValidityChecker()),
    builder(new STPBuilder(vc)),
    timeout(0.0),
    useForkedSTP(_useForkedSTP)
{
  assert(vc && "unable to create validity checker");
  assert(builder && "unable to create STPBuilder");

#ifdef HAVE_EXT_STP
  vc_setInterfaceFlags(vc, EXPRDELETE, 0);

  switch (SatSolverType) {
      case MINISAT_SOLVER:
        vc_setInterfaceFlags(vc, MS, 0);
        break;
      case SIMPLIFYING_MINISAT_SOLVER:
        vc_setInterfaceFlags(vc, SMS, 0);
        break;
      case CRYPTOMINISAT_SOLVER:
        vc_setInterfaceFlags(vc, CMS2, 0);
        break;
      case MINISAT_PROPAGATORS:
        vc_setInterfaceFlags(vc, MSP, 0);
        break;
  }
#endif

  vc_registerErrorHandler(stp_error_handler);

  if (useForkedSTP) {
#ifdef __MINGW32__
    assert(false && "Cannot use forked stp solver on Windows");
#else
    shared_memory_id = shmget(IPC_PRIVATE, shared_memory_size, IPC_CREAT | 0700);
    assert(shared_memory_id>=0 && "shmget failed");
    shared_memory_ptr = (unsigned char*) shmat(shared_memory_id, NULL, 0);
    assert(shared_memory_ptr!=(void*)-1 && "shmat failed");
    shmctl(shared_memory_id, IPC_RMID, NULL);
#endif
  }
}

STPSolverImpl::~STPSolverImpl() {
  delete builder;

  vc_Destroy(vc);
}

void STPSolverImpl::reinstantiate()
{
    //XXX: This seems to cause crashes.
    //Will have to find other ways of preventing slowdown
    if (ReinstantiateSolver) {
        delete builder;
        vc_Destroy(vc);
        vc = vc_createValidityChecker();
        builder = new STPBuilder(vc);

        #ifdef HAVE_EXT_STP
        vc_setInterfaceFlags(vc, EXPRDELETE, 0);
        #endif

        vc_registerErrorHandler(stp_error_handler);
    }
}

/***/

STPSolver::STPSolver(bool useForkedSTP)
  : Solver(new STPSolverImpl(this, useForkedSTP))
{
}

char *STPSolver::getConstraintLog(const Query &query) {
  return static_cast<STPSolverImpl*>(impl)->getConstraintLog(query);
}

void STPSolver::setTimeout(double timeout) {
  static_cast<STPSolverImpl*>(impl)->setTimeout(timeout);
}

/***/

char *STPSolverImpl::getConstraintLog(const Query &query) {
  vc_push(vc);
  for (ConstraintManager::const_iterator it = query.constraints.begin(),
         ie = query.constraints.end(); it != ie; ++it)
    vc_assertFormula(vc, builder->construct(query.constraints.toExpr(*it)));
  assert(query.expr == ConstantExpr::alloc(0, Expr::Bool) &&
         "Unexpected expression in query!");

  char *buffer;
  unsigned long length;
  vc_printQueryStateToBuffer(vc, builder->getFalse(),
                             &buffer, &length, false);
  vc_pop(vc);

  return buffer;
}

bool STPSolverImpl::computeTruth(const Query& query,
                                 bool &isValid) {
  std::vector<const Array*> objects;
  std::vector< std::vector<unsigned char> > values;
  bool hasSolution;

  if (!computeInitialValues(query, objects, values, hasSolution))
    return false;

  isValid = !hasSolution;
  return true;
}

bool STPSolverImpl::computeValue(const Query& query,
                                 ref<Expr> &result) {
  std::vector<const Array*> objects;
  std::vector< std::vector<unsigned char> > values;
  bool hasSolution;

  // Find the object used in the expression, and compute an assignment
  // for them.
  findSymbolicObjects(query.expr, objects);
  if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
    return false;
  assert(hasSolution && "state has invalid constraint set");

  // Evaluate the expression with the computed assignment.
  Assignment a(objects, values);
  result = a.evaluate(query.expr);

  return true;
}


static void runAndGetCex(::VC vc, STPBuilder *builder, ::VCExpr q,
                   const std::vector<const Array*> &objects,
                   std::vector< std::vector<unsigned char> > &values,
                   bool &hasSolution) {
  // XXX I want to be able to timeout here, safely
    int result;

    result = vc_query(vc, q);

    if (result < 0) {
        if (klee_message_stream) {
            char *buffer;
            unsigned long length;
            vc_push(vc);
            vc_printQueryStateToBuffer(vc, q,
                                       &buffer, &length, false);
            vc_pop(vc);

            *klee_message_stream << buffer << '\n';
            free(buffer);
        }

        //Bug in stp
        throw std::exception();
    }

//    klee_message_stream << "solver returned " << *result << '\n';
    hasSolution = !result;

    if (hasSolution) {
        values.reserve(objects.size());
        for (std::vector<const Array*>::const_iterator
             it = objects.begin(), ie = objects.end(); it != ie; ++it) {
            const Array *array = *it;
            std::vector<unsigned char> data;

            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; offset++) {
                ExprHandle counter =
                        vc_getCounterExample(vc, builder->getInitialRead(array, offset));
                unsigned char val = getBVUnsigned(counter);
                data.push_back(val);
            }

            values.push_back(data);
        }
    }
}

static void stpTimeoutHandler(int x) {
  _exit(52);
}

static bool runAndGetCexForked(::VC vc,
                               STPBuilder *builder,
                               ::VCExpr q,
                               const std::vector<const Array*> &objects,
                               std::vector< std::vector<unsigned char> >
                                 &values,
                               bool &hasSolution,
                               double timeout) {
#ifdef __MINGW32__
  assert(false && "Cannot run runAndGetCexForked on Windows");
  return false;
#else

  unsigned char *pos = shared_memory_ptr;
  unsigned sum = 0;
  for (std::vector<const Array*>::const_iterator
         it = objects.begin(), ie = objects.end(); it != ie; ++it)
    sum += (*it)->size;
  assert(sum<shared_memory_size && "not enough shared memory for counterexample");

  fflush(stdout);
  fflush(stderr);

  sigset_t sig_mask, sig_mask_old;
  sigfillset(&sig_mask);
  sigemptyset(&sig_mask_old);
  sigprocmask(SIG_SETMASK, &sig_mask, &sig_mask_old);

  int pid = fork();
  if (pid==-1) {
    fprintf(stderr, "error: fork failed (for STP)");
    return false;
  }

  if (pid == 0) {
    sigprocmask(SIG_SETMASK, &sig_mask_old, NULL);
    if (timeout) {
      ::alarm(0); /* Turn off alarm so we can safely set signal handler */
      ::signal(SIGALRM, stpTimeoutHandler);
      ::alarm(std::max(1, (int)timeout));
    }
    unsigned res = vc_query(vc, q);
    if (!res) {
      for (std::vector<const Array*>::const_iterator
             it = objects.begin(), ie = objects.end(); it != ie; ++it) {
        const Array *array = *it;
        for (unsigned offset = 0; offset < array->size; offset++) {
          ExprHandle counter =
            vc_getCounterExample(vc, builder->getInitialRead(array, offset));
          *pos++ = getBVUnsigned(counter);
        }
      }
    }
    _exit(res);
  } else {
    int status;
    pid_t res;

    do {
      res = waitpid(pid, &status, 0);
    } while (res < 0 && errno == EINTR);

    sigprocmask(SIG_SETMASK, &sig_mask_old, NULL);

    if (res < 0) {
      fprintf(stderr, "error: waitpid() for STP failed\n");
      perror("waitpid()");
      return false;
    }

    // From timed_run.py: It appears that linux at least will on
    // "occasion" return a status when the process was terminated by a
    // signal, so test signal first.
    if (WIFSIGNALED(status) || !WIFEXITED(status)) {
      fprintf(stderr, "error: STP did not return successfully\n");
      return false;
    }

    int exitcode = WEXITSTATUS(status);
    if (exitcode==0) {
      hasSolution = true;
    } else if (exitcode==1) {
      hasSolution = false;
    } else if (exitcode==52) {
      fprintf(stderr, "error: STP timed out");
      return false;
    } else {
      fprintf(stderr, "error: STP did not return a recognized code (%d)\n", exitcode);
      return false;
    }

    if (hasSolution) {
      values = std::vector< std::vector<unsigned char> >(objects.size());
      unsigned i=0;
      for (std::vector<const Array*>::const_iterator
             it = objects.begin(), ie = objects.end(); it != ie; ++it) {
        const Array *array = *it;
        std::vector<unsigned char> &data = values[i++];
        data.insert(data.begin(), pos, pos + array->size);
        pos += array->size;
      }
    }

    return true;
  }
#endif
}

bool
STPSolverImpl::computeInitialValues(const Query &query,
                                    const std::vector<const Array*>
                                      &objects,
                                    std::vector< std::vector<unsigned char> >
                                      &values,
                                    bool &hasSolution) {
  if (EnableTimingLog) {
    TimerStatIncrementer t(stats::queryTime);
  }

  reinstantiate();

  vc_push(vc);

  for (ConstraintManager::const_iterator it = query.constraints.begin(),
         ie = query.constraints.end(); it != ie; ++it)
    vc_assertFormula(vc, builder->construct(query.constraints.toExpr(*it)));

  ++stats::queries;
  ++stats::queryCounterexamples;

  ExprHandle stp_e = builder->construct(query.expr);

  bool success;
  if (useForkedSTP) {
    success = runAndGetCexForked(vc, builder, stp_e, objects, values,
                                 hasSolution, timeout);
  } else {
    try {
        runAndGetCex(vc, builder, stp_e, objects, values, hasSolution);
        success = true;
    } catch(std::exception &) {
        klee::klee_warning("STP solver threw an exception");
        exit(-1);
        vc_pop(vc);
        reinstantiate();
        success = false;
        return success;
    }
  }

  if (success) {
    if (hasSolution)
      ++stats::queriesInvalid;
    else
      ++stats::queriesValid;
  }

  vc_pop(vc);


  return success;
}

}
