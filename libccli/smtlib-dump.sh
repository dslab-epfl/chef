#!/usr/bin/env sh
#
# This script dumps queries from sqlite3 databases in SMT-Lib format.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

# UTILS ========================================================================

. "$(readlink -f "$(dirname "$0")")/utils.sh"

DOCKER_HOSTPATH_IN='/host-in'
DOCKER_HOSTPATH_OUT='/host-out'
DOCKER_HOSTPATH_BUILD='/host-build'

# DUMP =========================================================================

dump()
{
	case "$TARGET" in
		'release') BUILD_NAME='Release+Asserts' ;;
		'debug') BUILD_NAME='Debug+Asserts' ;;
		*) die_internal 'dump(): Invalid target: %s' "$TARGET" ;;
	esac

	TOOL_BIN="$BUILD_PATH/$BUILD/klee/$BUILD_NAME/bin/query-tool"

	if [ ! -e "$TOOL_BIN" ]; then
		die 2 '%s: Executable not found' "$TOOL_BIN"
	fi

	"$TOOL_BIN" \
		-end-solver="$SOLVER" \
		-dump-smtlib=$(test $MONOLITHIC -eq $TRUE && echo 'single' || echo 'separate') \
		-dump-smtlib-path="$DUMP_PATH/" \
		$(test $HUMAN -eq $TRUE && echo '-smtlib-human-readable') \
		$(test $COMPACT -eq $FALSE && echo '-smtlib-compact') \
		"$DB_FILE" \
		$IDS_EXPANDED
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
	exec docker run \
		--rm \
		-t \
		-i \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		-v "$DB_PATH":"$DOCKER_HOSTPATH_IN" \
		-v "$DUMP_PATH":"$DOCKER_HOSTPATH_OUT" \
		-v "$BUILD_PATH":"$DOCKER_HOSTPATH_BUILD" \
		"$DOCKER_IMAGE" \
		"$DOCKER_HOSTPATH"/"$RUNDIR"/"$RUNNAME" \
			-b "$DOCKER_HOSTPATH_BUILD" \
			$(test $COMPACT -eq $TRUE && printf "%s" '-c') \
			$(test $MONOLITHIC -eq $TRUE && printf "%s" '-M') \
			-o "$DOCKER_HOSTPATH_OUT" \
			-r "$RELEASE" \
			-s "$SOLVER" \
			$(test $HUMAN -eq $TRUE && printf "%s" '-w') \
			-z \
			"$DOCKER_HOSTPATH_IN/$DB_NAME" \
			"$IDS"
}

# DRY RUN ======================================================================

dryrun()
{
	cat <<- EOF
	ARCH=$ARCH
	MODE=$MODE
	RELEASE=$RELEASE
	SOLVER=$SOLVER
	DUMP_PATH=$DUMP_PATH
	DB_FILE=$DB_FILE
	MONOLITHIC=$(as_boolean $MONOLITHIC)
	HUMAN=$(as_boolean $HUMAN)
	EOF

	if [ $DIRECT -eq $FALSE ]; then
		cat <<- EOF
		DOCKER_HOSTPATH=$SRCPATH_ROOT:$DOCKER_HOSTPATH
		DOCKER_HOSTPATH_IN=$DB_PATH:$DOCKER_HOSTPATH_IN
		DOCKER_HOSTPATH_OUT=$DUMP_PATH:$DOCKER_HOSTPATH_OUT
		DOCKER_HOSTPATH_BUILD=$BUILD_PATH:$DOCKER_HOSTPATH_BUILD
		EOF
	fi
}

# UTILITIES ====================================================================

usage()
{
	echo "Usage: $INVOKENAME [OPTIONS ...] DB_FILE IDS"
}

help()
{
	usage
	cat <<- EOF

	Options:
	  -b BUILD_PATH  Path to the Chef build directory
	                 [default=$BUILD_PATH]
	  -c             Print compact SMTLIB (experimental!)
	  -h             Display this help
	  -M             Monolithic dump (no separate files)
	  -o DUMP_PATH   Dump queries from the DB file to DUMP_PATH
	                 [default=$DUMP_PATH]
	  -r RELEASE     Release tuple [default=$DEFAULT_RELEASE]
	  -s SOLVER      Use solver SOLVER [default=$SOLVER]
	  -w             Use whitespace to make it human-readable
	  -y             Dry run: print runtime variables and exit
	  -z             Direct mode (do not use docker)

	Solvers:
	  z3  cvc3

	IDs:
	  The query IDs can be given as a comma-separated list of numbers or ranges,
	  where a range is two numbers separated by a dash.
	EOF
}

get_options()
{
	# Default values:
	BUILD_PATH="$SRCPATH_ROOT/build"
	COMPACT=$FALSE
	DUMP_PATH="$PWD"
	MONOLITHIC=$FALSE
	RELEASE="$DEFAULT_RELEASE"
	SOLVER='stp'
	HUMAN=$FALSE
	DIRECT=$FALSE
	DRYRUN=$FALSE

	while getopts :b:chMo:r:s:wyz opt; do
		case "$opt" in
			b) BUILD_PATH="$OPTARG" ;;
			c) COMPACT=$TRUE ;;
			h) help; exit 1 ;;
			M) MONOLITHIC=$TRUE ;;
			o) DUMP_PATH="$OPTARG" ;;
			r) RELEASE="$OPTARG" ;;
			s) SOLVER="$OPTARG" ;;
			w) HUMAN=$TRUE ;;
			y) DRYRUN=$TRUE ;;
			z) DIRECT=$TRUE ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done

	split_release "$RELEASE"
	DUMP_PATH="$(readlink -f "$DUMP_PATH")"
	test -d "$DUMP_PATH" || die 2 '%s: Directory does not exist' "$DUMP_PATH"
	test -w "$DUMP_PATH" || die 3 '%s: Permission denied' "$DUMP_PATH"
	BUILD_PATH="$(readlink -f "$BUILD_PATH")"
	BUILD="$ARCH-$TARGET-$MODE"
	if [ ! -d "$BUILD_PATH/$BUILD" ]; then
		die 2 '%s: build does not exist in %s' "$BUILD" "$BUILD_PATH"
	fi
	ARGSHIFT=$(($OPTIND - 1))
}

get_db_path()
{
	DB_FILE="$1"
	if [ -z "$DB_FILE" ]; then
		die_help 'Missing database path'
	fi
	DB_FILE="$(readlink -f "$DB_FILE")"
	if [ ! -e "$DB_FILE" ]; then
		die 2 '%s: database does not exist' "$DB_FILE"
	fi
	DB_NAME="$(basename "$DB_FILE")"
	DB_PATH="$(dirname "$DB_FILE")"
	DB_DIR="$(basename "$DB_PATH")"
	ARGSHIFT=1
}

get_ids()
{
	IDS="$1"
	test -n "$IDS" || die_help 'Missing ID list'
	IDS_EXPANDED="$(list_expand "$IDS")"
	IDS_EXPANDED="$(for id in $IDS_EXPANDED; do range_expand "$id"; done \
	                | uniq | sort -n)"
	ID_COUNT=$(printf "$IDS_EXPANDED" | wc -l)
	test -n "$IDS_EXPANDED" || die_help 'Invalid ID format'
	ARGSHIFT=1
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_db_path "$@"
	shift $ARGSHIFT
	get_ids "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "Trailing arguments: $@"

	if [ $DRYRUN -eq $TRUE ]; then
		dryrun
		exit
	fi

	if [ $DIRECT -eq $TRUE ]; then
		dump
	else
		docker_dump
	fi
}

set -e
main "$@"
set +e
