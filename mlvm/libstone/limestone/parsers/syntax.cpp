#include "detail.hpp"
#include <limits>
#include <new>

namespace limestone::syntax::detail {
namespace {
struct ParseState { bool ambiguous=false; const char* position=nullptr; int symbol=0; };
void syntax_error(D_Parser*) {}
D_ParseNode* ambiguity(D_Parser* parser,int,D_ParseNode** nodes) {
  auto& state=*static_cast<ParseState*>(parser->initial_globals);
  if(!state.ambiguous) { state.position=nodes[0]->start_loc.s;state.symbol=nodes[0]->symbol; }
  state.ambiguous=true;
  return nodes[0]; // DParser requires a node; the owning entry point rejects this parse.
}
std::string diagnostic(const SourceSpan& source,std::string_view message) {
  return source.file+":"+std::to_string(source.begin.line)+":"+
    std::to_string(source.begin.column)+": "+std::string(message);
}
}
struct Tree::Impl {
  D_ParserTables* tables;
  std::string buffer, file;
  std::vector<size_t> lines{0};
  ParseState state;
  std::unique_ptr<D_Parser,decltype(&free_D_Parser)> parser{nullptr,free_D_Parser};
  D_ParseNode* root=nullptr;
  Impl(D_ParserTables& t,std::string_view input,std::string_view filename):tables(&t),buffer(input),file(filename) {
    for(size_t i=0;i<buffer.size();++i)if(buffer[i]=='\n')lines.push_back(i+1);
  }
  ~Impl() { if(root)free_D_ParseNode(parser.get(),root); }
  SourcePosition position(const char* pointer) const {
    size_t offset=pointer?static_cast<size_t>(pointer-buffer.data()):0;
    offset=std::min(offset,buffer.size());
    auto line=static_cast<size_t>(std::upper_bound(lines.begin(),lines.end(),offset)-lines.begin()-1);
    return {offset,line+1,offset-lines[line]+1};
  }
};
Tree::Tree(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
Tree::~Tree()=default;
NodeView Tree::root() const { return {*this,impl_->root}; }
std::string_view NodeView::symbol() const { return tree_->impl_->tables->symbols[node_->symbol].name; }
std::string_view NodeView::text() const {
  auto span=source();
  return std::string_view(tree_->impl_->buffer).substr(span.begin.offset,span.end.offset-span.begin.offset);
}
SourceSpan NodeView::source() const {
  auto& impl=*tree_->impl_;
  return {impl.file,impl.position(node_->start_loc.s),impl.position(node_->end)};
}
std::vector<NodeView> NodeView::children() const {
  std::vector<NodeView> out;
  auto count=d_get_number_of_children(node_);out.reserve(static_cast<size_t>(count));
  for(int i=0;i<count;++i)out.emplace_back(*tree_,d_get_child(node_,i));
  return out;
}
Result<std::unique_ptr<Tree>> parse_tree(D_ParserTables& tables,std::string_view input,std::string_view file,const ParseLimits& limits) {
  using R=Result<std::unique_ptr<Tree>>;
  if(!limits.bytes||!limits.nodes||!limits.depth)return R::err({Error::Code::InvalidArgument,"parse limits must be positive"});
  if(input.size()>limits.bytes||input.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
    return R::err({Error::Code::ResourceLimit,std::string(file)+": input byte limit exceeded"});
  if(auto offset=input.find('\0');offset!=input.npos)
    return R::err({Error::Code::Parse,std::string(file)+": embedded NUL at byte "+std::to_string(offset)});
  try {
    auto impl=std::make_unique<Tree::Impl>(tables,input,file);
    impl->parser.reset(new_D_Parser(&tables,0));
    auto& parser=*impl->parser;
    parser.save_parse_tree=1;parser.fixup_EBNF_productions=1;parser.error_recovery=0;
    parser.syntax_error_fn=syntax_error;parser.ambiguity_fn=ambiguity;parser.initial_globals=&impl->state;
    impl->root=dparse(&parser,impl->buffer.data(),static_cast<int>(input.size()));
    if(!impl->root||parser.syntax_errors) {
      SourceSpan source{impl->file,impl->position(parser.loc.s),{}};
      return R::err({Error::Code::Parse,diagnostic(source,"invalid syntax")});
    }
    if(impl->state.ambiguous)return R::err({Error::Code::Parse,diagnostic({impl->file,impl->position(impl->state.position),{}},
      "ambiguous syntax for "+std::string(tables.symbols[impl->state.symbol].name))});
    auto tree=std::unique_ptr<Tree>(new Tree(std::move(impl)));
    std::vector<std::pair<NodeView,size_t>> pending{{tree->root(),1}};
    size_t count=0;
    while(!pending.empty()) {
      auto [node,depth]=pending.back();pending.pop_back();
      if(++count>limits.nodes||depth>limits.depth)
        return R::err({Error::Code::ResourceLimit,diagnostic(node.source(),"parse tree node/depth limit exceeded")});
      for(auto child:node.children())pending.emplace_back(child,depth+1);
    }
    return R::ok(std::move(tree));
  }catch(const std::bad_alloc&) { return R::err({Error::Code::ResourceLimit,std::string(file)+": parser allocation failed"}); }
}
Fields::Fields(NodeView parent,std::span<const std::string_view> symbols):parent_(parent) {
  auto pending=parent.children();std::reverse(pending.begin(),pending.end());
  while(!pending.empty()) {
    auto node=pending.back();pending.pop_back();
    if(std::find(symbols.begin(),symbols.end(),node.symbol())!=symbols.end())children_.push_back({node});
    else {
      auto children=node.children();
      for(auto it=children.rbegin();it!=children.rend();++it)pending.push_back(*it);
    }
  }
}
std::vector<size_t> Fields::matching(std::string_view name) const {
  std::vector<size_t> out;
  for(size_t i=0;i<children_.size();++i)if(children_[i].node.symbol()==name)out.push_back(i);
  return out;
}
[[noreturn]] void Fields::fail(std::string_view message) const {
  throw Error{Error::Code::Internal,diagnostic(parent_.source(),std::string(parent_.symbol())+": AST schema mismatch: "+std::string(message))};
}
NodeView Fields::one(std::string_view name,size_t index) {
  auto nodes=matching(name);
  if(index==SIZE_MAX) { if(nodes.size()!=1)fail("expected one "+std::string(name));index=0; }
  if(index>=nodes.size())fail("missing "+std::string(name));
  auto& child=children_[nodes[index]];
  if(child.used)fail("duplicate mapping of "+std::string(name));
  child.used=true;return child.node;
}
std::optional<NodeView> Fields::optional(std::string_view name,size_t index) {
  auto nodes=matching(name);
  if(index==SIZE_MAX) { if(nodes.size()>1)fail("expected at most one "+std::string(name));index=0; }
  if(index>=nodes.size())return {};
  return one(name,index);
}
std::vector<NodeView> Fields::many(std::string_view name,size_t begin) {
  auto indices=matching(name);std::vector<NodeView> out;out.reserve(indices.size());
  if(begin>indices.size())fail("missing prefix of "+std::string(name));
  for(size_t k=begin;k<indices.size();++k) {
    auto i=indices[k];
    if(children_[i].used)fail("duplicate mapping of "+std::string(name));
    children_[i].used=true;out.push_back(children_[i].node);
  }
  return out;
}
NodeView Fields::one_of(std::span<const std::string_view> names) {
  if(children_.size()!=1||std::find(names.begin(),names.end(),children_[0].node.symbol())==names.end())fail("expected one sum alternative");
  children_[0].used=true;return children_[0].node;
}
void Fields::finish() const {
  for(const auto& child:children_)if(!child.used)fail("unmapped "+std::string(child.node.symbol()));
}
}
