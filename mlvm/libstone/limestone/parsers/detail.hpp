#pragma once
#include "syntax.hpp"
#include <dparse.h>

namespace limestone::syntax::detail {
class Tree;
class NodeView {
  const Tree* tree_;
  D_ParseNode* node_;
public:
  NodeView(const Tree& tree, D_ParseNode* node):tree_(&tree),node_(node) {}
  std::string_view symbol() const;
  std::string_view text() const;
  SourceSpan source() const;
  std::vector<NodeView> children() const;
};
class Tree {
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit Tree(std::unique_ptr<Impl>);
  friend class NodeView;
  friend Result<std::unique_ptr<Tree>> parse_tree(D_ParserTables&,std::string_view,std::string_view,const ParseLimits&);
public:
  ~Tree();
  NodeView root() const;
};
Result<std::unique_ptr<Tree>> parse_tree(D_ParserTables&,std::string_view,std::string_view,const ParseLimits&);

// Flatten only DParser's synthetic EBNF nodes. Named productions form AST boundaries.
class Fields {
  struct Child { NodeView node; bool used=false; };
  NodeView parent_;
  std::vector<Child> children_;
  std::vector<size_t> matching(std::string_view) const;
  [[noreturn]] void fail(std::string_view) const;
public:
  Fields(NodeView,std::span<const std::string_view> symbols);
  NodeView one(std::string_view,size_t index=SIZE_MAX);
  std::optional<NodeView> optional(std::string_view,size_t index=SIZE_MAX);
  std::vector<NodeView> many(std::string_view,size_t begin=0);
  NodeView one_of(std::span<const std::string_view>);
  void finish() const;
};
}
