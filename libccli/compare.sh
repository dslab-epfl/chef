#!/usr/bin/env sh

compare()
{
	printf "%4s | %5s | %5s\n" 'q' 'norm' 'comp'
	echo '-----+-------+------'
	for i in {1..12}; do
		smtd="$(./third/cvc3/bin/cvc3 -lang smt2 data/smtlibdump/$( printf "%04d" $i).smt)"
		cmpd="$(./third/cvc3/bin/cvc3 -lang smt2 data/compactdump/$(printf "%04d" $i).smt)"
		if [ "$smtd" != "$cmpd" ]; then
			printf "\033[31m"
		fi
		printf "%4d | %5s | %5s\033[0m\n" $i "$smtd" "$cmpd"
	done 2>/dev/null
}

# MAIN =========================================================================

get_options()
{
	LIMIT=0
	while getopts hl: opt; do
		case "$opt" in
			h) help; exit 1 ;;
			l) LIMIT="$OPTARG" ;;
			'?') die_help ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	compare
}

set -e
main "$@"
set +e
