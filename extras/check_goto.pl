#!/usr/bin/env perl

use strict;
use warnings;

my @ignore_labels = qw (TODO retry fetch_data again try_again sp_state_read_proghdr redo disabled_loop fd_alloc_try_again);
my @ignore_files  = qw (y.tab.c lex.c);
my @c_files;
my $line;
my @labels;
my $in_comments;

{
        local $" = "|";
        my $cmd = "find . -type f -name '*.c' | grep -vE '(@ignore_files)'";
        @c_files = `$cmd`;
}

foreach my $file (@c_files) {
        chomp ($file);
        open FD, $file or die ("Failed to read file $file: $!");
        @labels = ();
        $in_comments = 0;
        while ($line = <FD>) {
                chomp ($line);

                next if $line =~ /^\s*(#|\/\/)/;
                $in_comments = 1 if ($line =~ /\/\*/);
                $in_comments = 0 if ($line =~ /\*\//);

                next if $in_comments;
                if ($line =~ /^\s*(([a-zA-Z]|_)\w*)\s*:/) {
                        push (@labels, $1) unless grep (/$1/, @ignore_labels);
                }
                @labels = () if $line =~ /^}/;

                next unless @labels;
                if ($line =~ /^\s*goto\s*(\w+)/) {
                        print "$file:$.: $line\n" if grep /^$1$/, @labels;
                }
        }

        close FD;
}

