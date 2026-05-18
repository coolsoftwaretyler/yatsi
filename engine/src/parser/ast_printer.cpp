#include "parser/ast_printer.h"

#include "lexer/token.h"
#include "typechecker/types.h"

namespace yatsi {

namespace {

void print_indent(std::ostream& out, int depth) {
  for (int i = 0; i < depth; i++)
    out << "  ";
}

void print_type_suffix(const Expr& expr, std::ostream& out, bool show_types) {
  if (show_types && expr.resolved_type) {
    out << " :: " << type_to_string(*expr.resolved_type);
  }
}

void print_expr(const Expr& expr, std::ostream& out, int depth, bool show_types);
void print_stmt(const Stmt& stmt, std::ostream& out, int depth, bool show_types);

void print_expr(const Expr& expr, std::ostream& out, int depth, bool show_types) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NumberLiteral>) {
          print_indent(out, depth);
          out << "NumberLiteral(" << node.value << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, StringLiteral>) {
          print_indent(out, depth);
          out << "StringLiteral(\"" << node.value << "\")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, BooleanLiteral>) {
          print_indent(out, depth);
          out << "BooleanLiteral(" << (node.value ? "true" : "false") << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, NullLiteral>) {
          print_indent(out, depth);
          out << "NullLiteral";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, UndefinedLiteral>) {
          print_indent(out, depth);
          out << "UndefinedLiteral";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, Identifier>) {
          print_indent(out, depth);
          out << "Identifier(" << node.name << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          print_indent(out, depth);
          out << "BinaryExpr(" << token_kind_to_string(node.op) << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          print_expr(*node.left, out, depth + 1, show_types);
          print_expr(*node.right, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          print_indent(out, depth);
          out << "UnaryExpr(" << token_kind_to_string(node.op)
              << (node.prefix ? " prefix" : " postfix") << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          print_expr(*node.operand, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          print_indent(out, depth);
          out << "AssignmentExpr(" << token_kind_to_string(node.op) << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          print_expr(*node.target, out, depth + 1, show_types);
          print_expr(*node.value, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          print_indent(out, depth);
          out << "CallExpr";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          print_indent(out, depth + 1);
          out << "callee:\n";
          print_expr(*node.callee, out, depth + 2, show_types);
          print_indent(out, depth + 1);
          out << "args: (" << node.arguments.size() << ")\n";
          for (const auto& arg : node.arguments)
            print_expr(*arg, out, depth + 2, show_types);
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          print_indent(out, depth);
          if (node.is_computed) {
            out << "MemberExpr[computed]";
            print_type_suffix(expr, out, show_types);
            out << "\n";
            print_expr(*node.object, out, depth + 1, show_types);
            print_expr(*node.computed, out, depth + 1, show_types);
          } else {
            out << "MemberExpr(." << node.property << ")";
            print_type_suffix(expr, out, show_types);
            out << "\n";
            print_expr(*node.object, out, depth + 1, show_types);
          }
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          print_indent(out, depth);
          out << "ArrayLiteral (" << node.elements.size() << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          for (const auto& el : node.elements)
            print_expr(*el, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          print_indent(out, depth);
          out << "ObjectLiteral (" << node.properties.size() << ")";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          for (const auto& prop : node.properties) {
            print_indent(out, depth + 1);
            out << "property:\n";
            print_indent(out, depth + 2);
            out << "key:\n";
            print_expr(*prop.key, out, depth + 3, show_types);
            print_indent(out, depth + 2);
            out << "value:\n";
            print_expr(*prop.value, out, depth + 3, show_types);
          }
        } else if constexpr (std::is_same_v<T, ArrowFunction>) {
          print_indent(out, depth);
          out << "ArrowFunction(";
          for (size_t i = 0; i < node.params.size(); i++) {
            if (i > 0) out << ", ";
            out << node.params[i].name;
            if (node.params[i].type_annotation)
              out << ": " << node.params[i].type_annotation->name;
          }
          out << ")";
          if (node.return_type)
            out << ": " << node.return_type->name;
          print_type_suffix(expr, out, show_types);
          out << "\n";
          std::visit(
              [&](const auto& body) {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, ExprPtr>) {
                  if (body) print_expr(*body, out, depth + 1, show_types);
                } else {
                  if (body) print_stmt(*body, out, depth + 1, show_types);
                }
              },
              node.body);
        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          print_indent(out, depth);
          out << "ConditionalExpr";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          print_indent(out, depth + 1);
          out << "condition:\n";
          print_expr(*node.condition, out, depth + 2, show_types);
          print_indent(out, depth + 1);
          out << "consequent:\n";
          print_expr(*node.consequent, out, depth + 2, show_types);
          print_indent(out, depth + 1);
          out << "alternate:\n";
          print_expr(*node.alternate, out, depth + 2, show_types);
        } else if constexpr (std::is_same_v<T, TemplateLiteral>) {
          print_indent(out, depth);
          out << "TemplateLiteral";
          print_type_suffix(expr, out, show_types);
          out << "\n";
          for (size_t i = 0; i < node.quasis.size(); i++) {
            print_indent(out, depth + 1);
            out << "quasi: \"" << node.quasis[i] << "\"\n";
            if (i < node.expressions.size())
              print_expr(*node.expressions[i], out, depth + 1, show_types);
          }
        }
      },
      static_cast<const Expr::variant&>(expr));
}

void print_stmt(const Stmt& stmt, std::ostream& out, int depth, bool show_types) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) {
          print_indent(out, depth);
          out << "ExpressionStmt\n";
          print_expr(*node.expression, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, VarDeclaration>) {
          print_indent(out, depth);
          out << "VarDeclaration(" << token_kind_to_string(node.kind) << " "
              << node.name;
          if (node.type_annotation)
            out << ": " << node.type_annotation->name;
          out << ")\n";
          if (node.initializer)
            print_expr(*node.initializer, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          print_indent(out, depth);
          out << "BlockStmt\n";
          for (const auto& s : node.statements)
            print_stmt(*s, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
          print_indent(out, depth);
          out << "IfStmt\n";
          print_indent(out, depth + 1);
          out << "condition:\n";
          print_expr(*node.condition, out, depth + 2, show_types);
          print_indent(out, depth + 1);
          out << "then:\n";
          print_stmt(*node.consequent, out, depth + 2, show_types);
          if (node.alternate) {
            print_indent(out, depth + 1);
            out << "else:\n";
            print_stmt(*node.alternate, out, depth + 2, show_types);
          }
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
          print_indent(out, depth);
          out << "WhileStmt\n";
          print_indent(out, depth + 1);
          out << "condition:\n";
          print_expr(*node.condition, out, depth + 2, show_types);
          print_indent(out, depth + 1);
          out << "body:\n";
          print_stmt(*node.body, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, ForStmt>) {
          print_indent(out, depth);
          out << "ForStmt\n";
          if (node.init) {
            print_indent(out, depth + 1);
            out << "init:\n";
            print_stmt(*node.init, out, depth + 2, show_types);
          }
          if (node.condition) {
            print_indent(out, depth + 1);
            out << "condition:\n";
            print_expr(*node.condition, out, depth + 2, show_types);
          }
          if (node.update) {
            print_indent(out, depth + 1);
            out << "update:\n";
            print_expr(*node.update, out, depth + 2, show_types);
          }
          print_indent(out, depth + 1);
          out << "body:\n";
          print_stmt(*node.body, out, depth + 2, show_types);
        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
          print_indent(out, depth);
          out << "FunctionDecl(" << node.name << "(";
          for (size_t i = 0; i < node.params.size(); i++) {
            if (i > 0) out << ", ";
            out << node.params[i].name;
            if (node.params[i].type_annotation)
              out << ": " << node.params[i].type_annotation->name;
          }
          out << ")";
          if (node.return_type)
            out << ": " << node.return_type->name;
          out << ")\n";
          if (node.body) print_stmt(*node.body, out, depth + 1, show_types);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          print_indent(out, depth);
          out << "ReturnStmt\n";
          if (node.value)
            print_expr(*node.value, out, depth + 1, show_types);
        }
      },
      static_cast<const Stmt::variant&>(stmt));
}

} // namespace

void print_ast(const Program& program, std::ostream& out) {
  out << "Program\n";
  for (const auto& stmt : program.body)
    print_stmt(*stmt, out, 1, false);
}

void print_typed_ast(const Program& program, std::ostream& out) {
  out << "Program\n";
  for (const auto& stmt : program.body)
    print_stmt(*stmt, out, 1, true);
}

} // namespace yatsi
