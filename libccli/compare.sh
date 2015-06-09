#!/usr/bin/env sh
#
# This script uses CVC3 to check the integrity of query results

RUNNAME="$(basename "$0")"
if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

# COMPARE ======================================================================

compare()
{
	compare_wrong=0
	compare_array=''
	printf "%4s | %5s | %5s\n" 'q' 'norm' 'comp'
	echo '-----+-------+------'
	for i in $(seq $LIMIT); do
		smtd="$(./third/cvc3/bin/cvc3 -lang smt2 data/smtlibdump/$( printf "%04d" $i).smt)"
		cmpd="$(./third/cvc3/bin/cvc3 -lang smt2 data/compactdump/$(printf "%04d" $i).smt)"
		if [ "$smtd" != "$cmpd" ]; then
			compare_wrong=$(($compare_wrong + 1))
			compare_array="$compare_array $i"
			printf "\033[31m"
		fi
		printf "%4d | %5s | %5s\033[0m\n" $i "$smtd" "$cmpd"
	done 2>/dev/null
	echo '-----+-------+------'
	if [ $compare_wrong -eq 0 ]; then
		printf "\033[32mAll queries passed correctly\033[0m\n"
	else
		printf "\033[31m%d queries failed:%s\033[0m\n" \
			$compare_wrong "$compare_array"
	fi
}

# UTILITIES ====================================================================

die()
{
	die_retval=$1
	die_format="$2"
	shift 2
	printf "$die_format\n" "$@" >&2
	exit $die_retval
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME LIMIT
	EOF
}

get_limit()
{
	LIMIT="$1"
	if [ -z "$LIMIT" ]; then
		usage >&2
		exit 1
	fi
	case "$LIMIT" in
		''|*[!0-9]*) die 1 'Limit needs to be a numeric value' ;;
		*) ;;
	esac
	if [ $LIMIT -le 0 ]; then
		die 1 'Limit must be larger than zero'
	fi
	ARGSHIFT=1
}

main()
{
	get_limit "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die 1 'Unknown argument: %s' "$1"
	compare
}

set -e
main "$@"
set +e
