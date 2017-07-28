#!/usr/bin/perl

use strict;
use warnings;
use Digest;
use String::CRC32 ();
use JSON;

sub read_file($@) {
  my ($mode, @args) = @_;
  open my $f, $mode, @args or die "@args: $!\n";
  local $/;
  <$f>
}

my @algos = qw{CRC32 MD5 SHA-1 SHA-256 SHA-512};

my $ref = read_file "/usr/share/common-licenses/GPL-2";

my @digests;
push @digests, { name => "CRC32", tag => "crc32",
  compute => sub { sprintf "%08x", String::CRC32::crc32($_[0]) } };
for my $a (@algos[1 .. $#algos]) {
  my $t = lc $a;
  $t =~ s/-//;
  my $d = Digest->new($a);
  my $f = sub { $d->add($_[0]); $d->hexdigest };
  push @digests, { name => $a, tag => $t, compute => $f };
}

sub par_rng($@) {
  my ($bound, @val) = @_;
  my $t = 3141592653;
  for my $x (@val) {
    $t += ($x * 577) ^ 2718281828;
    $t &= 0xFFFFFFFF;
  }
  return $t % $bound;
}

die "Non-standard GPL-2\n" unless $digests[3]->{compute}->($ref) eq
  "8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643";

my @files;
my %dirs;

sub create_parent_dirs($) {
  my ($path) = @_;
  my $rpath = "tests";
  for my $d (split "/", $path) {
    if (!$dirs{$rpath}++) {
      mkdir $rpath;
      chmod 0755, $rpath;
      push @files, { path => $rpath, type => "D", mode => "0755" };
    }
    $rpath .= "/$d";
  }
}

sub create_file($$$) {
  my ($path, $size, $mode) = @_;
  my $data = substr($ref, par_rng(length($ref) - $size, 1, $size), $size);
  substr($data, -1, 1) = "\n" if $size > 0;
  create_parent_dirs $path;
  my $rpath = "tests/$path";
  open my $f, ">", $rpath or die "$rpath: $!\n";
  print $f $data;
  chmod $mode, $f;
  push @files, { path => $rpath, type => "F",
    mode => sprintf("%04o", $mode), size => $size, hash =>
    { map { $_->{tag}, $_->{compute}->($data) } @digests },
  };
}

sub create_symlink($$) {
  my ($path, $target) = @_;
  create_parent_dirs $path;
  my $rpath = "tests/$path";
  unlink $rpath;
  symlink $target, $rpath or die "$rpath -> $target: $!\n";
  push @files, { path => $rpath, type => "L", target => $target,
    mode => "0777",
  };
}

my $long = "0123456789" x 4;

create_file "test1", 10000, 0644;
create_file "subdir/test2", 2000, 0644;
create_file "private", 1500, 0600;
create_file "empty", 0, 0644;
create_file "integer", 512 * 7, 0644;
create_file "long_directory_name_${long}/long_file_name_${long}", 2500, 0644;
create_symlink "symlink", "test1";
#create_symlink "long_symlink",
#  "long_directory_name_${long}/long_file_name_${long}";

#unshift @files, { path => "tests/", type => "D" };
$files[0]->{path} .= "/";

@files = sort { $a->{path} cmp $b->{path} } @files;
for my $f (@files) {
  undef $!;
  $f->{mtime} = (lstat $f->{path})[9];
  $f->{path} =~ s/^tests//;
}

my @reg_files;
my $out1_ref = "";
my $out2_ref = "";
my $out3_ref = "{\n   \"files\" : [\n";
my $idx = 0;
for my $f (@files) {
  if ($f->{type} eq "F") {
    my $p = "tests" . $f->{path};
    push @reg_files, $p;
    for my $d (@digests) {
      my $t = $d->{tag};
      my $h = $f->{hash}->{$t};
      $out1_ref .= sprintf "%s:%s  %s\n", $t, $h, $p;
      $out2_ref .= sprintf "%s:%s  %09d\n", $t, $h, $idx;
    }
    $idx++;
  }
  $out3_ref .= "      {\n";
  for my $t (qw{path type target +size +mtime mode}) {
    my $t = $t;
    my $quot = $t =~ s/^\+// ? "" : "\"";
    my $v = $f->{$t};
    next unless defined $v;
    $out3_ref .= "         \"$t\" : $quot$v$quot,\n";
  }
  my $h = $f->{hash};
  if (defined $h) {
    $out3_ref .= "         \"hash\" : {\n";
    for my $d (@digests) {
      my $t = $d->{tag};
      my $h = $f->{hash}->{$t};
      $out3_ref .= sprintf "            \"%s\" : \"%s\",\n", $t, $h;
    }
    $out3_ref .= "         },\n";
  }
  $out3_ref .= "      },\n";
}
$out3_ref .= "   ]\n}\n";
$out3_ref =~ s/,(?=\s*[\}\]])//gs or die; # JSON sucks
my $out4_ref = $out3_ref;
$out4_ref =~ s/"path" : "\//"path" : "tests\//g or die;
$out4_ref =~ s/"tests\/"/"tests"/ or die; # exception

my $out1 = read_file "-|", "./multihash", "-C", @reg_files;
my $out2 = read_file "-|", "./multihash", "-Cs", @reg_files;
my $out3 = read_file "-|", "./multihash", "-Cr", "tests";
my $out4 = read_file "-|", "tar c tests | ./multihash -Ct";

sub test_success($$$) {
  my ($label, $ref, $out) = @_;
  if ($ref eq $out) {
    print "$label success\n";
    return;
  }
  my @ref = split "\n", $ref;
  my @out = split "\n", $out;
  my $line = 1;
  while (@ref && @out && $ref[0] eq $out[0]) {
    shift @ref;
    shift @out;
    $line++;
  }
  push @ref, "<EOF>" unless @ref;
  push @out, "<EOF>" unless @out;
  print "$label failed, line $line:\n-", $ref[0], "\n+", $out[0], "\n";
  exit 1;
}

# Order in tar unpredictible:
# remove the surrounding brackets, split, sort, join.
my $pfx4r = substr($out4_ref, 0, 17, "");
my $pfx4c = substr($out4,     0, 17, "");
die "multihash -Ct failed in prefix\n" unless $pfx4r eq $pfx4c;
my $sfx4r = substr($out4_ref, -7, 7, "");
my $sfx4c = substr($out4,     -7, 7, "");
die "multihash -Ct failed in suffix\n" unless $sfx4r eq $sfx4c;
my @out4 = split /\n(?=      \{)/, $out4;
$out4[-1] .= ",";
@out4 = sort @out4;
$out4[-1] =~ s/\,$// or die;
$out4 = join("\n", @out4);

test_success "multihash -C", $out1_ref, $out1;
test_success "multihash -Cs", $out2_ref, $out2;
test_success "multihash -Cr", $out3_ref, $out3;
test_success "multihash -Ct", $out4_ref, $out4;
