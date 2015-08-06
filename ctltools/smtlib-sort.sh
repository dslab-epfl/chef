#!/usr/bin/env sh
set -e

# This script searches for interesting query dumps in a directory.
# "Interesting" is naively defined as a high ratio r = t/s, where t = query
# duration, and s = query dump size.
#
# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# SORT =========================================================================

smtlib_sort()
{
	n=$(find "$DUMP_PATH"/*.smt 2>/dev/null | wc -l)
	if [ $n -eq 0 ]; then
		die 2 '%s does not contain any query dumps' "$DUMP_PATH"
	fi
	for query in "$DUMP_PATH"/*.smt; do
		t=$(head -n 1 "$query" | awk '{print $2}')
		s=$(stat -c '%s' "$query")
		r=$(dc -e "5k $t $s / p")
		printf "%f µs/B: %s\n" $r "$query"
	done | sort | tail -n $LIMIT | tac
}

# MAIN =========================================================================

usage() { echo "Usage: $INVOKENAME [OPTIONS ...] DUMPNAME"; }
help()
{
	usage
	cat <<- EOF

	Options:
	  -h        Display this help
	  -l LIMIT  Limit output to top LIMIT results [default=$LIMIT]
	EOF
}

get_options()
{
	LIMIT=10

	while getopts :hl: opt; do
		case "$opt" in
			h) help; exit 1 ;;
			l) LIMIT="$OPTARG" ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done
	is_numeric "$LIMIT" || die_help 'Limit must be numeric'
	ARGSHIFT=$(($OPTIND - 1))
}

get_dumpname()
{
	# TODO translate DUMPNAME to DUMPPATH
	die_internal 'get_dump_id(): dump names not implemented yet'
}

main()
{
	is_command dc || die 1 'dc is not available on this system'

	get_options "$@"
	shift $ARGSHIFT
	get_dumpname "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "trailing arguments: $@"

	smtlib_sort
}
main "$@"
