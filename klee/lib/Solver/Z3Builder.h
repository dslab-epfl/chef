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

#include <z3++.h>

#include <map>
#include <list>
#include <stack>

#include <boost/shared_ptr.hpp>

namespace klee {


class Z3Builder {
public:
    Z3Builder(z3::context &context);
    virtual ~Z3Builder();

    z3::context &context() {
        return context_;
    }

    z3::expr construct(ref<Expr> e) {
        // TODO: Should we clear the cache after each construction?
        return getOrMakeExpr(e);
    }

    virtual z3::expr getInitialRead(const Array *root, unsigned index) = 0;

protected:
    typedef ExprHashMap<z3::expr> ExprMap;

    z3::expr getOrMakeExpr(ref<Expr> e);
    z3::expr makeExpr(ref<Expr> e);
    virtual z3::expr makeReadExpr(ref<ReadExpr> re) = 0;

    z3::context &context_;
    ExprMap cons_expr_;
};


class Z3ArrayBuilder: public Z3Builder {
public:
    Z3ArrayBuilder(z3::context &context);
    virtual ~Z3ArrayBuilder();

    virtual z3::expr getInitialRead(const Array *root, unsigned index);
protected:
    virtual z3::expr makeReadExpr(ref<ReadExpr> re);
    virtual z3::expr initializeArray(const Array *root, z3::expr array_ast);

private:
    typedef llvm::DenseMap<const Array*, z3::expr> ArrayMap;
    typedef llvm::DenseMap<const UpdateNode*, z3::expr> UpdateListMap;

    z3::expr getArrayForUpdate(const Array *root, const UpdateNode *un);
    z3::expr getInitialArray(const Array *root);

    ArrayMap cons_arrays_;
    UpdateListMap cons_updates_;
};


class Z3AssertArrayBuilder : public Z3ArrayBuilder {
public:
    Z3AssertArrayBuilder(z3::solver &solver);
    virtual ~Z3AssertArrayBuilder();
protected:
    virtual z3::expr initializeArray(const Array *root, z3::expr array_ast);
private:
    z3::expr getArrayAssertion(const Array *root, z3::expr array_ast);
    z3::solver solver_;
};


class Z3IteBuilder : public Z3Builder {
public:
    Z3IteBuilder(z3::context &context);
    virtual ~Z3IteBuilder();

    virtual z3::expr getInitialRead(const Array *root, unsigned index);
protected:
    virtual z3::expr makeReadExpr(ref<ReadExpr> re);
private:
    typedef std::vector<z3::expr> ExprVector;
    typedef llvm::DenseMap<const Array*,
            boost::shared_ptr<ExprVector> > ArrayVariableMap;
    typedef llvm::DenseMap<std::pair<Z3_ast,
            std::pair<const Array*, const UpdateNode*> >, z3::expr> ReadMap;

    z3::expr getReadForArray(z3::expr index, const Array *root,
            const UpdateNode *un);
    z3::expr getReadForInitialArray(z3::expr index, const Array *root);

    boost::shared_ptr<ExprVector> getArrayValues(const Array *root);

    ArrayVariableMap array_variables_;
    ReadMap read_map_;
};


} /* namespace klee */

#endif /* Z3BUILDER_H_ */
