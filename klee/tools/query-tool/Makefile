LEVEL=../..
TOOLNAME = query-tool

include $(LEVEL)/Makefile.config


USEDLIBS = kleeData.a kleaverSolver.a kleaverExpr.a kleeSupport.a kleeBasic.a kleeCore.a kleaverSolver.a kleaverExpr.a kleeSupport.a kleeBasic.a kleeCore.a

LINK_COMPONENTS = ipo

include $(LEVEL)/Makefile.common

# This dependency is ridiculous. We must refactor the Expr library.
ifeq ($(ENABLE_EXT_STP),1)
  LIBS += -lstp
endif
LIBS += -lz3 -lsqlite3
