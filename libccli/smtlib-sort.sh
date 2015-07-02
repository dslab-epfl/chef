#!/usr/bin/env sh
#
# This script searches for interesting query dumps in a directory.
# "Interesting" is naively defined as a high ratio r = t/s, where t = query
# duration, and s = query dump size.

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# SORT =========================================================================

smtlib_sort()
{
	n=$(find "$DIRECTORY"/*.smt 2>/dev/null | wc -l)
	if [ $n -eq 0 ]; then
		die 2 '%s does not contain any query dumps' "$DIRECTORY"
	fi
	for query in "$DIRECTORY"/*.smt; do
		t=$(head -n 1 "$query" | awk '{print $2}')
		s=$(stat -c '%s' "$query")
		r=$(dc -e "5k $t $s / p")
		printf "%f µs/B: %s\n" $r "$query"
	done | sort | tail -n $LINES | tac
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] [DIRECTORY]
	EOF
}

help()
{
	usage

	cat <<- EOF

	Options:
	  -h        Display this help
	  -l LINES  Limit output to LINES lines [default=$LINES]

	If DIRECTORY is omitted, the current working directory is assumed.
	EOF
}

get_options()
{
	LINES=10

	while getopts hl: opt; do
		case "$opt" in
			h) help; exit 1 ;;
			l) LINES="$OPTARG" ;;
			'?') die_help ;;
		esac
	done
	is_numeric "$LINES" || die_help 'Limit must be numeric'
	ARGSHIFT=$(($OPTIND - 1))
}

get_directory()
{
	DIRECTORY="$1"
	if [ -z "$DIRECTORY" ]; then
		DIRECTORY="$(pwd)"
		ARGSHIFT=0
	else
		ARGSHIFT=1
	fi
	test -d "$DIRECTORY" || die 2 '%s: directory does not exist' "$DIRECTORY"
	test -r "$DIRECTORY" || die 2 '%s: read permission denied' "$DIRECTORY"
}

main()
{
	if ! is_command dc; then
		die 2 'dc is not available on this system'
	fi

	for getargs in get_options get_directory; do
		$getargs "$@"
		shift $ARGSHIFT
	done
	test $# -eq 0 || die_help "trailing arguments: $@"

	smtlib_sort
}

set -e
main "$@"
set +e
