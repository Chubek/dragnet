#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long qw(GetOptions);
use File::Basename qw(basename);
use File::Temp qw(tempfile);
use File::Spec;

# A deliberately small declarative schema compiler. No input is evaluated as Perl/C++.
my ($header, $source, $grammar, $check, $help);
GetOptions('header=s'=>\$header, 'source=s'=>\$source, 'grammar=s'=>\$grammar,
           'check'=>\$check, 'help'=>\$help) or die "syngen: invalid options (see --help)\n";
if ($help) {
  print "Usage: perl scripts/syngen.pl [--grammar file.g] --check file.absyn\n",
        "       perl scripts/syngen.pl --grammar file.g --header ast.hpp --source ast.cpp file.absyn\n";
  exit 0;
}
@ARGV == 1 or die "syngen: expected one .absyn input (see --help)\n";
my $file=$ARGV[0];
sub read_file {
  my ($path)=@_;
  open my $fh, '<', $path or die "syngen: $path: $!\n";
  local $/;my $data=<$fh>;close $fh or die "syngen: $path: $!\n";
  return defined($data) ? $data : '';
}
my $text=read_file($file);
my @tokens;
pos($text)=0;
while (pos($text)<length($text)) {
  if ($text =~ /\G(?:\s+|\#[^\n]*|\/\/[^\n]*|\/\*.*?\*\/)/gcs) { next; }
  my $offset=pos($text);
  if ($text =~ /\G([A-Za-z_][A-Za-z_0-9]*|[0-9]+|::|[;{}=*?\@|])/gc) {
    push @tokens,{value=>$1,offset=>$offset};next;
  }
  die location($offset).": invalid schema character\n";
}
push @tokens,{value=>'<eof>',offset=>length($text)};
my $cursor=0;
sub location {
  my ($offset)=@_;my $prefix=substr($text,0,$offset);
  my $line=1+($prefix=~tr/\n/\n/);my $last=rindex($prefix,"\n");
  return "$file:$line:".($offset-$last);
}
sub fail { die location(defined($_[1]) ? $_[1] : $tokens[$cursor]{offset}).": $_[0]\n"; }
sub peek { return $tokens[$cursor]{value}; }
sub consume { return $tokens[$cursor++]{value}; }
sub expect { peek() eq $_[0] or fail("expected '$_[0]', got '".peek()."'");return consume(); }
my %reserved=map {$_=>1} qw(alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq);
sub identifier {
  my $name=peek();$name =~ /^[A-Za-z_][A-Za-z_0-9]*$/ or fail('expected identifier');
  !$reserved{$name} && $name !~ /__|^_[A-Z]/ or fail("reserved C++ identifier '$name'");
  return consume();
}
expect('namespace');
my @namespace=(identifier());push @namespace,identifier() while peek() eq '::' && consume();
expect(';');expect('parser');my $parser=identifier();expect(';');
expect('root');my $root_offset=$tokens[$cursor]{offset};my $root=identifier();expect(';');
my (@nodes,%nodes);
my %generated=map {$_=>1} qw(Node Kind Visitor ConstVisitor RecursiveVisitor ConstRecursiveVisitor parse);
while (peek() ne '<eof>') {
  my $kind=consume();$kind =~ /^(node|sum|token)$/ or fail("expected node, sum, or token; got '$kind'");
  my $offset=$tokens[$cursor]{offset};
  my $name=identifier();!exists($nodes{$name}) && !$generated{$name} or fail("duplicate or reserved node '$name'",$offset);
  my $node={name=>$name,kind=>$kind,offset=>$offset,fields=>[],alternatives=>[],alternative_offsets=>[]};
  $nodes{$name}=$node;push @nodes,$node;
  if ($kind eq 'node') {
    expect('{');my %fields;
    while (peek() ne '}') {
      my $field_offset=$tokens[$cursor]{offset};my $type=identifier();my $card='one';
      if (peek() eq '*' || peek() eq '?') { $card=consume() eq '*' ? 'many' : 'optional'; }
      my $field=identifier();
      !$fields{$field}++ && $field !~ /^(source|kind|accept|value)$/ && $field ne $name or fail("duplicate or reserved field '$field'");
      my $index;
      if (peek() eq '@') {
        consume();peek() =~ /^[0-9]+$/ or fail('expected zero-based field index');$index=0+consume();
        $index<=2147483647 or fail('field index out of range');
      }
      expect(';');push @{$node->{fields}},{type=>$type,name=>$field,card=>$card,index=>$index,offset=>$field_offset};
    }
    expect('}');
  }elsif ($kind eq 'sum') {
    expect('=');
    while (1) {
      push @{$node->{alternative_offsets}},$tokens[$cursor]{offset};
      push @{$node->{alternatives}},identifier();
      last if peek() ne '|';consume();
    }
    expect(';');
  }else { expect(';'); }
}
exists($nodes{$root}) or fail("unknown root '$root'",$root_offset);
for my $node (@nodes) {
  my %mapped;
  for my $field (@{$node->{fields}}) {
    exists($nodes{$field->{type}}) or fail("unknown field type '$field->{type}' in $node->{name}",$field->{offset});
    push @{$mapped{$field->{type}}},$field;
  }
  for my $type (sort keys %mapped) {
    my %indices;my $repeated;
    for my $field (@{$mapped{$type}}) {
      if (@{$mapped{$type}}>1) {
        defined($field->{index}) or fail("multiple '$type' fields in $node->{name} require explicit indices",$field->{offset});
        !$indices{$field->{index}}++ or fail("duplicate '$type' index in $node->{name}",$field->{offset});
        if ($field->{card} eq 'many') {
          !defined($repeated) or fail("multiple repeated '$type' fields in $node->{name}",$field->{offset});
          $repeated=$field->{index};
        }
      }
    }
    if (defined($repeated)) {
      for my $field (@{$mapped{$type}}) {
        $field->{card} eq 'many' || $field->{index}<$repeated or fail("overlapping '$type' fields in $node->{name}",$field->{offset});
      }
    }
  }
  my %alternatives;
  for my $i (0..$#{$node->{alternatives}}) {
    my $alt=$node->{alternatives}[$i];my $offset=$node->{alternative_offsets}[$i];
    exists($nodes{$alt}) or fail("unknown sum alternative '$alt' in $node->{name}",$offset);
    !$alternatives{$alt}++ or fail("duplicate sum alternative '$alt' in $node->{name}",$offset);
  }
}

if (defined $grammar) {
  # Remove comments, literals, and DParser code/directives before finding productions.
  my $g=read_file($grammar);
  $g =~ s~'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*|\$\{[^}]*\}~ ~gs;
  my %productions;
  while ($g =~ /\b([A-Za-z_][A-Za-z_0-9]*)\s*:\s*(.*?);/gs) {
    !exists($productions{$1}) or fail("duplicate grammar production '$1'");
    $productions{$1}=$2;
  }
  exists($productions{$root}) or fail("root '$root' absent from grammar",$root_offset);
  my ($first)=$g =~ /\b([A-Za-z_][A-Za-z_0-9]*)\s*:/;
  defined($first) && $first eq $root or fail('root must be the first grammar production',$root_offset);
  for my $node (@nodes) {
    exists($productions{$node->{name}}) or fail("node '$node->{name}' absent from grammar",$node->{offset});
    my %refs=map {$_=>1} grep {exists $productions{$_}} ($productions{$node->{name}} =~ /\b([A-Za-z_][A-Za-z_0-9]*)\b/g);
    my %mapped=map {$_=>1} (@{$node->{alternatives}},map {$_->{type}} @{$node->{fields}});
    for my $ref (sort keys %refs) { $mapped{$ref} or fail("unmapped grammar child '$ref' in $node->{name}",$node->{offset}); }
    for my $ref (sort keys %mapped) { $refs{$ref} or fail("'$ref' is not a grammar child of $node->{name}",$node->{offset}); }
  }
  for my $production (sort keys %productions) {
    $production eq 'whitespace' || exists($nodes{$production}) or fail("grammar production '$production' has no AST node");
  }
}
if ($check) { exit 0; }
defined($header) && defined($source) or die "syngen: --header and --source are required\n";
File::Spec->rel2abs($header) ne File::Spec->rel2abs($source) or die "syngen: outputs must differ\n";
for my $output ($header,$source) {
  File::Spec->rel2abs($output) ne File::Spec->rel2abs($file) &&
    (!defined($grammar)||File::Spec->rel2abs($output) ne File::Spec->rel2abs($grammar)) or die "syngen: output would overwrite input\n";
}
my $ns=join('::',@namespace);
my $banner="// Generated by scripts/syngen.pl from ".basename($file).". Do not edit.\n";
my $h=$banner."#pragma once\n#include \"parsers/syntax.hpp\"\n\nnamespace $ns {\n";
$h.="enum class Kind { ".join(', ',map {$_->{name}} @nodes)." };\n";
$h.="struct Visitor;\nstruct ConstVisitor;\n";
$h.="struct $_->{name};\n" for @nodes;
$h.=<<'CPP';
struct Node {
  ::limestone::syntax::SourceSpan source;
  virtual ~Node() = default;
  virtual Kind kind() const noexcept = 0;
  virtual void accept(Visitor&) = 0;
  virtual void accept(ConstVisitor&) const = 0;
};
CPP
for my $node (@nodes) {
  my $name=$node->{name};$h.="struct $name final : Node {\n";
  if ($node->{kind} eq 'token') { $h.="  std::string value; // Exact token spelling, including quotes/escapes.\n"; }
  elsif ($node->{kind} eq 'sum') {
    $h.="  std::variant<".join(', ',map {"std::unique_ptr<$_>"} @{$node->{alternatives}})."> value;\n";
  }else {
    for my $f (@{$node->{fields}}) {
      my $type="std::unique_ptr<$f->{type}>";$type="std::vector<$type>" if $f->{card} eq 'many';
      $h.="  $type $f->{name};\n";
    }
  }
  $h.="  $name();\n  ~$name() override;\n  Kind kind() const noexcept override { return Kind::$name; }\n";
  $h.="  void accept(Visitor&) override;\n  void accept(ConstVisitor&) const override;\n};\n";
}
for my $const ('','Const') {
  my $q=$const ? 'const ' : '';
  $h.="struct ${const}Visitor {\n  virtual ~${const}Visitor() = default;\n  virtual void visit(${q}Node&) {}\n";
  $h.="  virtual void visit(${q}$_->{name}&);\n" for @nodes;
  $h.="};\nstruct ${const}RecursiveVisitor : ${const}Visitor {\n  virtual bool enter(${q}Node&) { return true; }\n  virtual void leave(${q}Node&) {}\n";
  $h.="  void visit(${q}$_->{name}&) override;\n" for @nodes;
  $h.="};\n";
}
$h.="::limestone::Result<std::unique_ptr<$root>> parse(std::string_view input, std::string_view file=\"<input>\",\n  const ::limestone::syntax::ParseLimits& limits={});\n} // namespace $ns\n";
my $include=basename($header);$include =~ /^[A-Za-z_0-9.-]+$/ or die "syngen: invalid header filename\n";
my $c=$banner."#include \"$include\"\n#include \"parsers/detail.hpp\"\n#include <array>\n#include <new>\n\nextern \"C\" D_ParserTables parser_tables_$parser;\n\nnamespace $ns {\n";
for my $node (@nodes) {
  my $name=$node->{name};
  $c.="$name\::$name() = default;\n$name\::~$name() = default;\n";
  $c.="void $name\::accept(Visitor& v) { v.visit(*this); }\nvoid $name\::accept(ConstVisitor& v) const { v.visit(*this); }\n";
  for my $const ('','Const') {
    my $q=$const ? 'const ' : '';
    $c.="void ${const}Visitor::visit(${q}$name& n) { visit(static_cast<${q}Node&>(n)); }\n";
    $c.="void ${const}RecursiveVisitor::visit(${q}$name& n) {\n  if (!enter(n)) return;\n";
    if ($node->{kind} eq 'sum') {
      # Constness of a unique_ptr does not propagate to its pointee.
      my $cast=$const ? 'static_cast<const Node&>(*child)' : '(*child)';
      $c.="  std::visit([&](auto& child) { if(child) $cast.accept(*this); }, n.value);\n";
    }else {
      for my $f (@{$node->{fields}}) {
        my $cast=$const ? 'static_cast<const Node&>(*child)' : '(*child)';
        if ($f->{card} eq 'many') { $c.="  for(auto& child : n.$f->{name}) if(child) $cast.accept(*this);\n"; }
        else {
          my $expr=$const ? "static_cast<const Node&>(*n.$f->{name})" : "(*n.$f->{name})";
          $c.="  if(n.$f->{name}) $expr.accept(*this);\n";
        }
      }
    }
    $c.="  leave(n);\n}\n";
  }
}
$c.="namespace {\nnamespace detail = ::limestone::syntax::detail;\nconstexpr std::array<std::string_view, ".scalar(@nodes)."> symbols = {".join(', ',map {'"'.$_->{name}.'"'} @nodes)."};\n";
$c.="std::unique_ptr<$_->{name}> build_$_->{name}(detail::NodeView);\n" for @nodes;
for my $node (@nodes) {
  my $name=$node->{name};
  $c.="std::unique_ptr<$name> build_$name(detail::NodeView view) {\n  auto out=std::make_unique<$name>();\n  out->source=view.source();\n  detail::Fields fields(view,symbols);\n";
  if ($node->{kind} eq 'token') { $c.="  out->value=view.text();\n"; }
  elsif ($node->{kind} eq 'sum') {
    $c.="  constexpr std::array<std::string_view, ".scalar(@{$node->{alternatives}})."> alternatives = {".join(', ',map {'"'.$_.'"'} @{$node->{alternatives}})."};\n  auto child=fields.one_of(alternatives);\n";
    $c.="  if(child.symbol()==\"$_\") out->value=build_$_(child);\n" for @{$node->{alternatives}};
  }else {
    for my $f (@{$node->{fields}}) {
      my $args='"'.$f->{type}.'"';$args.=", $f->{index}" if defined $f->{index};
      if ($f->{card} eq 'many') {
        $c.="  for(auto child : fields.many($args)) out->$f->{name}.push_back(build_$f->{type}(child));\n";
      }elsif ($f->{card} eq 'optional') {
        $c.="  if(auto child=fields.optional($args)) out->$f->{name}=build_$f->{type}(*child);\n";
      }else { $c.="  out->$f->{name}=build_$f->{type}(fields.one($args));\n"; }
    }
  }
  $c.="  fields.finish();\n  return out;\n}\n";
}
$c.="} // namespace\n::limestone::Result<std::unique_ptr<$root>> parse(std::string_view input,std::string_view file,const ::limestone::syntax::ParseLimits& limits) {\n  using R=::limestone::Result<std::unique_ptr<$root>>;\n  auto tree=detail::parse_tree(parser_tables_$parser,input,file,limits);\n  if(!tree) return R::err(tree.error());\n  try { return R::ok(build_$root(tree.value()->root())); }\n  catch(const ::limestone::Error& error) { return R::err(error); }\n  catch(const std::bad_alloc&) { return R::err({::limestone::Error::Code::ResourceLimit,std::string(file)+\": AST allocation failed\"}); }\n}\n} // namespace $ns\n";
sub write_output {
  my ($path,$contents)=@_;
  return if -f $path && read_file($path) eq $contents;
  my ($volume,$directory)=File::Spec->splitpath($path);
  my ($fh,$temporary)=tempfile('.syngen-XXXXXX',DIR=>($directory||'.'),UNLINK=>1);
  print {$fh} $contents or die "syngen: $temporary: $!\n";
  close $fh or die "syngen: $temporary: $!\n";
  rename $temporary,$path or die "syngen: $path: $!\n";
}
write_output($header,$h);write_output($source,$c);
