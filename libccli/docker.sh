#!/usr/bin/env sh
#
# This script runs the right docker container, with the necessary paths set up
# to interact with the chef source. It is especially handy for debugging things
# inside the container without having to type the full docker command line.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>


. "$(readlink -f "$(dirname "$0")")/utils.sh"

# RUN ==========================================================================

docker_run()
{
	test $# -eq 0 || die_help "Trailing arguments: $@"
	exec docker run --rm -it \
		-v "$WSROOT":"$DOCKER_WSROOT" \
		"$DOCKER_IMAGE" \
		/bin/bash
}

# FETCH ========================================================================

docker_pull()
{
	test $# -eq 0 || die_help "Trailing arguments: $@"
	exec docker pull "$DOCKER_IMAGE"
}

# BUILD ========================================================================

docker_build()
{
	test $# -ge 1 || die_help 'Missing image'
	test $# -le 1 || die_help "Trailing arguments: $@"
	docker_image="$1"
	docker_path="$SRCROOT/docker/image/$docker_image"
	case "$docker_image" in
		base)
			mkdir -p "$docker_path/chef"
			cp -r "$SRCROOT/$RUNDIR" "$docker_path/chef/"
			cp -r "$SRCROOT/llvm"    "$docker_path/chef/"
			docker_version='0.4'
			;;
		chef)
			# cp not necessary, it's based on dslab/s2e-base:v0.4
			docker_version='0.7'
			;;
		'-h') help; exit 1 ;;
		*) die_help 'Unknown image: %s' "$docker_image" ;;
	esac

	# Because https://github.com/docker/docker/issues/1676
	docker build --rm -t="dslab/s2e-$docker_image:v$docker_version" \
		"$docker_path"
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
	  run               Run docker container
	  pull              Pull the prepared docker image from hub.docker.com
	  build {base,chef} Build S²E or Chef docker image
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
		build|pull|run) ;;
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

	docker_$SUBCOMMAND "$@"
}

set -e
main "$@"
set +e
