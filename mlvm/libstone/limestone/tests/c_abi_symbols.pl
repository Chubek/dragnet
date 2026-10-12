use strict;
use warnings;
my ($root,$nm,@libraries)=@ARGV;
die "expected repository, nm and C ABI libraries\n" unless defined($nm) && @libraries;
my %declared;
for my $name (qw(limestone.h il.h runtime.h optimization.h object.h traceml.h traceml_native.h)) {
    open my $file,'<',"$root/limestone/$name" or die "cannot read $name: $!\n";
    local $/;my $header=<$file>;
    $header=~s@/\*.*?\*/@@sg;
    while($header=~/\b(limestone_\w+)\s*\((?!\s*\*)/g) {
        die "duplicate public declaration $1\n" if $declared{$1}++;
    }
}
my %defined;
for my $library (@libraries) {
    open my $symbols,'-|',$nm,'-g','--defined-only',$library or die "cannot inspect $library: $!\n";
    while(<$symbols>) {
        if(/\bT\s+(limestone_\w+)\s*$/) {++$defined{$1};}
    }
    close $symbols or die "symbol inspection failed for $library\n";
}
for my $symbol (sort keys %declared) {
    die "missing or duplicate public definition $symbol\n" unless ($defined{$symbol}//0)==1;
}
for my $symbol (sort keys %defined) {
    die "undeclared public C symbol $symbol\n" unless $declared{$symbol};
}
print "Limestone C ABI: ",scalar(keys %declared)," declared symbols verified\n";
