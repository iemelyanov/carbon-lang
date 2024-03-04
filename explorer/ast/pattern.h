// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CARBON_EXPLORER_AST_PATTERN_H_
#define CARBON_EXPLORER_AST_PATTERN_H_

#include <optional>
#include <string>
#include <vector>

#include "common/check.h"
#include "common/ostream.h"
#include "explorer/ast/ast_node.h"
#include "explorer/ast/ast_rtti.h"
#include "explorer/ast/clone_context.h"
#include "explorer/ast/expression.h"
#include "explorer/ast/expression_category.h"
#include "explorer/ast/value_node.h"
#include "explorer/base/source_location.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace Carbon {

class Value;

// Abstract base class of all AST nodes representing patterns.
//
// Pattern and its derived classes support LLVM-style RTTI, including
// llvm::isa, llvm::cast, and llvm::dyn_cast. To support this, every
// class derived from Pattern must provide a `classof` operation, and
// every concrete derived class must have a corresponding enumerator
// in `Kind`; see https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html for
// details.
class Pattern : public AstNode {
 public:
  explicit Pattern(CloneContext& context, const Pattern& other)
      : AstNode(context, other),
        static_type_(context.Clone(other.static_type_)),
        value_(context.Clone(other.value_)) {}

  Pattern(const Pattern&) = delete;
  auto operator=(const Pattern&) -> Pattern& = delete;

  ~Pattern() override = 0;

  void Print(llvm::raw_ostream& out) const override;
  void PrintID(llvm::raw_ostream& out) const override;

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromPattern(node->kind());
  }

  // Returns the enumerator corresponding to the most-derived type of this
  // object.
  auto kind() const -> PatternKind {
    return static_cast<PatternKind>(root_kind());
  }

  // The static type of this pattern. Cannot be called before typechecking.
  auto static_type() const -> const Value& {
    CARBON_CHECK(static_type_.has_value());
    return **static_type_;
  }
  auto has_static_type() const -> bool { return static_type_.has_value(); }

  // Sets the static type of this expression. Can only be called once, during
  // typechecking.
  void set_static_type(Nonnull<const Value*> type) {
    CARBON_CHECK(!static_type_.has_value());
    static_type_ = type;
  }

  // The value of this pattern. Cannot be called before typechecking.
  // TODO: Rename to avoid confusion with BindingPattern::constant_value
  auto value() const -> const Value& { return **value_; }

  // Sets the value of this pattern. Can only be called once, during
  // typechecking.
  void set_value(Nonnull<const Value*> value) {
    CARBON_CHECK(!value_) << "set_value called more than once";
    value_ = value;
  }

  // Returns whether the value has been set. Should only be called
  // during typechecking: before typechecking it's guaranteed to be false,
  // and after typechecking it's guaranteed to be true.
  auto has_value() const -> bool { return value_.has_value(); }

  // Determines whether the pattern has already been type-checked. Should
  // only be used by type-checking.
  auto is_type_checked() const -> bool {
    return static_type_.has_value() && value_.has_value();
  }

 protected:
  // Constructs a Pattern representing syntax at the given line number.
  // `kind` must be the enumerator corresponding to the most-derived type being
  // constructed.
  Pattern(AstNodeKind kind, SourceLocation source_loc)
      : AstNode(kind, source_loc) {}

 private:
  std::optional<Nonnull<const Value*>> static_type_;
  std::optional<Nonnull<const Value*>> value_;
};

// Call the given `visitor` on all patterns nested within the given pattern,
// including `pattern` itself, in a preorder traversal. Aborts and returns
// `false` if `visitor` returns `false`, otherwise returns `true`.
auto VisitNestedPatterns(const Pattern& pattern,
                         llvm::function_ref<bool(const Pattern&)> visitor)
    -> bool;
inline auto VisitNestedPatterns(Pattern& pattern,
                                llvm::function_ref<bool(Pattern&)> visitor)
    -> bool {
  // The non-const version is implemented in terms of the const version. The
  // const_cast is safe because every pattern reachable through a non-const
  // pattern is also non-const.
  const Pattern& const_pattern = pattern;
  return VisitNestedPatterns(const_pattern, [&](const Pattern& inner) {
    return visitor(const_cast<Pattern&>(inner));
  });
}

// A pattern consisting of the `auto` keyword.
class AutoPattern : public Pattern {
 public:
  explicit AutoPattern(SourceLocation source_loc)
      : Pattern(AstNodeKind::AutoPattern, source_loc) {}

  explicit AutoPattern(CloneContext& context, const AutoPattern& other)
      : Pattern(context, other) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromAutoPattern(node->kind());
  }
};

class VarPattern : public Pattern {
 public:
  explicit VarPattern(SourceLocation source_loc, Nonnull<Pattern*> pattern)
      : Pattern(AstNodeKind::VarPattern, source_loc), pattern_(pattern) {}

  explicit VarPattern(CloneContext& context, const VarPattern& other)
      : Pattern(context, other), pattern_(context.Clone(other.pattern_)) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromVarPattern(node->kind());
  }

  auto pattern() const -> const Pattern& { return *pattern_; }
  auto pattern() -> Pattern& { return *pattern_; }

  auto expression_category() const -> ExpressionCategory {
    return ExpressionCategory::Reference;
  }

 private:
  Nonnull<Pattern*> pattern_;
};

// A pattern that matches a value of a specified type, and optionally binds
// a name to it.
class BindingPattern : public Pattern {
 public:
  using ImplementsCarbonValueNode = void;

  BindingPattern(SourceLocation source_loc, std::string name,
                 Nonnull<Pattern*> type,
                 std::optional<ExpressionCategory> expression_category)
      : Pattern(AstNodeKind::BindingPattern, source_loc),
        name_(std::move(name)),
        type_(type),
        expression_category_(expression_category) {}

  explicit BindingPattern(CloneContext& context, const BindingPattern& other)
      : Pattern(context, other),
        name_(other.name_),
        type_(context.Clone(other.type_)),
        expression_category_(other.expression_category_) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromBindingPattern(node->kind());
  }

  // The name this pattern binds, if any. If equal to AnonymousName, indicates
  // that this BindingPattern does not bind a name, which in turn means it
  // should not be used as a ValueNode.
  auto name() const -> const std::string& { return name_; }

  // The pattern specifying the type of values that this pattern matches.
  auto type() const -> const Pattern& { return *type_; }
  auto type() -> Pattern& { return *type_; }

  // Returns the value category of this pattern. Can only be called after
  // typechecking.
  auto expression_category() const -> ExpressionCategory {
    return expression_category_.value();
  }

  // Returns whether the value category has been set. Should only be called
  // during typechecking.
  auto has_expression_category() const -> bool {
    return expression_category_.has_value();
  }

  // Sets the value category of the variable being bound. Can only be called
  // once during typechecking
  void set_expression_category(ExpressionCategory vc) {
    CARBON_CHECK(!expression_category_.has_value());
    expression_category_ = vc;
  }

  auto constant_value() const -> std::optional<Nonnull<const Value*>> {
    return std::nullopt;
  }
  auto symbolic_identity() const -> std::optional<Nonnull<const Value*>> {
    return std::nullopt;
  }

 private:
  std::string name_;
  Nonnull<Pattern*> type_;
  std::optional<ExpressionCategory> expression_category_;
};

class AddrPattern : public Pattern {
 public:
  explicit AddrPattern(SourceLocation source_loc,
                       Nonnull<BindingPattern*> binding)
      : Pattern(AstNodeKind::AddrPattern, source_loc), binding_(binding) {}

  explicit AddrPattern(CloneContext& context, const AddrPattern& other)
      : Pattern(context, other), binding_(context.Clone(other.binding_)) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromAddrPattern(node->kind());
  }

  auto binding() const -> const BindingPattern& { return *binding_; }
  auto binding() -> BindingPattern& { return *binding_; }

 private:
  Nonnull<BindingPattern*> binding_;
};

// A pattern that matches a tuple value field-wise.
class TuplePattern : public Pattern {
 public:
  TuplePattern(SourceLocation source_loc, std::vector<Nonnull<Pattern*>> fields)
      : Pattern(AstNodeKind::TuplePattern, source_loc),
        fields_(std::move(fields)) {}

  explicit TuplePattern(CloneContext& context, const TuplePattern& other)
      : Pattern(context, other), fields_(context.Clone(other.fields_)) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromTuplePattern(node->kind());
  }

  auto fields() const -> llvm::ArrayRef<Nonnull<const Pattern*>> {
    return fields_;
  }
  auto fields() -> llvm::ArrayRef<Nonnull<Pattern*>> { return fields_; }

 private:
  std::vector<Nonnull<Pattern*>> fields_;
};

class GenericBinding : public Pattern {
 public:
  using ImplementsCarbonValueNode = void;

  enum class BindingKind {
    // A checked generic binding, `T:! type`.
    Checked,
    // A template generic binding, `template T:! type`.
    Template,
  };

  explicit GenericBinding(SourceLocation source_loc, std::string name,
                          Nonnull<Expression*> type, BindingKind binding_kind)
      : Pattern(AstNodeKind::GenericBinding, source_loc),
        name_(std::move(name)),
        type_(type),
        binding_kind_(binding_kind) {}

  explicit GenericBinding(CloneContext& context, const GenericBinding& other);

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromGenericBinding(node->kind());
  }

  auto name() const -> const std::string& { return name_; }
  auto type() const -> const Expression& { return *type_; }
  auto type() -> Expression& { return *type_; }
  auto binding_kind() const -> BindingKind { return binding_kind_; }

  // The index of this binding, which is the number of bindings that are in
  // scope at the point where this binding is declared.
  auto index() const -> int {
    CARBON_CHECK(index_);
    return *index_;
  }

  // Set the index of this binding. Should be called only during type-checking.
  void set_index(int index) {
    CARBON_CHECK(!index_) << "should only set depth and index once";
    index_ = index;
  }

  auto expression_category() const -> ExpressionCategory {
    return ExpressionCategory::Value;
  }

  auto constant_value() const -> std::optional<Nonnull<const Value*>> {
    return template_value_;
  }

  auto symbolic_identity() const -> std::optional<Nonnull<const Value*>> {
    return symbolic_identity_;
  }
  void set_symbolic_identity(Nonnull<const Value*> value) {
    CARBON_CHECK(!symbolic_identity_.has_value());
    symbolic_identity_ = value;
  }

  void set_template_value(Nonnull<const Value*> template_value) {
    CARBON_CHECK(binding_kind_ == BindingKind::Template);
    template_value_ = template_value;
  }
  auto has_template_value() const -> bool {
    CARBON_CHECK(binding_kind_ == BindingKind::Template);
    return template_value_.has_value();
  }

  // The impl binding associated with this type variable.
  auto impl_binding() const -> std::optional<Nonnull<const ImplBinding*>> {
    return impl_binding_;
  }
  // Set the impl binding.
  void set_impl_binding(Nonnull<const ImplBinding*> binding) {
    CARBON_CHECK(!impl_binding_.has_value());
    impl_binding_ = binding;
  }

  // Return the original generic binding.
  auto original() const -> Nonnull<const GenericBinding*> {
    if (original_.has_value()) {
      return *original_;
    } else {
      return this;
    }
  }
  // Set the original generic binding.
  void set_original(Nonnull<const GenericBinding*> orig) { original_ = orig; }

  // Returns whether this binding has been named as a type within its own type
  // expression via `.Self`. Set by type-checking.
  auto named_as_type_via_dot_self() const -> bool {
    return named_as_type_via_dot_self_;
  }
  // Set that this binding was named as a type within its own type expression
  // via `.Self`. May only be called during type-checking.
  void set_named_as_type_via_dot_self() { named_as_type_via_dot_self_ = true; }

 private:
  std::string name_;
  Nonnull<Expression*> type_;
  BindingKind binding_kind_;
  std::optional<int> index_;
  std::optional<Nonnull<const Value*>> template_value_;
  std::optional<Nonnull<const Value*>> symbolic_identity_;
  std::optional<Nonnull<const ImplBinding*>> impl_binding_;
  std::optional<Nonnull<const GenericBinding*>> original_;
  bool named_as_type_via_dot_self_ = false;
};

// Converts paren_contents to a Pattern, interpreting the parentheses as
// grouping if their contents permit that interpretation, or as forming a
// tuple otherwise.
auto PatternFromParenContents(Nonnull<Arena*> arena, SourceLocation source_loc,
                              const ParenContents<Pattern>& paren_contents)
    -> Nonnull<Pattern*>;

// Converts paren_contents to a TuplePattern, interpreting the parentheses as
// forming a tuple.
auto TuplePatternFromParenContents(Nonnull<Arena*> arena,
                                   SourceLocation source_loc,
                                   const ParenContents<Pattern>& paren_contents)
    -> Nonnull<TuplePattern*>;

// Converts `contents` to ParenContents<Pattern> by replacing each Expression
// with an ExpressionPattern.
auto ParenExpressionToParenPattern(Nonnull<Arena*> arena,
                                   const ParenContents<Expression>& contents)
    -> ParenContents<Pattern>;

// A pattern that matches an alternative of a choice type.
class AlternativePattern : public Pattern {
 public:
  // Constructs an AlternativePattern that matches the alternative specified
  // by `alternative`, if its arguments match `arguments`.
  static auto Create(Nonnull<Arena*> arena, SourceLocation source_loc,
                     Nonnull<Expression*> alternative,
                     Nonnull<TuplePattern*> arguments)
      -> ErrorOr<Nonnull<AlternativePattern*>> {
    CARBON_ASSIGN_OR_RETURN(
        Nonnull<SimpleMemberAccessExpression*> member_access,
        RequireSimpleMemberAccess(alternative));
    return arena->New<AlternativePattern>(source_loc, &member_access->object(),
                                          member_access->member_name(),
                                          arguments);
  }

  // Constructs an AlternativePattern that matches a value of the type
  // specified by choice_type if it represents an alternative named
  // alternative_name, and its arguments match `arguments`.
  AlternativePattern(SourceLocation source_loc,
                     Nonnull<Expression*> choice_type,
                     std::string alternative_name,
                     Nonnull<TuplePattern*> arguments)
      : Pattern(AstNodeKind::AlternativePattern, source_loc),
        choice_type_(choice_type),
        alternative_name_(std::move(alternative_name)),
        arguments_(arguments) {}

  explicit AlternativePattern(CloneContext& context,
                              const AlternativePattern& other)
      : Pattern(context, other),
        choice_type_(context.Clone(other.choice_type_)),
        alternative_name_(other.alternative_name_),
        arguments_(context.Clone(other.arguments_)) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromAlternativePattern(node->kind());
  }

  auto choice_type() const -> const Expression& { return *choice_type_; }
  auto choice_type() -> Expression& { return *choice_type_; }
  auto alternative_name() const -> const std::string& {
    return alternative_name_;
  }
  auto arguments() const -> const TuplePattern& { return *arguments_; }
  auto arguments() -> TuplePattern& { return *arguments_; }

 private:
  static auto RequireSimpleMemberAccess(Nonnull<Expression*> alternative)
      -> ErrorOr<Nonnull<SimpleMemberAccessExpression*>>;

  Nonnull<Expression*> choice_type_;
  std::string alternative_name_;
  Nonnull<TuplePattern*> arguments_;
};

// A pattern that matches a value if it is equal to the value of a given
// expression.
class ExpressionPattern : public Pattern {
 public:
  explicit ExpressionPattern(Nonnull<Expression*> expression)
      : Pattern(AstNodeKind::ExpressionPattern, expression->source_loc()),
        expression_(expression) {}

  explicit ExpressionPattern(CloneContext& context,
                             const ExpressionPattern& other)
      : Pattern(context, other),
        expression_(context.Clone(other.expression_)) {}

  static auto classof(const AstNode* node) -> bool {
    return InheritsFromExpressionPattern(node->kind());
  }

  auto expression() const -> const Expression& { return *expression_; }
  auto expression() -> Expression& { return *expression_; }

 private:
  Nonnull<Expression*> expression_;
};

}  // namespace Carbon

#endif  // CARBON_EXPLORER_AST_PATTERN_H_
