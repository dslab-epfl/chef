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
SRCDIR_ROOT="$(basename "$SRCPATH_ROOT")"
BUILDPATH_ROOT="$SRCPATH_ROOT/build"

if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

NULL='/dev/null'

# VALUES =======================================================================

KIBI=1024
MEBI=$(($KIBI * $KIBI))
GIBI=$(($MEBI * $KIBI))

# MESSAGES =====================================================================

if [ -t 1 ] && [ -t 2 ]; then
    COLOUR_ERROR="\033[31m"
    COLOUR_SUCCESS="\033[32m"
    COLOUR_WARNING="\033[33m"
    COLOUR_MISC="\033[34m"
    COLOUR_SPECIAL="\033[35m"
    ESC_BOLD="\033[1m"
    ESC_RESET="\033[0m"
    ESC_SAVE="\033[s"
    ESC_RESTORE="\033[u"
else
    COLOUR_ERROR=''
    COLOUR_SUCCESS=''
    COLOUR_WARNING=''
    COLOUR_MISC=''
    COLOUR_SPECIAL=''
    ESC_BOLD=''
    ESC_RESET=''
    ESC_SAVE=''
    ESC_RESTORE="\n"
fi

FATAL="[${COLOUR_ERROR}FATAL${COLOUR_RESET}]"
FAIL="[${COLOUR_ERROR}FAIL${COLOUR_RESET}]"
WARN="[${COLOUR_WARNING}WARN${COLOUR_RESET}]"
_OK_="[${COLOUR_SUCCESS} OK ${COLOUR_RESET}]"
SKIP="[${COLOUR_SUCCESS}SKIP${COLOUR_RESET}]"
INFO="[${COLOUR_MISC}INFO${COLOUR_RESET}]"
ALRT="[${COLOUR_SPECIAL} !! ${COLOUR_RESET}]"
PEND="[ .. ]"

_print()
{
    _print_prefix="$1"
    _print_newline=$2
    _print_format="$3"
    [ $_print_newline != $TRUE ] || _print_format="$_print_format\n"
    shift 3
    printf "${_print_prefix}$_print_format" "$@"
}

note()  { _print ''       $TRUE "$@"; }
info()  { _print "$INFO " $TRUE "$@"; }
warn()  { _print "$WARN " $TRUE "$@" >&2; }
skip()  { _print "$SKIP " $TRUE "$@"; }
fail()  { _print "$FAIL " $TRUE "$@" >&2; }
pend()  { _print "$PEND " $TRUE "$@"; }
alert() { _print "$ALRT " $TRUE "$@"; }
ok()    { _print "$_OK_ " $TRUE "$@"; }

_print_emphasised()
{
	_emphasised_colour=$1
	_emphasised_format="$2"
	shift 2
	printf "${COLOUR_BOLD}${_emphasised_colour}>>>\033[0m $_emphasised_format" \
	       "$@"
}

success() { _print_emphasised "$COLOUR_SUCCESS" "$@"; }
failure() { _print_emphasised "$COLOUR_ERROR"   "$@"; }

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
		printf "$ESC_SAVE"; _print "$PEND " $FALSE '%s' "$track_msg"
		track_print=ok
		{ "$@" || track_print=fail; } >>"$LOGFILE" 2>>"$LOGFILE"
		printf "$ESC_RESTORE"; $track_print '%s' "$track_msg"

		if [ $track_print = ok ]; then
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
		_print_emphasised $ask_colour "$ask_format $ask_sel " "$@"
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
		if ask "$COLOUR_ERROR" 'yes' 'Examine %s?' "$LOGFILE"; then
			less "$LOGFILE"
		else
			note '"ignoring the error logs - the path to dark side is" -- Yoda'
		fi
	fi
}

# EXPANSIONS ===================================================================

list_expand()
{
	h="$(echo "$1" | cut -d ',' -f 1)"       # head
	t="$(echo "$1" | cut -d ',' -f 2-)"      # tail
	echo "$h"
	if [ -n "$t" ] && [ "$t" != "$1" ]; then # ugly
		list_expand "$t"
	fi
}

range_expand()
{
	range="$1"
	test -n "$range" || return

	begin="$(echo "$range" | cut -d '-' -f 1)"
	if [ -z "$begin" ]; then
		warn 'Missing begin in range'
		return
	elif ! is_numeric "$begin"; then
		warn 'Range begin must be numeric (`%s` found)' "$begin" >&2
		return
	fi

	end="$(echo "$range" | cut -d '-' -f 2-)"
	if [ -z "$end" ]; then
		warn 'Missing end in range'
		return
	elif ! is_numeric "$end"; then
		warn 'Range end must be numeric (`%s` found)' "$end" >&2
		return
	fi

	for i in $(seq $begin $end); do
		echo $i
	done
}

# EXIT =========================================================================

# General die
die()
{
	die_retval=$1
	shift
	die_format="$2"
	if [ -n "$die_format" ]; then
        shift
        printf "$die_format\n" "$@" >&2
    fi
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
	die 2 'Run `%s -h` for help.' "$INVOKENAME"
}

# Internal reasons to crash
die_internal()
{
	die_internal_format="$1"
	shift
	fatal "Internal error: $die_internal_format" "$@"
	die 127
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

is_boolean()
{
    [ -n "$1" ] || return $FALSE
    is_numeric || return $FALSE
    [ $1 -eq $TRUE ] || [ $1 -eq $FALSE ] || return $FALSE
    return $TRUE
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
	if is_boolean "$1" && [ $1 -eq $TRUE ]; then
		echo 'true'
	else
		echo 'false'
	fi
}

# DEBUG ========================================================================

util_dryrun()
{
	util_check
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
	DOCKERIZED=$(as_boolean $DOCKERIZED)
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
	VERBOSE=${VERBOSE:-$DEFAULT_VERBOSE}
	LOGFILE="${LOGFILE:-"$DEFAULT_LOGFILE"}"
	DOCKERIZED=${DOCKERIZED:-$DEFAULT_DOCKERIZED}
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
DEFAULT_VERBOSE=$FALSE
DEFAULT_LOGFILE="$NULL"
DEFAULT_DOCKERIZED=${CCLI_DOCKERIZED:-$FALSE}

split_release()
{
	IFS=: read ARCH TARGET MODE <<- EOF
	$(echo "$1:")
	EOF

	if case "${ARCH:="$DEFAULT_ARCH"}" in (i386|x86_64) false;; esac; then
		die_help 'Unknown architecture: %s' "$ARCH"
	fi
	if case "${TARGET:="$DEFAULT_TARGET"}" in (release|debug) false;; esac; then
		die_help 'Unknown target: %s' "$TARGET"
	fi
	if case "${MODE:="$DEFAULT_MODE"}" in (normal|asan|libmemtracer) false;; esac; then
		die_help 'Unknown mode: %s' "$MODE"
	fi
	RELEASE="$ARCH:$TARGET:$MODE"
}
