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

#include "klee/data/ExprVisualizer.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>

#include <boost/make_shared.hpp>

#include <sstream>
#include <stack>
#include <cstdio>

using namespace llvm;

namespace {
cl::opt<unsigned> ArrayWrapSize("array-wrap-size",
                           cl::desc("The maximum width of an array when visualizing"),
                           cl::init(32));

cl::opt<bool> ShortcutConstReads("shortcut-const-reads",
                                 cl::desc("Make const read accesses point to the array cell"),
                                 cl::init(false));
}

namespace klee {

////////////////////////////////////////////////////////////////////////////////
// ExprVisualizer
////////////////////////////////////////////////////////////////////////////////


ExprVisualizer::ExprVisualizer()
    : next_expr_id_(0) {

}

ExprVisualizer::~ExprVisualizer() {

}


ExprGraphvizNodeRef ExprVisualizer::getOrCreateNode(const std::string &name) {
    NodeMap::iterator it = nodes_.find(name);
    if (it != nodes_.end())
        return it->second;

    ExprGraphvizNodeRef node = boost::make_shared<ExprGraphvizNode>(name);
    nodes_.insert(std::make_pair(name, node));
    node_order_.push_back(name);

    return node;
}


ExprGraphvizNodeRef ExprVisualizer::createNode() {
    char name_buffer[16];
    do {
        snprintf(name_buffer, 16, "E%lu", next_expr_id_++);
    } while (nodes_.count(std::string(name_buffer)) > 0);

    return getOrCreateNode(std::string(name_buffer));
}


void ExprVisualizer::draw(llvm::raw_ostream &os) {
    os << "digraph expr {" << '\n';
    for (std::vector<std::string>::iterator it = node_order_.begin(),
            ie = node_order_.end(); it != ie; ++it) {
        ExprGraphvizNodeRef node = nodes_[*it];
        os.indent(4);
        os << node->name;
        drawProperties(os, node->properties);
        os << ";" << '\n';

        for (unsigned i = 0; i < node->edges.size(); ++i) {
            os.indent(4);
            os << node->name << " -> " << node->edges[i].first;
            drawProperties(os, node->edges[i].second);
            os << ";" << '\n';
        }
        os << '\n';
    }
    os << "}" << '\n';
}


void ExprVisualizer::drawProperties(llvm::raw_ostream &os,
        const GraphvizProperties &properties) {
    if (!properties.empty()) {
        os << " [";
        for (GraphvizProperties::const_iterator it = properties.begin(),
                ie = properties.end(); it != ie; ++it) {
            if (it != properties.begin())
                os << ",";
            os << it->first << "=" << '"' << it->second << '"';
        }
        os << "]";
    }
}


////////////////////////////////////////////////////////////////////////////////
// ExprDotDecorator
////////////////////////////////////////////////////////////////////////////////


std::string ExprDotDecorator::GetConstantLabel(const ref<Expr> expr) {
  ConstantExpr *ce = cast<ConstantExpr>(expr);

  std::string label;
  llvm::raw_string_ostream oss(label);
  oss << ce->getWidth() << " : " << format("0x%lx", ce->getZExtValue());

  return oss.str();
}


std::string ExprDotDecorator::GetExprKindLabel(const ref<Expr> expr) {
  switch (expr->getKind()) {
  case Expr::Constant:
    return "CONST";
  case Expr::NotOptimized:
    return "NOPT";
  case Expr::Read:
    return "READ";
  case Expr::Select:
    return "SEL";
  case Expr::Concat:
    return "CNCT";
  case Expr::Extract:
    return "XTCT";

    // Casting
  case Expr::ZExt:
    return "ZEXT";
  case Expr::SExt:
    return "SEXT";

    // Arithmetic
  case Expr::Add:
    return "ADD";
  case Expr::Sub:
    return "SUB";
  case Expr::Mul:
    return "MUL";
  case Expr::UDiv:
    return "UDIV";
  case Expr::SDiv:
    return "SDIV";
  case Expr::URem:
    return "UREM";
  case Expr::SRem:
    return "SREM";

    // Bit
  case Expr::Not:
    return "NOT";
  case Expr::And:
    return "AND";
  case Expr::Or:
    return "OR";
  case Expr::Xor:
    return "XOR";
  case Expr::Shl:
    return "SHL";
  case Expr::LShr:
    return "LSHR";
  case Expr::AShr:
    return "ASHR";

    // Compare
  case Expr::Eq:
    return "EQ";
  case Expr::Ne:  ///< Not used in canonical form
    return "NE";
  case Expr::Ult:
    return "ULT";
  case Expr::Ule:
    return "ULE";
  case Expr::Ugt: ///< Not used in canonical form
    return "UGT";
  case Expr::Uge: ///< Not used in canonical form
    return "UGE";
  case Expr::Slt:
    return "SLT";
  case Expr::Sle:
    return "SLE";
  case Expr::Sgt: ///< Not used in canonical form
    return "SGT";
  case Expr::Sge:
    return "SGE";

  default:
    assert(0 && "Unhandled Expr type");
  }
}


////////////////////////////////////////////////////////////////////////////////
// ExprArtist
////////////////////////////////////////////////////////////////////////////////

ExprArtist::ExprArtist(ExprVisualizer &visualizer, ExprDotDecorator &decorator)
    : visualizer_(visualizer),
      decorator_(decorator) {
    ExprGraphvizNodeRef graph_node = visualizer_.getOrCreateNode("graph");
    graph_node->properties["fontname"] = "Helvetica";
    graph_node->properties["nslimit"] = "20";
    graph_node->properties["splines"] = "false";
}

ExprArtist::~ExprArtist() {

}


void ExprArtist::drawExpr(ref<Expr> expr) {
    getOrCreateExpr(expr);
}

void ExprArtist::highlightExpr(ref<Expr> expr, const std::string &label) {
    ExprGraphvizNodeRef node = getOrCreateExpr(expr);
    node->properties["color"] = "red";
    node->properties["xlabel"] = label;
}


ExprGraphvizNodeRef ExprArtist::getOrCreateExpr(ref<Expr> expr) {
    ExprGraphvizNodeRef node;
    if (isa<ConstantExpr>(expr)) {
        node = visualizer_.createNode();
    } else {
        ConsNodesMap::iterator it = cons_nodes_.find(expr);
        if (it != cons_nodes_.end()) {
            return visualizer_.getOrCreateNode(it->second);
        }
        node = visualizer_.createNode();
        cons_nodes_.insert(std::make_pair(expr, node->name));
    }

    for (unsigned i = 0; i < expr->getNumKids(); ++i) {
        ExprGraphvizNodeRef kid_node = getOrCreateExpr(expr->getKid(i));
        node->edges.push_back(std::make_pair(
                kid_node->name, GraphvizProperties()));
    }
    decorator_.decorateExpr(expr, node);

    if (expr->getKind() == Expr::Read) {
        ReadExpr *re = cast<ReadExpr>(expr);

        if (ShortcutConstReads && !re->updates.head && isa<ConstantExpr>(re->index)) {
            ExprGraphvizNodeRef array_node = getOrCreateArray(re->updates.root);

            ConstantExpr *ce = cast<ConstantExpr>(re->index);
            std::string cell_name;
            llvm::raw_string_ostream os(cell_name);
            os << array_node->name << ":"
                    << re->updates.root->name << "_" << ce->getZExtValue();

            node->edges.push_back(std::make_pair(os.str(), GraphvizProperties()));
        } else {
#if 0
            ExprGraphvizNodeRef next_node = getOrCreateUpdate(re->updates);
#else
            ExprGraphvizNodeRef next_node = getOrCreateArray(re->updates.root);
#endif
            node->edges.push_back(std::make_pair(next_node->name, GraphvizProperties()));
        }
    }

    return node;
}

ExprGraphvizNodeRef ExprArtist::getOrCreateUpdate(const UpdateList &ul) {
    ConsUpdatesMap::iterator it = cons_updates_.find(ul);
    if (it != cons_updates_.end()) {
        return visualizer_.getOrCreateNode(it->second);
    }
    ExprGraphvizNodeRef node = visualizer_.createNode();
    cons_updates_.insert(std::make_pair(ul, node->name));

    ExprGraphvizNodeRef next_node;
    if (ul.head->next) {
        next_node = getOrCreateUpdate(UpdateList(ul.root,
                ul.head->next));
    } else {
        next_node = getOrCreateArray(ul.root);
    }
    node->edges.push_back(std::make_pair(next_node->name, GraphvizProperties()));

    ExprGraphvizNodeRef index_node = getOrCreateExpr(ul.head->index);
    ExprGraphvizNodeRef value_node = getOrCreateExpr(ul.head->value);
    node->edges.push_back(std::make_pair(index_node->name, GraphvizProperties()));
    node->edges.push_back(std::make_pair(value_node->name, GraphvizProperties()));

    return node;
}

ExprGraphvizNodeRef ExprArtist::getOrCreateArray(const Array* array) {
    ConsArraysMap::iterator it = cons_arrays_.find(array);
    if (it != cons_arrays_.end()) {
        return visualizer_.getOrCreateNode(it->second);
    }

    ExprGraphvizNodeRef node = visualizer_.createNode();
    cons_arrays_.insert(std::make_pair(array, node->name));

    decorator_.decorateArray(array, node);
    return node;
}


////////////////////////////////////////////////////////////////////////////////
// Decorators
////////////////////////////////////////////////////////////////////////////////


void DefaultExprDotDecorator::decorateExprNode(const ref<Expr> expr,
        ExprGraphvizNodeRef node) {

  node->properties["shape"] = "circle";
  node->properties["margin"] = "0";

  switch (expr->getKind()) {
  case Expr::Constant:
    node->properties["label"] = GetConstantLabel(expr);
    node->properties["shape"] = "box";
    node->properties["style"] = "filled";
    node->properties["fillcolor"] = "lightgray";
    break;
  case Expr::Read:
    node->properties["label"] = GetExprKindLabel(expr);
    node->properties["shape"] = "box";
    break;
  case Expr::ZExt:
  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(expr);
    std::ostringstream oss;
    oss << GetExprKindLabel(expr) << "\\n[" << ce->getWidth() << "]";
    node->properties["label"] = oss.str();
    break;
  }
  case Expr::Select:
    node->properties["label"] = GetExprKindLabel(expr);
    node->properties["style"] = "filled";
    node->properties["fillcolor"] = "lightyellow";
    break;
  default:
    node->properties["label"] = GetExprKindLabel(expr);
    break;
  }
}

void DefaultExprDotDecorator::decorateExprEdges(const ref<Expr> expr,
      ExprGraphvizNodeRef node) {
  for (unsigned i = 0; i < expr->getNumKids(); ++i) {
      node->edges[i].second["fontsize"] = "10.0";
  }

  switch (expr->getKind()) {
  case Expr::Constant:
    break;
  case Expr::NotOptimized:
    node->edges[0].second["label"] = "expr";
    break;
  case Expr::Read:
    node->edges[0].second["label"] = "index";
    node->edges[0].second["style"] = "dotted";
    break;
  case Expr::Select:
    node->edges[0].second["label"] = "cond";
    node->edges[0].second["style"] = "dotted";
    node->edges[1].second["label"] = "true";
    node->edges[1].second["color"] = "green";
    node->edges[2].second["label"] = "false";
    node->edges[2].second["color"] = "red";
    break;
  case Expr::Concat: {
    ConcatExpr *ce = cast<ConcatExpr>(expr);
    for (unsigned i = 0; i < ce->getNumKids(); ++i) {
      std::ostringstream oss;
      oss << i;
      node->edges[i].second["label"] = oss.str();
    }
    break;
  }
  case Expr::Extract:
    node->edges[0].second["label"] = "expr";
    break;

    // Casting
  case Expr::ZExt:
  case Expr::SExt:
    node->edges[0].second["label"] = "src";
    break;

    // Arithmetic
  case Expr::Add:
  case Expr::Sub:
  case Expr::Mul:
  case Expr::UDiv:
  case Expr::SDiv:
  case Expr::URem:
  case Expr::SRem:
    node->edges[0].second["label"] = "lhs";
    node->edges[1].second["label"] = "rhs";
    break;

    // Bit
  case Expr::Not:
    node->edges[0].second["label"] = "expr";
    break;

  case Expr::And:
  case Expr::Or:
  case Expr::Xor:
  case Expr::Shl:
  case Expr::LShr:
  case Expr::AShr:
    node->edges[0].second["label"] = "lhs";
    node->edges[1].second["label"] = "rhs";
    break;

    // Compare
  case Expr::Eq:
  case Expr::Ne:  ///< Not used in canonical form
  case Expr::Ult:
  case Expr::Ule:
  case Expr::Ugt: ///< Not used in canonical form
  case Expr::Uge: ///< Not used in canonical form
  case Expr::Slt:
  case Expr::Sle:
  case Expr::Sgt: ///< Not used in canonical form
  case Expr::Sge:
    node->edges[0].second["label"] = "lhs";
    node->edges[1].second["label"] = "rhs";
    break;

  default:
    assert(0 && "Unhandled Expr type");
  }
}

void DefaultExprDotDecorator::decorateExpr(const ref<Expr> expr,
        ExprGraphvizNodeRef node) {
    decorateExprNode(expr, node);
    decorateExprEdges(expr, node);
}

void DefaultExprDotDecorator::decorateArray(const Array *array,
    ExprGraphvizNodeRef node) {
  node->properties["shape"] = "record";

  // Compose the label
  std::ostringstream oss;
  if (array->size > ArrayWrapSize)
    oss << "{{";
  for (unsigned i = 0; i < array->size; i++) {
    if (i > 0) {
      if (i % ArrayWrapSize == 0) {
        oss << "} | {";
      } else {
        oss << " | ";
      }
    }

    oss << "<" << array->name << "_" << i << "> ";
    oss << "[" << i << "]\\n";
    if (!array->constantValues.empty())
      oss << array->constantValues[i]->getZExtValue();
    else
      oss << "X";
  }
  if (array->size > ArrayWrapSize)
    oss << "}}";
  node->properties["label"] = oss.str();
  node->properties["xlabel"] = array->name;
}

}
