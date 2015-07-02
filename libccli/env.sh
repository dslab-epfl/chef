#!/usr/bin/env sh

# Prints Chef-specific environment variables

# UTILITIES ====================================================================

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# ENV ==========================================================================

ccli_env()
{
	cat <<- EOF
	CCLI_ARCH=$CCLI_ARCH
	CCLI_TARGET=$CCLI_TARGET
	CCLI_MODE=$CCLI_MODE
	CCLI_SILENT_BUILD=$CCLI_SILENT_BUILD  # $(as_boolean $CCLI_SILENT_BUILD)
	EOF
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS]
	EOF
}

help()
{
	usage
	cat <<- EOF

	Print Chef-specific environment variables.

	Options:
	  -h     Display this help.
	EOF
}

get_options()
{
	while getopts h opt; do
		case "$opt" in
			h) help; exit 1 ;;
			'?') die_help ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "Trailing argument: $@"

	ccli_env
}

set -e
main "$@"
set +e
