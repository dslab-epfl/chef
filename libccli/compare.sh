#!/usr/bin/env sh
#
# This script uses CVC3 to check the integrity of query results

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# COMPARE ======================================================================

compare()
{
	case "$SOLVER" in
	z3)
		SOLVER_BIN="$($WHICH $SOLVER)"
		SOLVER_ARGS='-smt2'
		;;
	cvc3)
		SOLVER_BIN="$(find "$SRCPATH_ROOT" \
		              -name 'cvc3' \
		              -executable \
		              -type f)"
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
	for i in $(seq $RANGE_OFFSET $(($RANGE_OFFSET + $RANGE_LENGTH - 1))); do
		# original
		original_colour=''
		original="$("$SOLVER_BIN" \
		            $SOLVER_ARGS \
		            "$SRCPATH_ROOT/data/smtlibdump/$( printf "%04d" $i).smt")" \
		            || { original='error'; original_colour="\033[31m"; }

		# compact
		compact_colour="\033[32m"
		compact="$("$SOLVER_BIN" \
		           $SOLVER_ARGS \
		           "$SRCPATH_ROOT/data/compactdump/$(printf "%04d" $i).smt")" \
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
		success "queries passed (%d)\n" $RANGE_LENGTH
	else
		fail "queries failed (%d):%s\n" $compare_wrong "$compare_array"
	fi

	# Detailed output (if single)
	if [ $compare_wrong -eq 1 ] || [ $RANGE_LENGTH -eq 1 ]; then
		success "original:\n"
		"$SOLVER_BIN" \
			$SOLVER_ARGS \
			"$SRCPATH_ROOT/data/smtlibdump/$( printf "%04d" $i).smt"
		fail "compact:\n"
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
	DIRECT=$(as_boolean $DIRECT)
	EOF
}

# DOCKER =======================================================================

docker_compare()
{
	docker run \
		-t \
		-i \
		--rm \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		dslab/s2e-chef:v0.6 \
		"$DOCKER_HOSTPATH/$RUNDIR/$RUNNAME" \
			-z \
			"$SOLVER" \
			$RANGE_OFFSET \
			$RANGE_LENGTH
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] SOLVER OFFSET [LENGTH]
	EOF
}

help()
{
	usage

	cat <<- EOF

	Solvers:
	  z3
	  cvc3
	  stp        (not yet supported)

	Offset, Length:
	  Strictly positive values denoting the first query to start with, and the
	  number of queries to compare. The length is 1 by default.

	Options:
	  -h         Display this help
	  -y         Dry run: print variables and exit
	  -z         Direct mode (don't use docker)
	EOF
}

get_options()
{
	DIRECT=$FALSE
	DRYRUN=$FALSE

	while getopts hyz opt; do
		case "$opt" in
			h) help; exit 1 ;;
			y) DRYRUN=$TRUE ;;
			z) DIRECT=$TRUE ;;
			'?') die_help ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

get_solver()
{
	SOLVER="$1"
	test -n "$SOLVER" || die_help 'Missing solver'
	case "$SOLVER" in
		z3|cvc3) ;;
		stp) die 1 'This solver is not supported yet' ;;
		*) die_help 'Unknown solver: %s' "$SOLVER" ;;
	esac
	ARGSHIFT=1
}

get_range_offset()
{
	RANGE_OFFSET="$1"
	test -n "$RANGE_OFFSET" || die_help 'Missing offset'
	is_numeric "$RANGE_OFFSET" || die 1 'Offset needs to be a numeric value'
	test $RANGE_OFFSET -gt 0 || die 1 'Offset must be larger than zero'
	ARGSHIFT=1
}

get_range_length()
{
	RANGE_LENGTH="$1"
	if [ -z "$RANGE_LENGTH" ]; then
		RANGE_LENGTH=1
		ARGSHIFT=0
	else
		is_numeric "$RANGE_LENGTH" || die 1 'Length needs to be a numeric value'
		test $RANGE_LENGTH -gt 0 || die 1 'Length must be larger than zero'
		ARGSHIFT=1
	fi
}

main()
{
	for getargs in get_options get_solver get_range_offset get_range_length; do
		$getargs "$@"
		shift $ARGSHIFT
	done
	test $# -eq 0 || die_help "trailing arguments: @"

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
