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

#include "klee/data/QuerySerializer.h"
#include "klee/data/ExprSerializer.h"
#include "klee/data/Support.h"
#include "klee/Solver.h"

#include "Queries.pb.h"

namespace klee {


QuerySerializer::QuerySerializer(ExprSerializer &es)
        : next_id_(1),
          es_(es) {

}

QuerySerializer::~QuerySerializer() {

}


std::pair<uint64_t, uint64_t> QuerySerializer::Serialize(const Query &query,
        std::string &blob) {
    data::QueryData query_data;

    ExprFrame expr_frame(es_, query_data.mutable_expr_data());

    query_data.set_id(next_id_++);
    query_data.set_expr_id(expr_frame.RecordExpr(query.expr));

    // XXX: Not really incremental, might happen that two queries
    // share the same prefix.
    for (ConditionNodeRef node = query.constraints.head(),
            root = query.constraints.root(); node != root;
            node = node->parent()) {
        ConditionNodeMap::iterator it = serialized_nodes_.find(node);
        if (it != serialized_nodes_.end()) {
            query_data.set_parent_id(it->second);
            break;
        }
        query_data.add_assert_expr_id(expr_frame.RecordExpr(node->expr()));
    }
    // This is a no-op if the node is already there
    serialized_nodes_.insert(std::make_pair(query.constraints.head(),
            query_data.id()));

    blob.clear();
    query_data.SerializeToString(&blob);

    return std::make_pair(query_data.id(),
            query_data.has_parent_id() ? query_data.parent_id() : 0);
}


}
