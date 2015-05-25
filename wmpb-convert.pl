#!/usr/bin/perl -w

#
# wmpinboard pre-0.7 to v0.7 data file converter
#

require 5.003;

my($ifn, $ofn, $c) = ('.wmpinboardrc', '.wmpinboarddata', 0);

die "This script should not be executed while an instance of wmpinboard is " .
  "running.\n" if `ps x` =~ /\d wmpinboard/;

if (open IN, "$ENV{HOME}/$ifn" and open OUT, ">$ENV{HOME}/$ofn") {
  binmode OUT;
  print OUT "WMPB0-*-fixed-*--10-*-iso8859-1\n";
  while (<IN>) {
    chomp;
    if (@_ = /^(\d+) (\d+) (\d+) (.*)/) {
      if (length $4 <= 39) {
        $_[3] =~ s/.{8}/$&\n/gs;
        $_[3] = join '  ', split /\n/, $_[3];
      }
      $_[3] .= ' ' x (59 - length $_[3]);
      print OUT pack 'i3a60', @_;
      $c++
    } else {
      die "Invalid data in `~/.$ifn'.\nAborting" if $_
    }
  }
  close IN;
  close OUT;
  print "Conversion of $c note@{[$c != 1 ? 's' : '']} complete.\n";
  unlink "$ENV{HOME}/$ifn"
} else {
  die "Couldn't open both `~/$ifn' and `~/$ofn'.\n" .
      "Sure you've run this script as the right user?\n"
}

