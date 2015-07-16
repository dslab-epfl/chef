/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2015, Dependable Systems Laboratory, EPFL
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

#ifndef KLEE_LIB_SOLVER_Z3ITEBUILDER_H_
#define KLEE_LIB_SOLVER_Z3ITEBUILDER_H_


#include "Z3Builder.h"


namespace klee {


class Z3IteBuilderCache : public Z3BuilderCache {

};


class Z3IteBuilder : public Z3Builder {
public:
    Z3IteBuilder(z3::context &context, Z3IteBuilderCache *cache);
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

#endif /* KLEE_LIB_SOLVER_Z3ITEBUILDER_H_ */
