#!/usr/bin/env sh

# Prints Chef-specific environment variables

# UTILITIES ====================================================================

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# ENV ==========================================================================

ccli_env()
{
	cat <<- EOF
	CHEF_ARCH=$CHEF_ARCH
	CHEF_TARGET=$CHEF_TARGET
	CHEF_MODE=$CHEF_MODE
	CHEF_VERBOSE=$CHEF_VERBOSE
	CHEF_DOCKERIZED=$CHEF_DOCKERIZED
	CHEF_DATAROOT=$CHEF_DATAROOT
	CHEF_DATAROOT_VMROOT=$CHEF_DATAROOT_VM
	CHEF_DATAROOT_EXPDATA=$CHEF_DATAROOT_EXPDATA
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
	For booleans, true=$TRUE and false=$FALSE.

	Options:
	  -h     Display this help.
	EOF
}

get_options()
{
	while getopts :h opt; do
		case "$opt" in
			h) help; exit 1 ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
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
