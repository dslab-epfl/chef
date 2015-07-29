//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Constraints.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"

#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <map>

namespace klee {


void ConstraintManager::addConstraint(const ref<Expr> e) {
    switch (e->getKind()) {
    case Expr::Constant:
        assert(cast<ConstantExpr>(e)->isTrue()
                && "attempt to add invalid (false) constraint");
        break;
    case Expr::And: {
        BinaryExpr *be = cast<BinaryExpr>(e);
        addConstraint(be->left);
        addConstraint(be->right);
        break;
    }
    default:
        head_ = head_->getOrCreate(e);
    }
}


////////////////////////////////////////////////////////////////////////////////

llvm::raw_ostream &ConditionInspector::Print(llvm::raw_ostream &out,
        const ConstraintManager &cm) {
    ConditionNodeRef node = cm.head();
    while (node != cm.root()) {
        if (node != cm.head()) {
            out << '*';
        }
        out << '[';
        Print(out, node->expr());
        out << ']';
        node = node->parent();
    }
    return out;
}

llvm::raw_ostream &ConditionInspector::PrintOpaque(llvm::raw_ostream &out,
        const ref<Expr> expr) {
    ExprHashMap<std::string>::iterator it = terms_.find(expr);
    std::string expr_name;
    if (it != terms_.end()) {
        expr_name = it->second;
    } else {
        int id = counter_++;
        if (!id) {
            expr_name = "A";
        } else {
            while (id) {
                expr_name.push_back('A' + id % ('Z' - 'A' + 1));
                id = id / ('Z' - 'A' + 1);
            }
            std::reverse(expr_name.begin(), expr_name.end());
        }
        terms_.insert(std::make_pair(expr, expr_name));
    }

    out << expr_name << ":" << expr->getWidth();
    return out;
}

llvm::raw_ostream &ConditionInspector::Print(llvm::raw_ostream &out,
        const ref<Expr> expr) {
    if (BinaryExpr *be = cast<BinaryExpr>(expr)) {
        if (be->left->getWidth() != Expr::Bool) {
            PrintOpaque(out, expr);
            return out;
        }
    }

    switch (expr->getKind()) {
    case Expr::And: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        Print(out, be->left);
        out << '*';
        Print(out, be->right);
        break;
    }
    case Expr::Or: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        out << '(';
        Print(out, be->left);
        out << '+';
        Print(out, be->right);
        out << ')';
        break;
    }
    case Expr::Not: {
        NotExpr *ne = cast<NotExpr>(expr);
        out << "!(";
        Print(out, ne->expr);
        out << ')';
        break;
    }
    case Expr::Eq: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        if (be->left->isZero()) {
            out << "!(";
            Print(out, be->right);
            out << ")";
        } else if (be->right->isZero()) {
            out << "!(";
            Print(out, be->left);
            out << ")";
        } else {
            Print(out, be->left);
            out << "==";
            Print(out, be->right);
        }
        break;
    }
#if 0
    case Expr::Constant: {
        ConstantExpr *ce = cast<ConstantExpr>(expr);
        out << ce->getAPValue();
        break;
    }
    case Expr::Eq:
    case Expr::Ult:
    case Expr::Ule:
    case Expr::Slt:
    case Expr::Sle: {
        BinaryExpr *be = cast<BinaryExpr>(expr);
        Print(out, be->left);
        switch (expr->getKind()) {
        case Expr::Eq:
            out << '=';
            break;
        case Expr::Ult:
        case Expr::Slt:
            out << '<';
            break;
        case Expr::Ule:
        case Expr::Sle:
            out << "<=";
            break;
        }
        Print(out, be->right);
        break;
    }
#endif
    default:
        PrintOpaque(out, expr);
        break;
    }
    return out;
}


}
