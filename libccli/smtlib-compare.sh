#!/usr/bin/env sh
#
# This script uses CVC3 to check the integrity of query results

. "$(readlink -f "$(dirname "$0")")/utils.sh"

DOCKER_DUMPPATH1='/host1'
DOCKER_DUMPPATH2='/host2'

# COMPARE ======================================================================

compare()
{
	case "$SOLVER" in
	z3)
		if ! is_command "$SOLVER"; then
			die 2 'Could not find Z3 installation on this system'
		fi
		SOLVER_BIN="$SOLVER"
		SOLVER_ARGS='-smt2'
		;;
	cvc3)
		SOLVER_BIN="$(find "$SRCPATH_ROOT" \
		              -name 'cvc3' \
		              -executable \
		              -type f)"
		if [ -z "$SOLVER_BIN" ]; then
			die 2 'Could not find CVC3 executable in project directory tree.'
		fi
		SOLVER_ARGS='-lang smt2'
		;;
	*)
		die_internal 'compare(): invalid solver %s' "$SOLVER"
		;;
	esac

	compare_wrong=0
	compare_array=''

	# List output comparison
	printf "query| orig  | comp\n"
	echo   '-----+-------+------'
	for i in $IDS; do
		# original
		original_colour=''
		original="$("$SOLVER_BIN" \
		            $SOLVER_ARGS \
		            "$DUMPPATH1/$(printf "%04d" $i).smt")" \
		            || { original='error'; original_colour="\033[31m"; }

		# compact
		compact_colour="\033[32m"
		compact="$("$SOLVER_BIN" \
		           $SOLVER_ARGS \
		           "$DUMPPATH2/$(printf "%04d" $i).smt")" \
		           || { compact='invld'; compact_colour="\033[31m"; }

		# keep track
		if [ "$original" != "$compact" ]; then
			compare_wrong=$(($compare_wrong + 1))
			compare_array="$compare_array $i"
		fi

		# display
		printf "%4d | $original_colour%5s | $compact_colour%5s\033[0m\n" \
			$i "$original" "$compact"
	done 2>/dev/null
	echo '-----+-------+------'

	# Summary
	if [ $compare_wrong -eq 0 ]; then
		success "queries passed (%d)\n" $ID_COUNT
	else
		fail "queries failed (%d):%s\n" $compare_wrong "$compare_array"
	fi

	# Detailed output (if single)
	if [ $compare_wrong -eq 1 ] || [ $ID_COUNT -eq 1 ]; then
		emphasised $COLOUR_MISC "original:\n"
		"$SOLVER_BIN" \
			$SOLVER_ARGS \
			"$SRCPATH_ROOT/data/smtlibdump/$( printf "%04d" $i).smt"
		emphasised $COLOUR_MISC "compact:\n"
		"$SOLVER_BIN" \
			$SOLVER_ARGS \
			"$SRCPATH_ROOT/data/compactdump/$( printf "%04d" $i).smt"
	fi
}

# DRYRUN =======================================================================

dryrun()
{
	util_dryrun
	cat <<- EOF
	RANGE_OFFSET=$RANGE_OFFSET
	RANGE_LENGTH=$RANGE_LENGTH
	SOLVER=$SOLVER
	EOF
}

# DOCKER =======================================================================

docker_compare()
{
	exec docker run \
		-t \
		-i \
		--rm \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		-v "$DUMPPATH1":"$DOCKER_DUMPPATH1" \
		-v "$DUMPPATH2":"$DOCKER_DUMPPATH2" \
		dslab/s2e-chef:v0.6 \
		"$DOCKER_HOSTPATH/$RUNDIR/$RUNNAME" \
			-s "$SOLVER" \
			-z \
			"$DOCKER_DUMPPATH1":"$DOCKER_DUMPPATH2" \
			"$IDS"
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] DUMPPATH1:DUMPPATH2 IDS
	EOF
}

help()
{
	usage

	cat <<- EOF

	Options:
	  -h         Display this help
	  -s SOLVER  Use solver SOLVER [default=$SOLVER]
	  -y         Dry run: print variables and exit
	  -z         Direct mode (don't use docker)

	Solvers:
	  z3  cvc3  stp  (STP not supported yet)

	DUMPPATH1:DUMPPATH2
	  Directories containing the queries that will be compared to each other.

	IDs:
	  The query IDs can be given as a comma-separated list of numbers or ranges,
	  where a range is two numbers separated by a dash.
	EOF
}

get_options()
{
	DIRECT=$DEFAULT_DIRECT
	DRYRUN=$FALSE
	SOLVER='z3'

	while getopts :hs:yz opt; do
		case "$opt" in
			h) help; exit 1 ;;
			s) SOLVER="$OPTARG" ;;
			y) DRYRUN=$TRUE ;;
			z) DIRECT=$TRUE ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done

	case "$SOLVER" in
		z3|cvc3) ;;
		stp) die 1 'This solver is not supported yet' ;;
		*) die_help 'Unknown solver: %s' "$SOLVER" ;;
	esac
	ARGSHIFT=$(($OPTIND - 1))
}

get_directories()
{
	DIRS="$1"
	DUMPPATH1="$(readlink -f "$(echo "$DIRS" | cut -d ':' -f 1)")"
	DUMPPATH2="$(readlink -f "$(echo "$DIRS" | cut -d ':' -f 2)")"
	for dir in "$DUMPPATH1" "$DUMPPATH2"; do
		test -n "$dir" || die_help 'Missing directory'
		test -d "$dir" || die 2 '%s: Directory not found' "$dir"
		test -r "$dir" || die 3 '%s: permission denied' "$dir"
		if ! find "$dir"/*.smt >"$NULL" 2>"$NULL"; then
			die 2 "$dir does not seem to contain any SMT-LIB query dump files"
		fi
	done
	if [ "$DUMPPATH1" = "$DUMPPATH2" ]; then
		die_help 'Please specify a second, different directory'
	fi
	ARGSHIFT=1
}

get_ids()
{
	IDS="$1"
	test -n "$IDS" || die_help 'Missing ID list'
	IDS="$(list_expand "$IDS")"
	IDS="$(for id in $IDS; do range_expand "$id"; done | uniq | sort -n)"
	ID_COUNT=$(echo "$IDS" | wc -l)
	ARGSHIFT=1
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_directories "$@"
	shift $ARGSHIFT
	get_ids "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "trailing arguments: $@"

	if [ $DRYRUN -eq $TRUE ]; then
		dryrun
		exit
	fi

	if [ $DIRECT -eq $TRUE ]; then
		compare
	else
		docker_compare
	fi
}

set -e
main "$@"
set +e
