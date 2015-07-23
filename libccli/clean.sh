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

# Maintainer: Tinu Weber <martin.weber@epfl.ch>

# UTILITIES ====================================================================

. "$(readlink -f "$(dirname "$0")")/utils.sh"

# CLEAN ========================================================================

clean()
{
	BUILDPATH="$BUILDPATH_ROOT/$ARCH-$TARGET-$MODE"
	if [ ! -d "$BUILDPATH" ]; then
		die 2 '%s: Directory does not exist' "$BUILDPATH"
	fi
	track "Removing $BUILDPATH" rm -r "$BUILDPATH" \
	|| return $FALSE
}

# DOCKER =======================================================================

docker_clean()
{
	exec docker run \
		--rm \
		-t \
		-i \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		"$DOCKER_IMAGE" \
		"$DOCKER_HOSTPATH/$RUNDIR/$RUNNAME" \
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

	Options:
	  -d       Dockerized (wrap execution inside docker container)
	  -h       Display this help
	EOF
}

get_options()
{
	DOCKERIZED=$DEFAULT_DOCKERIZED

	while getopts :dh opt; do
		case "$opt" in
			d) DOCKERIZED=$TRUE ;;
			h) help; exit 1 ;;
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
	split_release "$1"  # sets RELEASE, ARCH, TARGET and MODE
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
		LOGFILE='clean.log'
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
