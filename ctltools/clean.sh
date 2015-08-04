#!/usr/bin/env sh

# This script is responsible for cleaning/removing builds. It is mostly
# necessary for removing directories that have been generated within a docker
# container, as ACLs are not properly supported by AUFS (the backend for sharing
# data between containers and optionally the host).¹
#
# The alternative would be to switch to "devicemapper" as device backend, but
# this would require the user to modify their own docker daemon's command line
# options, which is not ideal if we want to keep initial setup efforts to a
# minimum.
# ____
# ¹ The problematic part is actually only `mkdir -p`, whereas `mkdir` works.

# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# CLEAN ========================================================================

clean()
{
	BUILDPATH="$CHEFROOT_BUILD/$ARCH-$TARGET-$MODE"
	if [ ! -d "$BUILDPATH" ]; then
		die 2 '%s: Directory does not exist' "$BUILDPATH"
	fi
	track "Removing $BUILDPATH" rm -rf "$BUILDPATH" \
	|| return $FAILURE
}

# DOCKER =======================================================================

docker_clean()
{
	exec docker run --rm -it \
		-v "$CHEFROOT":"$DOCKER_CHEFROOT" \
		"$DOCKER_IMAGE" \
		"$DOCKER_INVOKEPATH" clean \
			$(test $VERBOSE -eq $TRUE && printf '%s' '-v') \
			"$RELEASE"
}

# MAIN =========================================================================

usage()
{
	echo "Usage: $INVOKENAME [OPTIONS ...] [[ARCH]:[TARGET]:[MODE]]"
}

help()
{
	usage
	cat <<- EOF

	Delete builds.

	Options:
	  -d       Dockerized (wrap execution inside docker container)
	  -h       Display this help
	  -v       Verbose output
	EOF
}

get_options()
{
	while getopts :dhv opt; do
		case "$opt" in
			d) DOCKERIZED=$TRUE ;;
			h) help; exit 1 ;;
			v) VERBOSE=$TRUE ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG" ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

get_release()
{
	if [ -z "$1" ]; then
		ARGSHIFT=0
	else
		ARGSHIFT=1
	fi
	parse_release "$1"
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_release "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "Trailing arguments: $@"

	if [ $DOCKERIZED -eq $TRUE ]; then
		docker_clean
	else
		LOGFILE="$CHEFROOT_BUILD/clean.log"
		if ! clean; then
			failure "Cleaning failed.\n"
			examine_logs
			exit 2
		fi
	fi
}

set -e
main "$@"
set +e
