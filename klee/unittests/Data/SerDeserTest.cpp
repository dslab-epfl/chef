/*
 * SerDeserTest.cpp
 *
 *  Created on: Oct 19, 2012
 *      Author: bucur
 */

#include "gtest/gtest.h"
#include "google/protobuf/stubs/common.h"

#include "klee/ExprBuilder.h"

#include "klee/data/ExprDeserializer.h"
#include "klee/data/ExprSerializer.h"

#include <sstream>
#include <llvm/Support/raw_ostream.h>

namespace klee {
namespace {


class SerDeserTest: public ::testing::Test {
protected:
  virtual void SetUp() {
    eb = createDefaultExprBuilder();
  }

  virtual void TearDown() {
    delete eb;
    arrays.clear();
  }

  void TestSerDeser(ref<Expr> expr, std::vector<Array*> &expr_arrays) {
    std::string data;
    llvm::raw_string_ostream oss(data);

    ExprSerializer serializer;
    ExprFrame frame(serializer);
    uint64_t id = frame.RecordExpr(expr);
    oss << frame;
    oss.flush();

    std::istringstream iss(data);
    ExprDeserializer deserializer(*eb, expr_arrays);
    deserializer.ReadFrame(iss);
    ref<Expr> des_expr = deserializer.GetExpr(id);

    EXPECT_EQ(expr, des_expr);
  }

  void TestSerDeser(ref<Expr> expr) {
    TestSerDeser(expr, arrays);
  }

  ref<Expr> GetSimpleExpr(unsigned first=41, unsigned second=42,
                          unsigned third=2) {
    ref<Expr> expr = eb->Add(eb->Constant(first, Expr::Int8),
                             eb->Constant(second, Expr::Int8));
    expr = eb->UDiv(expr, eb->Constant(third, Expr::Int8));

    return expr;
  }

  ref<Expr> GetComplexExpr() {
    ref<Expr> expr = eb->Add(eb->Constant(0, Expr::Int8),
                             eb->Constant(1, Expr::Int8));
    expr = eb->Sub(expr, eb->Constant(2, Expr::Int8));
    expr = eb->Mul(expr, eb->Constant(3, Expr::Int8));
    expr = eb->UDiv(expr, eb->Constant(4, Expr::Int8));
    expr = eb->SDiv(expr, eb->Constant(5, Expr::Int8));
    expr = eb->URem(expr, eb->Constant(6, Expr::Int8));
    expr = eb->SRem(expr, eb->Constant(7, Expr::Int8));
    expr = eb->Not(expr);
    expr = eb->And(expr, eb->Constant(8, Expr::Int8));
    expr = eb->Or(expr, eb->Constant(9, Expr::Int8));
    expr = eb->Xor(expr, eb->Constant(10, Expr::Int8));
    expr = eb->Shl(expr, eb->Constant(11, Expr::Int8));
    expr = eb->LShr(expr, eb->Constant(12, Expr::Int8));
    expr = eb->AShr(expr, eb->Constant(13, Expr::Int8));
    expr = eb->Eq(expr, eb->Constant(14, Expr::Int8));
    expr = eb->Ne(expr, eb->Constant(15, Expr::Int8));
    expr = eb->Ult(expr, eb->Constant(16, Expr::Int8));
    expr = eb->Ule(expr, eb->Constant(17, Expr::Int8));
    expr = eb->Ugt(expr, eb->Constant(18, Expr::Int8));
    expr = eb->Uge(expr, eb->Constant(19, Expr::Int8));
    expr = eb->Slt(expr, eb->Constant(20, Expr::Int8));
    expr = eb->Sle(expr, eb->Constant(21, Expr::Int8));
    expr = eb->Sgt(expr, eb->Constant(22, Expr::Int8));
    expr = eb->Sge(expr, eb->Constant(23, Expr::Int8));

    return expr;
  }

  ref<Expr> GetDiamondShaped() {
    ref<Expr> common_expr = eb->Add(eb->Constant(0, Expr::Int8),
                                    eb->Constant(1, Expr::Int8));
    ref<Expr> not_expr = eb->Not(common_expr);
    ref<Expr> sub_expr = eb->Sub(common_expr, not_expr);

    return sub_expr;
  }

  ExprBuilder *eb;
  std::vector<Array*> arrays;
};


// Test simple expression structures (no DAG in the structure, no arrays)
TEST_F(SerDeserTest, BasicExpr) {
  ref<Expr> expr = GetComplexExpr();
  TestSerDeser(expr);
}

// Test various constants
TEST_F(SerDeserTest, ConstExpr) {
  // TODO
}


// Test expressions with a DAG structure.
TEST_F(SerDeserTest, SharedExpr) {
  ref<Expr> expr = GetDiamondShaped();
  TestSerDeser(expr);
}


// Test expressions involving reads from a symbolic array
TEST_F(SerDeserTest, ReadsSymbolic) {
  Array *array = new Array("test", 256);
  arrays.push_back(array);

  UpdateList ul(array, 0);

  ul.extend(GetSimpleExpr(41, 42, 43), GetSimpleExpr(3, 4, 5));
  ul.extend(GetSimpleExpr(15, 16, 17), GetSimpleExpr(8, 9, 10));
  ref<Expr> first_read = eb->Read(ul, GetSimpleExpr(18, 19, 20));

  ul.extend(GetSimpleExpr(49, 50, 51), GetSimpleExpr(19, 20, 21));
  ref<Expr> second_read = eb->Read(ul, GetSimpleExpr(20, 21, 22));

  ref<Expr> combine = eb->Add(first_read, second_read);
  TestSerDeser(combine);
}


// Test read expressions in a DAG
TEST_F(SerDeserTest, SharedReads) {
  // TODO
}

// Test the ser-deser of two identical update lists applied on different arrays
TEST_F(SerDeserTest, SameUpdateListDiffArrays) {
  // TODO
}

}
}
