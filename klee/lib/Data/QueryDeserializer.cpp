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

#include "klee/data/QueryDeserializer.h"
#include "klee/data/ExprDeserializer.h"
#include "klee/data/Support.h"
#include "klee/Solver.h"

#include <llvm/Support/raw_ostream.h>

#include "Queries.pb.h"


namespace klee {


QueryDeserializer::QueryDeserializer(ExprDeserializer &ed)
    : expr_deserializer_(ed) {
    root_ = make_shared<ConditionNode>();
}


QueryDeserializer::~QueryDeserializer() {

}


bool QueryDeserializer::Deserialize(const std::string &blob, Query &query) {
    data::QueryData query_data;
    if (!query_data.ParseFromString(blob)) {
        llvm::errs() << "QueryDeserializer: Invalid query frame." << '\n';
        return false;
    }

    expr_deserializer_.ReadFrame(query_data.expr_data());

    ConditionNodeRef seed = root_;
    if (query_data.has_parent_id()) {
        ConditionNodeMap::iterator it = nodes_.find(query_data.parent_id());
        assert(it != nodes_.end());
        seed = it->second;
    }

    // We add the constraints in reverse order
    for (int i = query_data.assert_expr_id_size() - 1; i >= 0; --i) {
        seed = seed->getOrCreate(
                expr_deserializer_.GetExpr(query_data.assert_expr_id(i)));
    }

    nodes_.insert(std::make_pair(query_data.id(), seed));

    query = Query(ConstraintManager(root_, seed),
            expr_deserializer_.GetExpr(query_data.expr_id()));

    return true;
}


}
