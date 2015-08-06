#!/usr/bin/env sh
set -e

# This script dumps queries from sqlite3 databases in SMT-Lib format.
#
# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# DUMP =========================================================================

dump()
{
	TOOL_BIN="$RELEASEPATH/klee/$ASSERTS/bin/query-tool"

	test -e "$TOOL_BIN" || die 2 '%s: file not found' "$TOOL_BIN"

	"$TOOL_BIN" \
		-end-solver="$SOLVER" \
		-dump-smtlib=$(test $MONOLITHIC -eq $TRUE && echo 'single' || echo 'separate') \
		-dump-smtlib-path="$DUMP_PATH/" \
		$(test $HUMAN -eq $TRUE && echo '-smtlib-human-readable') \
		$(test $COMPACT -eq $TRUE && echo '-smtlib-compact') \
		"$DB_FILE" \
		$IDS_EXPANDED
}

# DRY RUN ======================================================================

dryrun()
{
	cat <<- EOF
	SOLVER=$SOLVER
	DB_FILE=$DB_FILE
	MONOLITHIC=$(as_boolean $MONOLITHIC)
	HUMAN=$(as_boolean $HUMAN)
	EOF
	exit 1
}

# UTILITIES ====================================================================

usage() { echo "Usage: $INVOKENAME [OPTIONS ...] EXPNAME IDS DUMPNAME"; }
help()
{
	usage
	cat <<- EOF

	Options:
	  -c             Print compact SMTLIB (experimental!)
	  -h             Display this help
	  -M             Monolithic dump (no separate files)
	  -r RELEASE     Release tuple [default=$DEFAULT_RELEASE]
	  -s {z3,cvc3}   Use this solver [default=$SOLVER]
	  -w             Use whitespace to make it human-readable
	  -y             Dry run: print runtime variables and exit

	IDs:
	  The query IDs can be given as a comma-separated list of numbers or ranges,
	  where a range is two numbers separated by a dash.

	Experiment name:
	  The name of the experiment data generated with \`ctl run ... sym\`.

	Dump name:
	  Name of the dump - can be used for \`ctl smtlib-compare\`.
	EOF
}

get_options()
{
	COMPACT=$FALSE
	MONOLITHIC=$FALSE
	RELEASE="$DEFAULT_RELEASE"
	SOLVER='stp'
	HUMAN=$FALSE
	DRYRUN=$FALSE

	while getopts :chMr:s:wy opt; do
		case "$opt" in
			c) COMPACT=$TRUE ;;
			h) help; exit 1 ;;
			M) MONOLITHIC=$TRUE ;;
			r) RELEASE="$OPTARG" ;;
			s) SOLVER="$OPTARG" ;;
			w) HUMAN=$TRUE ;;
			y) DRYRUN=$TRUE ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done

	parse_release "$RELEASE"
	test -d "$RELEASEPATH" || die 1 '%s: release not found' "$RELEASEPATH"
	ARGSHIFT=$(($OPTIND - 1))
}

get_expname()
{
	# TODO convert EXPNAME to EXPDBPATH
	die_help 'get_expname(): experiment names not implemented yet'
}

get_ids()
{
	IDS="$1"
	test -n "$IDS" || die_help 'Missing ID list'
	IDS_EXPANDED="$(for id in $(list_expand "$IDS"); do \
	                    range_expand "$id"; \
	                done | uniq | sort -n)"
	ID_COUNT=$(printf "$IDS_EXPANDED" | wc -l)
	test -n "$IDS_EXPANDED" || die_help 'ID list syntax error'
	ARGSHIFT=1
}

get_dumpname()
{
	# TODO convert DUMPNAME to DUMPPATH
	die_help 'get_dumpname(): dump names not implemented yet'
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_expname "$@"
	shift $ARGSHIFT
	get_ids "$@"
	shift $ARGSHIFT
	get_dumpname "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "Trailing arguments: $@"

	test $DRYRUN -eq $FALSE || dryrun

	dump
}
main "$@"
