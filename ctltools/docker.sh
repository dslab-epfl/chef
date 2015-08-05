#!/usr/bin/env sh
#
# This script runs the right docker container, with the necessary paths set up
# to interact with the chef source. It is especially handy for debugging things
# inside the container without having to type the full docker command line.
#
# Maintainer:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)


. "$(readlink -f "$(dirname "$0")")/utils.sh"

# RUN ==========================================================================

docker_run()
{
	test $# -eq 0 || die_help "Trailing arguments: $@"
	exec docker run --rm -it \
		-v "$CHEFROOT":"$DOCKER_CHEFROOT" \
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

	# Detect image:
	docker_imagename="$1"
	case "$docker_imagename" in
		base) docker_image="$DOCKER_IMAGE_BASE" ;;
		chef) docker_image="$DOCKER_IMAGE" ;;
		'-h') help; exit 1 ;;
		*) die_help 'Unknown image name: %s' "$docker_imagename" ;;
	esac
	docker_path="$CHEFROOT_SRC/docker/image/$docker_imagename"
	docker_share="$docker_path/src"

	# Set up share:
	rm -rf "$docker_share"
	mkdir "$docker_share"
	cp -r "$CHEFROOT_SRC/setup.sh" "$docker_share/"
	cp -r "$CHEFROOT_SRC/ctl"      "$docker_share/"
	cp -r "$CHEFROOT_SRC/ctltools" "$docker_share/"
	cp -r "$CHEFROOT_SRC/llvm"     "$docker_share/"
	# Because https://github.com/docker/docker/issues/1676

	exec docker build --rm -t="$docker_image" "$docker_path"
}

# MAIN =========================================================================

usage() { echo "Usage: $INVOKENAME [OPTIONS ...] ACTION"; }
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
