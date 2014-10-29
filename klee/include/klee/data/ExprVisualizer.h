/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
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
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#ifndef KLEE_DATA_EXPRVISUALIZER_H_
#define KLEE_DATA_EXPRVISUALIZER_H_

#include "klee/Expr.h"

#include "klee/util/ExprHashMap.h"
#include "klee/util/ExprVisitor.h"

#include <string>
#include <map>
#include <set>
#include <stack>

#include <boost/shared_ptr.hpp>

namespace llvm {
class raw_ostream;
}

namespace klee {

typedef std::map<std::string, std::string> GraphvizProperties;

struct ExprGraphvizNode {
    ExprGraphvizNode(const std::string &node_name) : name(node_name) {

    }

    std::string name;
    GraphvizProperties properties;
    std::vector<std::pair<std::string, GraphvizProperties> > edges;
};

typedef boost::shared_ptr<ExprGraphvizNode> ExprGraphvizNodeRef;


class ExprVisualizer {
public:
    ExprVisualizer();
    ~ExprVisualizer();

    ExprGraphvizNodeRef getOrCreateNode(const std::string &name);
    ExprGraphvizNodeRef createNode();

    void draw(llvm::raw_ostream &os);
private:
    typedef std::map<std::string, ExprGraphvizNodeRef> NodeMap;
    NodeMap nodes_;
    std::vector<std::string> node_order_;
    uint64_t next_expr_id_;

    void drawProperties(llvm::raw_ostream &os,
            const GraphvizProperties &properties);
};


class ExprDotDecorator {
public:
    ExprDotDecorator() { }
    virtual ~ExprDotDecorator() { }

    virtual std::string GetExprKindLabel(const ref<Expr> expr);
    virtual std::string GetConstantLabel(const ref<Expr> expr);

    virtual void decorateExpr(const ref<Expr> expr, ExprGraphvizNodeRef node) = 0;
    virtual void decorateArray(const Array *array, ExprGraphvizNodeRef node) = 0;
};


class DefaultExprDotDecorator: public ExprDotDecorator {
public:
  DefaultExprDotDecorator() { }

  virtual void decorateExpr(const ref<Expr> expr, ExprGraphvizNodeRef node);
  virtual void decorateArray(const Array *array, ExprGraphvizNodeRef node);
private:
  void decorateExprNode(const ref<Expr> expr, ExprGraphvizNodeRef node);
  void decorateExprEdges(const ref<Expr> expr, ExprGraphvizNodeRef node);
};


class ExprArtist {
public:
    ExprArtist(ExprVisualizer &visualizer, ExprDotDecorator &decorator);
    ~ExprArtist();

    void drawExpr(ref<Expr> expr);
    void highlightExpr(ref<Expr> expr, const std::string &label);
private:
    typedef ExprHashMap<std::string> ConsNodesMap;
    typedef std::map<const Array*, std::string> ConsArraysMap;
    typedef UpdateListHashMap<std::string> ConsUpdatesMap;

    ExprVisualizer &visualizer_;
    ExprDotDecorator &decorator_;

    ConsNodesMap cons_nodes_;
    ConsArraysMap cons_arrays_;
    ConsUpdatesMap cons_updates_;

    ExprGraphvizNodeRef getOrCreateExpr(ref<Expr> expr);
    ExprGraphvizNodeRef getOrCreateUpdate(const UpdateList &ul);
    ExprGraphvizNodeRef getOrCreateArray(const Array* array);
};

}


#endif /* KLEE_DATA_EXPRVISUALIZER_H_ */
