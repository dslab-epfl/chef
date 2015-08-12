#!/bin/sh
set -e

# Make sure the git repository is there:
if [ ! -d build/html/.git ]; then
	echo 'could not find git repository in build/html, cloning'
	rm -rf build/html/
	git clone -b gh-pages git@github.com:dslab-epfl/chef build/html
fi

# Make sure the compilation is up to date:
make html

# Commit and push:
cd build/html
numchange="$(git status --porcelain | wc -l)"
if [ "$numchange" != '0' ]; then
	echo "changes detected: ($numchange files)"
	git commit -a -v
	git push
else
	echo "no changes detected, not publishing"
fi
