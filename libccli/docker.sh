#!/usr/bin/env sh
#
# This script runs the right docker container, with the necessary paths set up
# to interact with the chef source. It is especially handy for debugging things
# inside the container without having to type the full docker command line.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

# UTILS ========================================================================

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# RUN ==========================================================================

run()
{
	exec docker run \
		--rm \
		-t \
		-i \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		"$DOCKER_IMAGE" \
		/bin/bash
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] ACTION
	EOF
}

help()
{
	usage
	cat <<- EOF

	Options:
	  -h      Display this help

	Actions:
	  run     Run docker container
	EOF
}

read_options()
{
	while getopts :h opt; do
		case "$opt" in
			h) help; exit 1 ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
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
		*) die_internal 'main(): Unknown subcommand: %s' "$SUBCOMMAND" ;;
	esac
}

set -e
main "$@"
set +e
