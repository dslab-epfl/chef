#!/usr/bin/env sh
#
# This script simplifies the docker usage.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

RUNNAME="$(basename "$0")"
RUNPATH="$(dirname "$(readlink -f "$0")")"
CHEFROOT="$(dirname "$RUNPATH")"
DOCKER_IMAGE='dslab/s2e-chef'
DOCKER_VERSION='v0.6'
DOCKER_HOSTPATH='/host'
if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

# RUN ==========================================================================

run()
{
	exec docker run \
		--rm \
		-t \
		-i \
		-v "$CHEFROOT":"$DOCKER_HOSTPATH" \
		"$DOCKER_IMAGE":"$DOCKER_VERSION" \
		/bin/bash
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

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] COMMAND
	EOF
}

help()
{
	usage
	cat <<- EOF

	Commands:
	  run     Run docker container

	Options:
	  -h      Display this help
	EOF
}

die_help()
{
	die_help_format="$1"
	if [ -n "$die_help_format" ]; then
		shift
		printf "$die_help_format\n" "$@" >&2
	fi
	usage >&2
	die 1 'Run with `-h` for help.'
}

internal_error()
{
	internal_error_format="$1"
	shift
	printf "Internal error: $internal_error_format\n" "$@"
	exit 3
}

# MAIN =========================================================================

read_options()
{
	while getopts h opt; do
		case "$opt" in
			'h') help; exit 1 ;;
			'?') die_help ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

read_subcommand()
{
	SUBCOMMAND="$1"
	case "$SUBCOMMAND" in
		run) ;;
		'') die_help 'Missing subcommand' ;;
		*) die_help 'Unknown subcommand: %s' "$SUBCOMMAND" ;;
	esac
	ARGSHIFT=1
}

main()
{
	read_options "$@"
	shift $ARGSHIFT
	read_subcommand "$@"
	shift $ARGSHIFT

	case "$SUBCOMMAND" in
		run) run ;;
		*) internal_error 'main(): Unknown subcommand: %s' "$SUBCOMMAND" ;;
	esac
}

set -e
main "$@"
set +e
