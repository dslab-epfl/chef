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

#ifndef Z3BUILDER_H_
#define Z3BUILDER_H_

#include "klee/Expr.h"
#include "klee/util/ExprHashMap.h"
#include "llvm/ADT/DenseMap.h"

#include <z3.h>

#include <boost/intrusive_ptr.hpp>

#include <map>
#include <list>
#include <stack>

namespace klee {


class Z3Builder {
public:
    Z3Builder(Z3_context context, Z3_solver solver, bool cons_initial_array);
    ~Z3Builder();

    Z3_context context() const {
        return context_;
    }

    Z3_solver solver() const {
        return solver_;
    }

    Z3_ast construct(ref<Expr> e) {
        // TODO: Should we clear the cache after each construction?
        return getOrMakeExpr(e);
    }

    Z3_ast getInitialRead(const Array *root, unsigned index);

    void reset();
    void push();
    void pop(unsigned n);

private:
    typedef llvm::DenseMap<const Array*, Z3_ast> ArrayMap;
    typedef llvm::DenseMap<const UpdateNode*, Z3_ast> UpdateListMap;

    typedef std::list<const Array*> ArrayList;
    typedef std::stack<ArrayList*> ArrayContextStack;

    Z3_ast getOrMakeExpr(ref<Expr> e);
    Z3_ast makeExpr(ref<Expr> e);

    Z3_ast getArrayForUpdate(const Array *root, const UpdateNode *un);
    Z3_ast getInitialArray(const Array *root);
    Z3_ast getArrayValuesAsAssert(const Array *root, Z3_ast array_ast);
    Z3_ast getArrayValuesAsCons(const Array *root, Z3_ast array_ast);

    Z3_ast makeOne(unsigned width);
    Z3_ast makeZero(unsigned width);
    Z3_ast makeMinusOne(unsigned width);
    Z3_ast makeConst32(unsigned width, uint32_t value);
    Z3_ast makeConst64(unsigned width, uint64_t value);


    unsigned getBVWidth(Z3_ast expr) {
        Z3_sort sort = Z3_get_sort(context_, expr);
        assert(Z3_get_sort_kind(context_, sort) == Z3_BV_SORT);
        return Z3_get_bv_sort_size(context_, sort);
    }

    bool isBool(Z3_ast expr) {
        return Z3_get_sort_kind(context_,
                Z3_get_sort(context_, expr)) == Z3_BOOL_SORT;
    }

    Z3_context context_;
    Z3_solver solver_;

    Z3_sort domain_sort_;
    Z3_sort range_sort_;
    Z3_sort array_sort_;

    bool cons_initial_array_;

    ArrayMap cons_arrays_;
    ExprHashMap<Z3_ast> cons_expr_;
    UpdateListMap cons_updates_;

    ArrayContextStack arr_context_stack_;
};

} /* namespace klee */

#endif /* Z3BUILDER_H_ */
