#!/bin/bash
# pdfgen.sh simple pdf generation helper script.
# Copyright (C) 2012-2013  James Shubin
# Written by James Shubin <james@shubin.ca>

#dir='/tmp/pdf'
dir=`pwd`'/output/'
ln -s ../images images
mkdir -p "$dir"

for i in *.md; do
	pandoc $i -o "$dir"`echo $i | sed 's/\.md$/\.pdf/'`
done

rm images	# remove symlink

