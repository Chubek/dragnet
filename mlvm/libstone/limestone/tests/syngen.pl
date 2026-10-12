#!/usr/bin/env perl
use strict;
use warnings;
use Test::More;
use File::Temp qw(tempdir);
use File::Spec;
use IPC::Open3;
use Symbol qw(gensym);

my $root=shift @ARGV or die "expected source root\n";
my $directory=tempdir(CLEANUP=>1);
my $schema=File::Spec->catfile($directory,'sample.absyn');
my $header=File::Spec->catfile($directory,'sample.hpp');
my $source=File::Spec->catfile($directory,'sample.cpp');
my $grammar=File::Spec->catfile($directory,'sample.g');
sub write_file {
  my ($file,$text)=@_;open my $fh,'>',$file or die "$file: $!";print {$fh} $text;close $fh or die $!;
}
sub read_file { open my $fh,'<',$_[0] or die $!;local $/;return <$fh>; }
sub run {
  my $err=gensym;
  my $pid=open3(undef,my $out,$err,$^X,"$root/scripts/syngen.pl",@_);
  local $/;my $stdout=<$out>//'';my $stderr=<$err>//'';waitpid($pid,0);
  return ($?>>8,$stdout.$stderr);
}
for my $name (qw(bin2bin isa limeburg limestone machine-ir regtl schedrow traceml tuner unisel)) {
  my ($code,$message)=run('--grammar',"$root/parsers/$name.g",'--check',"$root/parsers/$name.absyn");
  is($code,0,"$name schema matches grammar") or diag($message);
}
my $prefix="namespace test::syntax;\nparser sample;\nroot Document;\n";
write_file($schema,$prefix.'node Document { Token first @0; Token* rest @1; } token Token;');
write_file($grammar,'Document: Token+; Token: "[a-z]+"; whitespace: "[ ]*";');
my @options=('--grammar',$grammar,'--header',$header,'--source',$source,$schema);
my ($code,$message)=run(@options);is($code,0,'generate ordered captures') or diag($message);
my $expected_header=read_file($header);my $expected_source=read_file($source);
($code,$message)=run(@options);is($code,0,'generation can be repeated');
is(read_file($header),$expected_header,'header generation is deterministic');
is(read_file($source),$expected_source,'source generation is deterministic');
like($expected_header,qr/ConstRecursiveVisitor/,'const recursive visitor generated');
like($expected_source,qr/static_cast<const Node&>/,'const walker propagates constness');
for my $case (
  ['node Document {} token Document;',qr/duplicate/],
  ['node Document { Missing child; }',qr/unknown field type/,qr/sample\.absyn:4:17:/],
  ['sum Document = Missing;',qr/unknown sum alternative/,qr/sample\.absyn:4:16:/],
  ['node Document { Token a; Token b; } token Token;',qr/require explicit indices/],
  ['node Document { Token a @1; Token* b @0; } token Token;',qr/overlapping/],
  ['node Document { Token a @0; Token b @0; } token Token;',qr/duplicate .*index/],
  ['node Document { Token source; } token Token;',qr/reserved field/],
  ['node Document { Token class; } token Token;',qr/reserved C\+\+ identifier/],
  ['node Document { Token __child; } token Token;',qr/reserved C\+\+ identifier/],
  ['node Document {} token Token;',qr/unmapped grammar child/],
  ['node Document { Token x; } token Token; token Extra;',qr/absent from grammar/],
  ['node Document { Token x;',qr/expected identifier/],
  ['node Document {} @',qr/expected node/],
  ['node Document {} "injected"',qr/invalid schema character/]
) {
  write_file($schema,$prefix.$case->[0]);
  ($code,$message)=run(@options);isnt($code,0,'reject malformed schema');like($message,$case->[1],'precise diagnostic');
  like($message,qr/sample\.absyn:\d+:\d+:/,'diagnostic contains source coordinates');
  like($message,$case->[2],'semantic diagnostic points to offending type') if $case->[2];
  is(read_file($header),$expected_header,'failed validation preserves header');
  is(read_file($source),$expected_source,'failed validation preserves source');
}
write_file($schema,$prefix.'node Document { Token first; } token Token;');
($code,$message)=run('--header',$schema,'--source',$source,$schema);
isnt($code,0,'input cannot be overwritten');like($message,qr/overwrite input/,'overwrite diagnostic');
done_testing();
