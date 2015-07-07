#!/usr/bin/env sh

# This script is responsible for cleaning/removing builds. It is mostly
# necessary for removing directories that have been generated within a docker
# container through `mkdir -p`, which does not seem to respect ACLs if run on a
# shared directory from within docker.

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
			-z \
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
	  -h       Display this help
	  -z       Run directly, without docker
	EOF
}

get_options()
{
	DIRECT=$DEFAULT_DIRECT

	while getopts :hz opt; do
		case "$opt" in
			h) help; exit 1 ;;
			z) DIRECT=$TRUE ;;
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

	if [ $DIRECT -eq $TRUE ]; then
		LOGFILE='clean.log'
		if ! clean; then
			fail "Cleaning failed.\n"
			examine_logs
		fi
	else
		docker_clean
	fi
}

set -e
main "$@"
set +e
