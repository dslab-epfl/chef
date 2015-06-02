#!/usr/bin/env sh
#
# This script dumps queries from sqlite3 databases in SMT-Lib format.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

DOCKER_VERSION='v0.6'
DOCKER_HOSTPATH='/host'
DOCKER_HOSTPATH_IN='/host-in'
DOCKER_HOSTPATH_OUT='/host-out'
RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
RUNDIR="$(basename "$RUNPATH")"
SRCPATH_ROOT="$(dirname "$RUNPATH")"
if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

# DUMP =========================================================================

dump()
{
	ls "$DUMP_PATH"
}

# DUMPALL ======================================================================

dumpall()
{
	DB_ROOT="$DB_PATH"
	DUMP_ROOT="$DUMP_PATH"

	die 3 'not implemented'
}

# DOCKER =======================================================================

docker_dump()
{
	docker run \
		--rm \
		-t \
		-i \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		-v "$DB_PATH":"$DOCKER_HOSTPATH_IN" \
		-v "$DUMP_PATH":"$DOCKER_HOSTPATH_OUT" \
		dslab/s2e-chef:"$DOCKER_VERSION" \
		"$DOCKER_HOSTPATH"/"$RUNDIR"/"$RUNNAME" \
			-o "$DOCKER_HOSTPATH_OUT" \
			-s "$SOLVER" \
			-z \
			"$DOCKER_HOSTPATH_IN/$DB_NAME"
}

# UTILITIES ====================================================================

# ACL + docker + mkdir -p = issues
mkdirp()
{
	test -e "$1" || mkdir "$1"
}

die()
{
	die_retval=$1
	die_format="$2"
	shift 2
	printf "$die_format\n" "$@" >&2
	exit $die_retval
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

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS] DB_FILE
	EOF
}

help()
{
	usage

	cat <<- EOF

	Options:
	  -h            Display this help
	  -o DUMP_PATH  Dump queries from the DB file to DUMP_PATH [default=$DUMP_PATH]
	  -s SOLVER     Use solver SOLVER [default=$SOLVER]
	  -z            Direct mode (do not use docker).
	EOF

	#If DB_PATH denotes a directory, all database files below are searched and dumped.
	#In that case, DUMP_PATH is considered a tree that copies DB_PATH's structure.
	#EOF
}

# MAIN =========================================================================

get_options()
{
	# Default values:
	DUMP_PATH="$PWD"
	SOLVER='stp'
	DIRECT=0

	while getopts ho:s:z opt; do
		case "$opt" in
			h) help; exit 1 ;;
			o) DUMP_PATH="$OPTARG" ;;
			s) SOLVER="$OPTARG" ;;
			z) DIRECT=1 ;;
			'?') die_help ;;
		esac
	done

	DUMP_PATH="$(readlink -f "$DUMP_PATH")"
	ARGSHIFT=$(($OPTIND - 1))
}

get_db_path()
{
	DB_FILE="$1"
	if [ -z "$DB_FILE" ]; then
		die_help 'Missing database path'
	fi
	DB_FILE="$(readlink -f "$DB_FILE")"
	DB_NAME="$(basename "$DB_FILE")"
	DB_PATH="$(dirname "$DB_FILE")"
	DB_DIR="$(basename "$DB_PATH")"
	ARGSHIFT=1
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_db_path "$@"
	shift $ARGSHIFT

	if [ $DIRECT -eq 1 ]; then
		if [ -d "$DBPATH" ]; then
			dumpall
		else
			dump
		fi
	else
		docker_dump
	fi
}

set -e
main "$@"
set +e
