#!/usr/bin/env sh

echo "This script shows examples for using GNU parallel."

# For the case someone executes this script:
sed -n '/^exit/,$p' "$0" | tail -n +2 | if which highlight >/dev/null 2>&1
then highlight --syntax=sh --out-format=ansi; else cat; fi
exit

# Run multiple invocations of sleep:
parallel \
	-j2 \
	--results gptest \
	--joblog gptest.log \
	--header : \
	sleep '{duration}' \; echo "{duration}" ::: duration 1 2 3 4
