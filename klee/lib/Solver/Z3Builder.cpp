/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2014, Dependable Systems Laboratory, EPFL
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

#include "Z3Builder.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CommandLine.h"

#include "klee/SolverStats.h"

#include <boost/make_shared.hpp>

namespace {

// TODO: Make this a solver factory option and factor it out
llvm::cl::opt<bool>
UseConstructHash("z3-use-hash-consing",
                 llvm::cl::desc("Use hash consing during Z3 query construction."),
                 llvm::cl::init(true));

}

using boost::shared_ptr;
using boost::make_shared;

namespace klee {


Z3Builder::Z3Builder(z3::context &context)
    : context_(context) {
}


Z3Builder::~Z3Builder() {

}


z3::expr Z3Builder::getOrMakeExpr(ref<Expr> e) {
    if (!UseConstructHash || isa<ConstantExpr>(e)) {
        return makeExpr(e);
    }

    ExprMap::iterator it = cons_expr_.find(e);
    if (it != cons_expr_.end()) {
        return it->second;
    } else {
        z3::expr result = makeExpr(e);
        cons_expr_.insert(std::make_pair(e, result));
        return result;
    }
}


z3::expr Z3Builder::makeExpr(ref<Expr> e) {
    ++stats::queryConstructs;

    switch (e->getKind()) {
    case Expr::Constant: {
        ConstantExpr *CE = cast<ConstantExpr>(e);
        unsigned width = CE->getWidth();
        if (width == 1)
            return context_.bool_val(CE->isTrue());
        if (width <= 64)
            return context_.bv_val((__uint64)CE->getZExtValue(), width);

        // This is slower than concatenating 64-bit extractions, like STPBuilder
        // does, but the assumption is that it's quite infrequent.
        // TODO: Log these transformations.
        llvm::SmallString<32> const_repr;
        CE->getAPValue().toStringUnsigned(const_repr, 10);
        return context_.bv_val(const_repr.c_str(), width);
    }

    case Expr::NotOptimized: {
        NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
        return getOrMakeExpr(noe->src);
    }

    case Expr::Read: {
        return makeReadExpr(cast<ReadExpr>(e));
    }

    case Expr::Select: {
        SelectExpr *se = cast<SelectExpr>(e);
        // XXX: A bug in Clang prevents us from using z3::ite
        return z3::to_expr(context_, Z3_mk_ite(context_,
                getOrMakeExpr(se->cond),
                getOrMakeExpr(se->trueExpr),
                getOrMakeExpr(se->falseExpr)));
    }

    case Expr::Concat: {
        ConcatExpr *ce = cast<ConcatExpr>(e);

        unsigned numKids = ce->getNumKids();
        z3::expr res = getOrMakeExpr(ce->getKid(numKids-1));
        for (int i = numKids - 2; i >= 0; --i) {
            res = z3::to_expr(context_,
                    Z3_mk_concat(context_, getOrMakeExpr(ce->getKid(i)), res));
        }
        return res;
    }

    case Expr::Extract: {
        ExtractExpr *ee = cast<ExtractExpr>(e);

        z3::expr src = getOrMakeExpr(ee->expr);
        if (ee->getWidth() == 1) {
            return z3::to_expr(context_, Z3_mk_extract(context_,
                    ee->offset, ee->offset, src)) == context_.bv_val(1, 1);
        } else {
            return z3::to_expr(context_, Z3_mk_extract(context_,
                    ee->offset + ee->getWidth() - 1, ee->offset, src));
        }
    }

    // Casting

    case Expr::ZExt: {
        CastExpr *ce = cast<CastExpr>(e);

        z3::expr src = getOrMakeExpr(ce->src);
        if (src.is_bool()) {
            // XXX: A bug in Clang prevents us from using z3::ite
            return z3::to_expr(context_, Z3_mk_ite(context_,
                    src,
                    context_.bv_val(1, ce->getWidth()),
                    context_.bv_val(0, ce->getWidth())));
        } else {
            return z3::to_expr(context_, Z3_mk_zero_ext(context_,
                    ce->getWidth() - src.get_sort().bv_size(), src));
        }
    }

    case Expr::SExt: {
        CastExpr *ce = cast<CastExpr>(e);

        z3::expr src = getOrMakeExpr(ce->src);
        if (src.is_bool()) {
            return z3::to_expr(context_, Z3_mk_ite(context_,
                    src,
                    context_.bv_val(1, ce->getWidth()),
                    context_.bv_val(0, ce->getWidth())));
        } else {
            return z3::to_expr(context_, Z3_mk_sign_ext(context_,
                    ce->getWidth() - src.get_sort().bv_size(), src));
        }
    }

    // Arithmetic

    case Expr::Add: {
        AddExpr *ae = cast<AddExpr>(e);
        return getOrMakeExpr(ae->left) + getOrMakeExpr(ae->right);
    }

    case Expr::Sub: {
        SubExpr *se = cast<SubExpr>(e);

        // STP here takes an extra width parameter, wondering why...
        return getOrMakeExpr(se->left) - getOrMakeExpr(se->right);
    }

    case Expr::Mul: {
        MulExpr *me = cast<MulExpr>(e);

        // Again, we skip some optimizations from STPBuilder; just let the solver
        // do its own set of simplifications.
        return getOrMakeExpr(me->left) * getOrMakeExpr(me->right);
    }

    case Expr::UDiv: {
        UDivExpr *de = cast<UDivExpr>(e);
        return z3::udiv(getOrMakeExpr(de->left), getOrMakeExpr(de->right));
    }

    case Expr::SDiv: {
        SDivExpr *de = cast<SDivExpr>(e);
        return getOrMakeExpr(de->left) / getOrMakeExpr(de->right);
    }

    case Expr::URem: {
        URemExpr *de = cast<URemExpr>(e);
        return z3::to_expr(context_, Z3_mk_bvurem(context_,
                getOrMakeExpr(de->left),
                getOrMakeExpr(de->right)));
    }

    case Expr::SRem: {
        SRemExpr *de = cast<SRemExpr>(e);

        // Assuming the sign follows dividend (otherwise we should have used
        // the Z3_mk_bvsmod() call)
        return z3::to_expr(context_, Z3_mk_bvsrem(context_,
                getOrMakeExpr(de->left),
                getOrMakeExpr(de->right)));
    }

    // Bitwise

    case Expr::Not: {
        NotExpr *ne = cast<NotExpr>(e);

        z3::expr expr = getOrMakeExpr(ne->expr);
        if (expr.is_bool()) {
            return !expr;
        } else {
            return ~expr;
        }
    }

    case Expr::And: {
        AndExpr *ae = cast<AndExpr>(e);

        z3::expr left = getOrMakeExpr(ae->left);
        z3::expr right = getOrMakeExpr(ae->right);

        if (left.is_bool()) {
            return left && right;
        } else {
            return left & right;
        }
    }

    case Expr::Or: {
        OrExpr *oe = cast<OrExpr>(e);

        z3::expr left = getOrMakeExpr(oe->left);
        z3::expr right = getOrMakeExpr(oe->right);

        if (left.is_bool()) {
            return left || right;
        } else {
            return left | right;
        }
    }

    case Expr::Xor: {
        XorExpr *xe = cast<XorExpr>(e);

        z3::expr left = getOrMakeExpr(xe->left);
        z3::expr right = getOrMakeExpr(xe->right);

        if (left.is_bool()) {
            return z3::to_expr(context_, Z3_mk_xor(context_, left, right));
        } else {
            return left ^ right;
        }
    }

    case Expr::Shl: {
        ShlExpr *se = cast<ShlExpr>(e);
        return z3::to_expr(context_, Z3_mk_bvshl(context_,
                getOrMakeExpr(se->left),
                getOrMakeExpr(se->right)));
    }

    case Expr::LShr: {
        LShrExpr *lse = cast<LShrExpr>(e);
        return z3::to_expr(context_, Z3_mk_bvlshr(context_,
                getOrMakeExpr(lse->left),
                getOrMakeExpr(lse->right)));
    }

    case Expr::AShr: {
        AShrExpr *ase = cast<AShrExpr>(e);
        return z3::to_expr(context_, Z3_mk_bvashr(context_,
                getOrMakeExpr(ase->left),
                getOrMakeExpr(ase->right)));
    }

    // Comparison

    case Expr::Eq: {
        EqExpr *ee = cast<EqExpr>(e);
        return getOrMakeExpr(ee->left) == getOrMakeExpr(ee->right);
    }

    case Expr::Ult: {
        UltExpr *ue = cast<UltExpr>(e);
        return z3::ult(getOrMakeExpr(ue->left), getOrMakeExpr(ue->right));
    }

    case Expr::Ule: {
        UleExpr *ue = cast<UleExpr>(e);
        return z3::ule(getOrMakeExpr(ue->left), getOrMakeExpr(ue->right));
    }

    case Expr::Slt: {
        SltExpr *se = cast<SltExpr>(e);
        return getOrMakeExpr(se->left) < getOrMakeExpr(se->right);
    }

    case Expr::Sle: {
        SleExpr *se = cast<SleExpr>(e);
        return getOrMakeExpr(se->left) <= getOrMakeExpr(se->right);
    }

    // unused due to canonicalization
#if 0
    case Expr::Ne:
    case Expr::Ugt:
    case Expr::Uge:
    case Expr::Sgt:
    case Expr::Sge:
#endif

    default:
        assert(0 && "unhandled Expr type");
    }
}

/* Z3ArrayBuilder ------------------------------------------------------------*/

Z3ArrayBuilder::Z3ArrayBuilder(z3::context &context)
    : Z3Builder(context) {

}


Z3ArrayBuilder::~Z3ArrayBuilder() {

}


z3::expr Z3ArrayBuilder::getInitialRead(const Array *root, unsigned index) {
    return z3::select(getInitialArray(root), context_.bv_val(index, 32));
}


z3::expr Z3ArrayBuilder::makeReadExpr(ref<ReadExpr> re) {
    return z3::select(getArrayForUpdate(re->updates.root, re->updates.head),
            getOrMakeExpr(re->index));
}


z3::expr Z3ArrayBuilder::getArrayForUpdate(const Array *root,
        const UpdateNode *un) {
    if (!un) {
        return getInitialArray(root);
    }

    UpdateListMap::iterator it = cons_updates_.find(un);
    if (it != cons_updates_.end()) {
        return it->second;
    }

    // TODO: Make non-recursive
    z3::expr result = z3::store(getArrayForUpdate(root, un->next),
            getOrMakeExpr(un->index), getOrMakeExpr(un->value));
    cons_updates_.insert(std::make_pair(un, result));
    return result;
}


z3::expr Z3ArrayBuilder::getInitialArray(const Array *root) {
    ArrayMap::iterator it = cons_arrays_.find(root);
    if (it != cons_arrays_.end()) {
        return it->second;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "%s_%p", root->name.c_str(), (void*)root);

    z3::expr result = context_.constant(buf,
            context_.array_sort(context_.bv_sort(32), context_.bv_sort(8)));
    if (root->isConstantArray()) {
        result = initializeArray(root, result);
    }
    cons_arrays_.insert(std::make_pair(root, result));
    return result;
}


z3::expr Z3ArrayBuilder::initializeArray(const Array *root, z3::expr array_ast) {
    z3::expr result = array_ast;
    for (unsigned i = 0, e = root->size; i != e; ++i) {
        z3::expr index = context_.bv_val(i, 32);
        z3::expr value = context_.bv_val(
                (unsigned)root->constantValues[i]->getZExtValue(), 8);
        result = z3::store(result, index, value);
    }
    return result;
}


/* Z3AssertArrayBuilder ------------------------------------------------------*/

Z3AssertArrayBuilder::Z3AssertArrayBuilder(z3::solver &solver)
    : Z3ArrayBuilder(solver.ctx()),
      solver_(solver) {

}


Z3AssertArrayBuilder::~Z3AssertArrayBuilder() {

}


z3::expr Z3AssertArrayBuilder::initializeArray(const Array *root,
        z3::expr array_ast) {
    solver_.add(getArrayAssertion(root, array_ast));
    return array_ast;
}

z3::expr Z3AssertArrayBuilder::getArrayAssertion(const Array *root,
        z3::expr array_ast) {
    z3::expr result = context_.bool_val(true);
    for (unsigned i = 0, e = root->size; i != e; ++i) {
        z3::expr array_read = z3::select(array_ast, context_.bv_val(i, 32));
        z3::expr array_value = context_.bv_val(
                (unsigned)root->constantValues[i]->getZExtValue(), 8);

        result = result && (array_read == array_value);
    }
    return result;
}

/* Z3IteBuilder --------------------------------------------------------------*/

Z3IteBuilder::Z3IteBuilder(z3::context &context)
    : Z3Builder(context) {

}

Z3IteBuilder::~Z3IteBuilder() {

}

z3::expr Z3IteBuilder::getInitialRead(const Array *root, unsigned index) {
    shared_ptr<ExprVector> elem_vector = getArrayValues(root);
    return (*elem_vector)[index];
}

z3::expr Z3IteBuilder::makeReadExpr(ref<ReadExpr> re) {
    return getReadForArray(getOrMakeExpr(re->index), re->updates.root,
            re->updates.head);
}


z3::expr Z3IteBuilder::getReadForArray(z3::expr index, const Array *root,
            const UpdateNode *un) {
    ReadMap::iterator it = read_map_.find(std::make_pair(index, un));
    if (it != read_map_.end()) {
        return it->second;
    }

    z3::expr result(context_);

    if (!un) {
        result = getReadForInitialArray(index, root);
    } else {
        result = z3::to_expr(context_, Z3_mk_ite(context_,
                index == getOrMakeExpr(un->index),
                getOrMakeExpr(un->value),
                getReadForArray(index, root, un->next)));
    }
    read_map_.insert(std::make_pair(std::make_pair(index, un), result));
    return result;
}


z3::expr Z3IteBuilder::getReadForInitialArray(z3::expr index, const Array *root) {
    shared_ptr<ExprVector> elem_vector = getArrayValues(root);

    // TODO: balance this tree
    z3::expr ite_tree = context_.bv_val(0, 8);
    for (unsigned i = 0, e = root->size; i != e; ++i) {
        ite_tree = z3::to_expr(context_, Z3_mk_ite(context_,
                index == context_.bv_val(i, 32),
                (*elem_vector)[i],
                ite_tree));
    }
    return ite_tree;
}

shared_ptr<Z3IteBuilder::ExprVector> Z3IteBuilder::getArrayValues(const Array *root) {
    ArrayVariableMap::iterator it = array_variables_.find(root);
    if (it != array_variables_.end()) {
        return it->second;
    }

    shared_ptr<ExprVector> elem_vector = make_shared<ExprVector>();

    if (root->isConstantArray()) {
        for (unsigned i = 0, e = root->size; i != e; ++i) {
            elem_vector->push_back(context_.bv_val(
                    (unsigned)root->constantValues[i]->getZExtValue(), 8));
        }
    } else {
        char buf[256];
        for (unsigned i = 0, e = root->size; i != e; ++i) {
            snprintf(buf, sizeof(buf), "%s_%p_%u",
                    root->name.c_str(), (void*)root, i);
            elem_vector->push_back(context_.bv_const(buf, 8));
        }
    }

    array_variables_.insert(std::make_pair(root, elem_vector));
    return elem_vector;
}

} /* namespace klee */
