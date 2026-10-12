use strict;
use warnings;
use File::Spec;
my $root=shift @ARGV or die "expected repository root\n";
sub read_text {
  my ($path)=@_;open my $file,'<',$path or die "cannot read $path: $!\n";
  local $/;return <$file>;
}
my $manifest=read_text("$root/third_party/exolangtk/manifests/Exolayer-Modules.yaml");
my ($path)=$manifest=~/header:\s*(\S+)/ or die "missing manifest header\n";
my $header=read_text(File::Spec->catfile($root,'third_party','exolangtk',$path));
my %provided;
while($manifest=~/(?:types|functions|macros|constants):\s*\[([^\]]*)\]/g) {
  for my $symbol(split /,\s*/,$1) {
    $symbol=~s/^\s+|\s+$//g;
    die "unprefixed Exolayer symbol $symbol\n" unless $symbol=~/^(?:exl_|EXL_)/;
    die "duplicate symbol $symbol\n" if $provided{$symbol}++;
    die "undeclared manifest symbol $symbol\n" unless $header=~/\b\Q$symbol\E\b/;
  }
}
$header=~s@/\*.*?\*/@@sg;
while($header=~/\bEXL_DEF\s+[^;]*?\b(exl_\w+)\s*\(/g) {
  die "public function missing from manifest: $1\n" unless $provided{$1};
}
while($header=~/#\s*define\s+(EXL_\w+)/g) {
  next if $1 eq 'EXL_EXOLAYER_H';
  die "public macro missing from manifest: $1\n" unless $provided{$1};
}
print "Exolayer manifest: ",scalar(keys %provided)," symbols verified\n";
