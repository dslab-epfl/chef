#!/bin/sh
set -e

# if git is not initialised, clone first:
if [ ! -d build/html/.git ]; then
	echo 'could not find git repository in build/html, cloning'
	rm -rf build/html/
	git clone -b gh-pages git@github.com:dslab-epfl/chef build/html
	make html
fi

cd build/html
numchange="$(git status --porcelain | wc -l)"
if [ "$numchange" != '0' ]; then
	echo "changes detected: ($numchange files)"
	git commit -a -v
	git push
else
	echo "no changes detected, not publishing"
fi
