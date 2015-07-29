LEVEL=../..

LIBRARYNAME=kleeData
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
BUILT_SOURCES = Expr.pb.cc Queries.pb.cc

PROTOC=protoc

include $(LEVEL)/Makefile.common

./%.pb.cc ./%.pb.h: %.proto
	$(PROTOC) -I$(dir $<) --cpp_out=. $<
