# Shell script utilities shared by most ccli scripts.
# Compatible with set -e
#
# To be included as follows:
#
#     . "$(readlink -f "$(dirname "$0")")/utils.sh"
#
# XXX $VERBOSE and $LOGFILE *must* be set for track() and examine_logs()
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

# PATHS/NAMES ==================================================================

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
RUNDIR="$(basename "$RUNPATH")"
SRCPATH_ROOT="$(dirname "$RUNPATH")"
SRCDIR_ROOT="$(basename "$SCRPATH_ROOT")"
BUILDPATH_ROOT="$SRCPATH_ROOT/build"

if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

NULL='/dev/null'

# MESSAGES =====================================================================

COLOUR_ERROR=31
COLOUR_SUCCESS=32
COLOUR_WARNING=33
COLOUR_MISC=34

FATAL_="[\033[31mFATAL\033[0m]"
FAIL_="[\033[${COLOUR_ERROR}mFAIL\033[0m]"
WARN_="[\033[${COLOUR_WARNING}mWARN\033[0m]"
_OK__="[\033[${COLOUR_SUCCESS}m OK \033[0m]"
SKIP_="[\033[${COLOUR_MISC}mSKIP\033[0m]"
INFO_="[\033[${COLOUR_MISC}mINFO\033[0m]"

note()
{
	note_format="$1"
	shift
	printf "       $note_format\n" "$@"
}

info()
{
	info_format="$1"
	shift
	printf "$INFO_ $info_format\n" "$@"
}

warn()
{
	warn_format="$1"
	shift
	printf "$WARN_ $warn_format\n" "$@" >&2
}

skip()
{
	skip_format="$1"
	shift
	printf "$SKIP_ $skip_format\n" "$@"
}

emphasised()
{
	emphasised_colour=$1
	emphasised_format="$2"
	shift 2
	printf "\033[1;${emphasised_colour}m>>>\033[0m $emphasised_format" "$@"
}

success()
{
	success_format="$1"
	shift
	emphasised $COLOUR_SUCCESS "$success_format" "$@"
}

fail()
{
	fail_format="$1"
	shift
	emphasised $COLOUR_ERROR "$fail_format" "$@"
}

track()
{
	util_check

	track_msg="$1"
	shift

	if [ $VERBOSE -eq $TRUE ]; then
		if "$@"; then
			return $TRUE
		else
			return $FALSE
		fi
	else
		printf "\033[s[ .. ] %s" "$track_msg"
		track_status="$_OK__"
		{ "$@" || track_status="$FAIL_"; } >>"$LOGFILE" 2>>"$LOGFILE"
		printf "\033[u$track_status %s\n" "$track_msg"
		if [ "$track_status" = "$_OK__" ]; then
			return $TRUE
		else
			return $FALSE
		fi
	fi
}

# USER INPUT ===================================================================

ask()
{
	ask_colour=$1
	ask_default="$2"
	ask_format="$3"
	shift 3
	case "$ask_default" in
		[Yy]*) ask_sel='[Y/n]';;
		[Nn]*) ask_sel='[y/N]';;
		*) die_internal "ask(): invalid default '%s'" "$ask_default" ;;
	esac
	while true; do
		emphasised $ask_colour "$ask_format $ask_sel " "$@"
		read a
		test -n "$a" || a="$ask_default"
		case "$a" in
			[Yy]*) return 0;;
			[Nn]*) return 1;;
			*) ;;
		esac
	done
}

examine_logs()
{
	util_check
	if [ $VERBOSE -eq $FALSE ]; then
		if ask $COLOUR_ERROR 'yes' 'Examine %s?' "$LOGFILE"; then
			less "$LOGFILE"
		else
			note '"ignoring the error logs - the path to dark side is" -- Yoda'
		fi
	fi
}

# EXIT =========================================================================

# General die
die()
{
	die_retval=$1
	die_format="$2"
	shift 2
	printf "$die_format\n" "$@" >&2
	exit $die_retval
}

# Wrong command line
die_help()
{
	die_help_format="$1"
	if [ -n "$die_help_format" ]; then
		shift
		printf "$die_help_format\n" "$@" >&2
	fi
	if is_command usage; then
		usage >&2
	fi
	die 1 'Run `%s -h` for help.' "$INVOKENAME"
}

# Internal reasons to crash
die_internal()
{
	die_internal_format="$1"
	shift
	die 127 "$FATAL_ Internal Error: $die_internal_format" "$@"
}

# SHELL ========================================================================

# Yes, fuck logic
TRUE=0
FALSE=1

# Test if a variable is purely (decimal) numeric
is_numeric()
{
	case "$1" in
		''|*[!0-9]*) return $FALSE ;;
		*) return $TRUE ;;
	esac
}

# Test if a command is available
is_command()
{
	if type "$1" >"$NULL" 2>&1; then
		return $TRUE
	else
		return $FALSE
	fi
}

# Give human readable representation for boolean
as_boolean()
{
	test -n "$1" || die_internal "as_boolean(): empty value"
	is_numeric "$1" || die_internal "as_boolean(): non-numeric value"

	if [ $1 -eq $TRUE ]; then
		echo 'true'
	else
		echo 'false'
	fi
}

# DEBUG ========================================================================

util_dryrun()
{
	cat <<- EOF
	RUNNAME=$RUNNAME
	RUNPATH=$RUNPATH
	RUNDIR=$RUNDIR
	SRCPATH_ROOT=$SRCPATH_ROOT
	SRCDIR_ROOT=$SRCDIR_ROOT
	BUILDPATH_ROOT=$BUILDPATH_ROOT
	INVOKENAME=$INVOKENAME
	RELEASE=$RELEASE
	ARCH=$ARCH
	TARGET=$TARGET
	MODE=$MODE
	VERBOSE=$(as_boolean $VERBOSE)
	LOGFILE=$LOGFILE
	FATAL_=$FATAL_
	FAIL_=$FAIL_
	WARN_=$WARN_
	_OK__=$_OK__
	SKIP_=$SKIP_
	DOCKER_IMAGE=$DOCKER_IMAGE
	DOCKER_HOSTPATH=$DOCKER_HOSTPATH
	EOF
}

util_check()
{
	test -n "$VERBOSE" || die_internal 'VERBOSE not set'
	test -n "$LOGFILE" || die_internal 'LOGFILE not set'
}

# DOCKER =======================================================================

DOCKER_IMAGE='dslab/s2e-chef:v0.6'
DOCKER_HOSTPATH='/host'

docker_image_exists()
{
	if docker inspect "$1" >"$NULL" 2>&1; then
		return $TRUE
	else
		return $FALSE
	fi
}

# S2E/CHEF =====================================================================

ARCHS='i386 x86_64'
TARGETS='release debug'
MODES='normal asan libmemtracer'
DEFAULT_ARCH="${CCLI_ARCH:-"i386"}"
DEFAULT_TARGET="${CCLI_TARGET:-"release"}"
DEFAULT_MODE="${CCLI_MODE:-"normal"}"
DEFAULT_RELEASE="${CCLI_RELEASE:-"$DEFAULT_ARCH:$DEFAULT_TARGET:$DEFAULT_MODE"}"
DEFAULT_DATAROOT="${CHEF_DATAROOT:-"/var/lib/chef"}"

split_release()
{
	IFS_="$IFS"
	IFS=':'
	from_release_slot='arch'
	for t in $1; do
		case "$from_release_slot" in
		arch)
			case "$t" in
				''|i386|x86_64) ARCH="$t" ;;
				*) die_help 'Invalid architecture: %s' "$t" ;;
			esac
			from_release_slot='target'
			;;
		target)
			case "$t" in
				''|release|debug) TARGET="$t" ;;
				*) die_help 'Invalid target: %s' "$t" ;;
			esac
			from_release_slot='mode'
			;;
		mode)
			case "$t" in
				''|normal|asan|libmemtracer) MODE="$t" ;;
				*) die_help 'Invalid mode: %s' "$t" ;;
			esac
			;;
		esac
	done
	IFS="$IFS_"

	ARCH="${ARCH:-"$DEFAULT_ARCH"}"
	TARGET="${TARGET:-"$DEFAULT_TARGET"}"
	MODE="${MODE:-"$DEFAULT_MODE"}"
}
