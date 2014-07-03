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

namespace {

llvm::cl::opt<bool>
UseConstructHash("z3-use-hash-consing",
                 llvm::cl::desc("Use hash consing during Z3 query construction."),
                 llvm::cl::init(true));

}

namespace klee {


Z3Builder::Z3Builder(Z3_context context, Z3_solver solver, bool cons_initial_array)
    : context_(context),
      solver_(solver),
      cons_initial_array_(cons_initial_array) {

    domain_sort_ = Z3_mk_bv_sort(context_, 32);
    range_sort_ = Z3_mk_bv_sort(context_, 8);
    array_sort_ = Z3_mk_array_sort(context_, domain_sort_, range_sort_);

    arr_context_stack_.push(new ArrayList());
}


Z3Builder::~Z3Builder() {
    reset();
}


Z3_ast Z3Builder::getInitialRead(const Array *root, unsigned index) {
    return Z3_mk_select(context_,
            getInitialArray(root),
            Z3_mk_unsigned_int(context_, index, domain_sort_));
}


void Z3Builder::reset() {
    while (!arr_context_stack_.empty()) {
        delete arr_context_stack_.top();
        arr_context_stack_.pop();
    }

    cons_arrays_.clear();
    cons_expr_.clear();
    cons_updates_.clear();
}


void Z3Builder::push() {
    arr_context_stack_.push(new ArrayList());
}


void Z3Builder::pop(unsigned n) {
    while (n--) {
        ArrayList *al = arr_context_stack_.top();
        for (ArrayList::iterator it = al->begin(), ie = al->end();
                it != ie; ++it) {
            cons_arrays_.erase(*it);
        }
        delete al;
        arr_context_stack_.pop();
    }

    // FIXME: Implement proper memory management to avoid doing this
    cons_updates_.clear();
    cons_expr_.clear();
}


Z3_ast Z3Builder::getArrayForUpdate(const Array *root,
        const UpdateNode *un) {
    if (!un) {
        return getInitialArray(root);
    }

    UpdateListMap::iterator it = cons_updates_.find(un);
    if (it != cons_updates_.end()) {
        return it->second;
    }

    // TODO: Make non-recursive
    Z3_ast result = Z3_mk_store(context_,
            getArrayForUpdate(root, un->next),
            getOrMakeExpr(un->index),
            getOrMakeExpr(un->value));
    cons_updates_.insert(std::make_pair(un, result));
    return result;
}


Z3_ast Z3Builder::getInitialArray(const Array *root) {
    ArrayMap::iterator it = cons_arrays_.find(root);
    if (it != cons_arrays_.end()) {
        return it->second;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "%s_%p", root->name.c_str(), (void*)root);

    Z3_ast result = Z3_mk_const(context_,
            Z3_mk_string_symbol(context_, buf),
            array_sort_);

    if (root->isConstantArray()) {
        if (cons_initial_array_) {
            result = getArrayValuesAsCons(root, result);
        } else {
            Z3_solver_assert(context_, solver_,
                    getArrayValuesAsAssert(root, result));
        }
    }

    cons_arrays_.insert(std::make_pair(root, result));
    return result;
}


Z3_ast Z3Builder::getArrayValuesAsAssert(const Array *root, Z3_ast array_ast) {
    Z3_ast *assert_list = new Z3_ast[root->size];

    for (unsigned i = 0, e = root->size; i != e; ++i) {
        Z3_ast index = Z3_mk_unsigned_int(context_, i, domain_sort_);
        Z3_ast value = Z3_mk_unsigned_int(context_,
                root->constantValues[i]->getZExtValue(), range_sort_);
        assert_list[i] = Z3_mk_eq(context_,
                Z3_mk_select(context_, array_ast, index),
                value);
    }

    Z3_ast result = Z3_mk_and(context_, root->size, assert_list);
    delete [] assert_list;
    return result;
}


Z3_ast Z3Builder::getArrayValuesAsCons(const Array *root, Z3_ast array_ast) {
    Z3_ast result = array_ast;

    for (unsigned i = 0, e = root->size; i != e; ++i) {
        Z3_ast index = Z3_mk_unsigned_int(context_, i, domain_sort_);
        Z3_ast value = Z3_mk_unsigned_int(context_,
                root->constantValues[i]->getZExtValue(), range_sort_);
        result = Z3_mk_store(context_, result, index, value);
    }

    return result;
}


Z3_ast Z3Builder::getOrMakeExpr(ref<Expr> e) {
    if (!UseConstructHash || isa<ConstantExpr>(e)) {
        return makeExpr(e);
    }

    ExprHashMap<Z3_ast>::iterator it = cons_expr_.find(e);
    if (it != cons_expr_.end()) {
        return it->second;
    } else {
        Z3_ast result = makeExpr(e);
        cons_expr_.insert(std::make_pair(e, result));
        return result;
    }
}


Z3_ast Z3Builder::makeExpr(ref<Expr> e) {
    ++stats::queryConstructs;

    switch (e->getKind()) {
    case Expr::Constant: {
        ConstantExpr *CE = cast<ConstantExpr>(e);
        unsigned width = CE->getWidth();
        if (width == 1)
            return CE->isTrue() ? Z3_mk_true(context_) : Z3_mk_false(context_);

        if (width <= 32)
            return makeConst32(width, CE->getZExtValue(32));
        if (width <= 64)
            return makeConst64(width, CE->getZExtValue());

        // This is slower than concatenating 64-bit extractions, like STPBuilder
        // does, but the assumption is that it's quite infrequent.
        // TODO: Log these transformations.
        llvm::SmallString<32> const_repr;
        CE->getAPValue().toStringUnsigned(const_repr, 10);
        return Z3_mk_numeral(context_, const_repr.c_str(),
                Z3_mk_bv_sort(context_, width));
    }

    case Expr::NotOptimized: {
        NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
        return getOrMakeExpr(noe->src);
    }

    case Expr::Read: {
        ReadExpr *re = cast<ReadExpr>(e);
        return Z3_mk_select(context_,
                getArrayForUpdate(re->updates.root, re->updates.head),
                getOrMakeExpr(re->index));
    }

    case Expr::Select: {
        SelectExpr *se = cast<SelectExpr>(e);

        Z3_ast cond = getOrMakeExpr(se->cond);
        Z3_ast tExpr = getOrMakeExpr(se->trueExpr);
        Z3_ast fExpr = getOrMakeExpr(se->falseExpr);
        return Z3_mk_ite(context_, cond, tExpr, fExpr);
    }

    case Expr::Concat: {
        ConcatExpr *ce = cast<ConcatExpr>(e);

        unsigned numKids = ce->getNumKids();
        Z3_ast res = getOrMakeExpr(ce->getKid(numKids-1));
        for (int i = numKids - 2; i >= 0; --i) {
            res = Z3_mk_concat(context_, getOrMakeExpr(ce->getKid(i)), res);
        }
        return res;
    }

    case Expr::Extract: {
        ExtractExpr *ee = cast<ExtractExpr>(e);

        Z3_ast src = getOrMakeExpr(ee->expr);
        if (ee->getWidth() == 1) {
            return Z3_mk_eq(context_,
                    Z3_mk_extract(context_, ee->offset, ee->offset, src),
                    makeOne(1));
        } else {
            return Z3_mk_extract(context_,
                    ee->offset + ee->getWidth() - 1, ee->offset, src);
        }
    }

    // Casting

    case Expr::ZExt: {
        CastExpr *ce = cast<CastExpr>(e);

        Z3_ast src = getOrMakeExpr(ce->src);
        if (isBool(src)) {
            return Z3_mk_ite(context_,
                    src, makeOne(ce->getWidth()), makeZero(ce->getWidth()));
        } else {
            return Z3_mk_zero_ext(context_,
                    ce->getWidth() - getBVWidth(src), src);
        }
    }

    case Expr::SExt: {
        CastExpr *ce = cast<CastExpr>(e);

        Z3_ast src = getOrMakeExpr(ce->src);
        if (isBool(src)) {
            return Z3_mk_ite(context_,
                    src, makeOne(ce->getWidth()), makeZero(ce->getWidth()));
        } else {
            return Z3_mk_sign_ext(context_,
                    ce->getWidth() - getBVWidth(src), src);
        }
    }

    // Arithmetic

    case Expr::Add: {
        AddExpr *ae = cast<AddExpr>(e);

        Z3_ast left = getOrMakeExpr(ae->left);
        Z3_ast right = getOrMakeExpr(ae->right);
        assert(getBVWidth(right) != 1 && "uncanonicalized add");
        return Z3_mk_bvadd(context_, left, right);
    }

    case Expr::Sub: {
        SubExpr *se = cast<SubExpr>(e);

        Z3_ast left = getOrMakeExpr(se->left);
        Z3_ast right = getOrMakeExpr(se->right);
        assert(getBVWidth(right) != 1 && "uncanonicalized sub");
        // STP here takes an extra width parameter, wondering why...
        return Z3_mk_bvsub(context_, left, right);
    }

    case Expr::Mul: {
        MulExpr *me = cast<MulExpr>(e);

        Z3_ast left = getOrMakeExpr(me->left);
        Z3_ast right = getOrMakeExpr(me->right);
        assert(getBVWidth(right) != 1 && "uncanonicalized mul");
        // Again, we skip some optimizations in STPBuilder; just let the solver
        // do its own set of simplifications.
        return Z3_mk_bvmul(context_, left, right);
    }

    case Expr::UDiv: {
        UDivExpr *de = cast<UDivExpr>(e);

        Z3_ast left = getOrMakeExpr(de->left);
        assert(getBVWidth(left) != 1 && "uncanonicalized udiv");
        Z3_ast right = getOrMakeExpr(de->right);
        return Z3_mk_bvudiv(context_, left, right);
    }

    case Expr::SDiv: {
        SDivExpr *de = cast<SDivExpr>(e);

        Z3_ast left = getOrMakeExpr(de->left);
        assert(getBVWidth(left) != 1 && "uncanonicalized sdiv");
        Z3_ast right = getOrMakeExpr(de->right);
        return Z3_mk_bvsdiv(context_, left, right);
    }

    case Expr::URem: {
        URemExpr *de = cast<URemExpr>(e);

        Z3_ast left = getOrMakeExpr(de->left);
        assert(getBVWidth(left) != 1 && "uncanonicalized urem");
        Z3_ast right = getOrMakeExpr(de->right);
        return Z3_mk_bvurem(context_, left, right);
    }

    case Expr::SRem: {
        SRemExpr *de = cast<SRemExpr>(e);

        Z3_ast left = getOrMakeExpr(de->left);
        Z3_ast right = getOrMakeExpr(de->right);
        assert(getBVWidth(right) != 1 && "uncanonicalized srem");

        // Assuming the sign follows dividend (otherwise we should have used
        // the Z3_mk_bvsmod() call)
        return Z3_mk_bvsrem(context_, left, right);
    }

    // Bitwise

    case Expr::Not: {
        NotExpr *ne = cast<NotExpr>(e);

        Z3_ast expr = getOrMakeExpr(ne->expr);
        if (isBool(expr)) {
            return Z3_mk_not(context_, expr);
        } else {
            return Z3_mk_bvnot(context_, expr);
        }
    }

    case Expr::And: {
        AndExpr *ae = cast<AndExpr>(e);

        Z3_ast terms[2];
        terms[0] = getOrMakeExpr(ae->left);
        terms[1] = getOrMakeExpr(ae->right);

        if (isBool(terms[0])) {
            return Z3_mk_and(context_, 2, terms);
        } else {
            return Z3_mk_bvand(context_, terms[0], terms[1]);
        }
    }

    case Expr::Or: {
        OrExpr *oe = cast<OrExpr>(e);

        Z3_ast terms[2];
        terms[0] = getOrMakeExpr(oe->left);
        terms[1] = getOrMakeExpr(oe->right);

        if (isBool(terms[0])) {
            return Z3_mk_or(context_, 2, terms);
        } else {
            return Z3_mk_bvor(context_, terms[0], terms[1]);
        }
    }

    case Expr::Xor: {
        XorExpr *xe = cast<XorExpr>(e);

        Z3_ast left = getOrMakeExpr(xe->left);
        Z3_ast right = getOrMakeExpr(xe->right);

        if (isBool(left)) {
            return Z3_mk_xor(context_, left, right);
        } else {
            return Z3_mk_bvxor(context_, left, right);
        }
    }

    case Expr::Shl: {
        ShlExpr *se = cast<ShlExpr>(e);

        Z3_ast left = getOrMakeExpr(se->left);
        Z3_ast right = getOrMakeExpr(se->right);
        assert(getBVWidth(left) == getBVWidth(right));
        return Z3_mk_bvshl(context_, left, right);
    }

    case Expr::LShr: {
        LShrExpr *lse = cast<LShrExpr>(e);

        Z3_ast left = getOrMakeExpr(lse->left);
        Z3_ast right = getOrMakeExpr(lse->right);
        assert(getBVWidth(left) == getBVWidth(right));
        return Z3_mk_bvlshr(context_, left, right);
    }

    case Expr::AShr: {
        AShrExpr *ase = cast<AShrExpr>(e);

        Z3_ast left = getOrMakeExpr(ase->left);
        Z3_ast right = getOrMakeExpr(ase->right);
        assert(getBVWidth(left) == getBVWidth(right));
        return Z3_mk_bvashr(context_, left, right);
    }

    // Comparison

    case Expr::Eq: {
        EqExpr *ee = cast<EqExpr>(e);

        Z3_ast left = getOrMakeExpr(ee->left);
        Z3_ast right = getOrMakeExpr(ee->right);
        return Z3_mk_eq(context_, left, right);
    }

    case Expr::Ult: {
        UltExpr *ue = cast<UltExpr>(e);

        Z3_ast left = getOrMakeExpr(ue->left);
        Z3_ast right = getOrMakeExpr(ue->right);
        return Z3_mk_bvult(context_, left, right);
    }

    case Expr::Ule: {
        UleExpr *ue = cast<UleExpr>(e);

        Z3_ast left = getOrMakeExpr(ue->left);
        Z3_ast right = getOrMakeExpr(ue->right);
        return Z3_mk_bvule(context_, left, right);
    }

    case Expr::Slt: {
        SltExpr *se = cast<SltExpr>(e);

        Z3_ast left = getOrMakeExpr(se->left);
        Z3_ast right = getOrMakeExpr(se->right);
        return Z3_mk_bvslt(context_, left, right);
    }

    case Expr::Sle: {
        SleExpr *se = cast<SleExpr>(e);

        Z3_ast left = getOrMakeExpr(se->left);
        Z3_ast right = getOrMakeExpr(se->right);
        return Z3_mk_bvsle(context_, left, right);
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


Z3_ast Z3Builder::makeOne(unsigned width) {
    return Z3_mk_int(context_, 1, Z3_mk_bv_sort(context_, width));
}


Z3_ast Z3Builder::makeZero(unsigned width) {
    return Z3_mk_int(context_, 0, Z3_mk_bv_sort(context_, width));
}


Z3_ast Z3Builder::makeMinusOne(unsigned width) {
    return Z3_mk_int(context_, -1, Z3_mk_bv_sort(context_, width));
}


Z3_ast Z3Builder::makeConst32(unsigned width, uint32_t value) {
    assert(width <= 32);
    return Z3_mk_unsigned_int(context_, value,
            Z3_mk_bv_sort(context_, width));
}


Z3_ast Z3Builder::makeConst64(unsigned width, uint64_t value) {
    assert(width <= 64);
    return Z3_mk_unsigned_int64(context_, value,
            Z3_mk_bv_sort(context_, width));
}


} /* namespace klee */
