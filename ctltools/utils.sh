# Shell script utilities shared by most ctl scripts.
# Compatible with set -e
#
# To be included as follows:
#
#   . "$(readlink -f "$(dirname "$0")")/utils.sh"
#
# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

# SHELL ========================================================================

# Don't mix these up!
TRUE=1
FALSE=0
SUCCESS=0
FAILURE=1
SKIPPED=42 # ... just to be sure

# Test if a variable is purely (decimal) numeric
is_numeric()
{
	case "$1" in
		''|*[!0-9]*) return $FAILURE ;;
		*) return $SUCCESS ;;
	esac
}

# Test if a variable is purely boolean (1 or 0):
is_boolean()
{
    [ -n "$1" ] || return $FAILURE
    is_numeric || return $FAILURE
    [ $1 -eq $TRUE ] || [ $1 -eq $FALSE ] || return $FAILURE
    return $SUCCESS
}

# Test if a command is available
is_command()
{
	if type "$1" >"$NULL" 2>&1; then
		return $SUCCESS
	else
		return $FAILURE
	fi
}

# Give human readable representation for boolean
as_boolean()
{
	if is_numeric "$1" && [ $1 -eq $TRUE ]; then
		echo 'true'
	else
		echo 'false'
	fi
}

# Check if a space-separated list contains a string:
list_contains()
{
	local haystack="$1"
	local needle="$2"
	for list_element in $haystack; do
		if [ "$needle" = "$list_element" ]; then
			return $SUCCESS
		fi
	done
	return $FAILURE
}

# Length of a string:
strlen()
{
	printf '%s' "$1" | wc -m
}

# Last character of a string:
strlast()
{
	printf '%s' "$1" | cut -c "$(strlen "$1")-"
}

# Make string usable for sed:
sedify()
{
	printf '%s' "$1" | sed -e 's/\^/\\^/g;s/\//\\\//g;s/\\/\\\\/g;s/\$/\\$/g'
}

# Make string usable as function name:
funcify()
{
	printf '%s' "$1" | sed -e 's/-/_/g'
}

# PATHS/NAMES ==================================================================

# Script location:
SCRIPTNAME="$(basename "$0")"
SCRIPTPATH="$(readlink -f "$0")"
SCRIPTDIR="$(dirname "$SCRIPTPATH")"

# Chef root:
CHEFROOT_SRC="$(dirname "$SCRIPTDIR")"
CHEFROOT="$(dirname "$CHEFROOT_SRC")"
CHEFROOT_BUILD="$CHEFROOT/build"
CHEFROOT_BUILD_DEPS="$CHEFROOT_BUILD/deps"
CHEFROOT_EXPDATA="$CHEFROOT/expdata"
CHEFROOT_VM="$CHEFROOT/expdata"

# Chef command line tools:
INVOKEPATH="$CHEFROOT_SRC/ctl"
test -n "$INVOKENAME" || INVOKENAME="$SCRIPTNAME"

# LLVM:
LLVM_BASE="$CHEFROOT_BUILD_DEPS/llvm"
LLVM_VERSION='3.2'
LLVM_SRC="$LLVM_BASE/llvm-${LLVM_VERSION}.src"
LLVM_BUILD_RELEASE="$LLVM_BASE/llvm-${LLVM_VERSION}.build-release"
LLVM_BUILD_DEBUG="$LLVM_BASE/llvm-${LLVM_VERSION}.build-debug"
LLVM_NATIVE="$LLVM_BASE/llvm-${LLVM_VERSION}-native"
LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"

# System:
NULL='/dev/null'

# Behaviour (default):
VERBOSE=${CHEF_VERBOSE:-$FALSE}
LOGFILE="$NULL"

# DEBUG ========================================================================

util_dryrun()
{
	cat <<- EOF
	SCRIPTNAME=$SCRIPTNAME
	SCRIPTPATH=$SCRIPTPATH
	SCRIPTDIR=$SCRIPTDIR
	INVOKENAME=$INVOKENAME
	CHEFROOT=$CHEFROOT
	CHEFROOT_SRC=$CHEFROOT_SRC
	CHEFROOT_BUILD=$CHEFROOT_BUILD
	CHEFROOT_EXPDATA=$CHEFROOT_EXPDATA
	CHEFROOT_VM=$CHEFROOT_VM
	LLVM_BASE=$LLVM_BASE
	LLVM_SRC=$LLVM_SRC
	LLVM_BUILD=$LLVM_BUILD
	LLVM_NATIVE=$LLVM_NATIVE
	LLVM_NATIVE_CC=$LLVM_NATIVE_CC
	LLVM_NATIVE_CXX=$LLVM_NATIVE_CXX
	VERBOSE=$VERBOSE
	ARCH=$ARCH
	TARGET=$TARGET
	MODE=$MODE
	RELEASE=$RELEASE
	RELEASEPATH=$RELEASEPATH
	DEFAULT_ARCH=$DEFAULT_ARCH
	DEFAULT_TARGET=$DEFAULT_TARGET
	DEFAULT_MODE=$DEFAULT_MODE
	DEFAULT_RELEASE=$DEFAULT_RELEASE
	ARCHS=$ARCHS
	TARGETS=$TARGETS
	MODES=$MODES
	LOGFILE=$LOGFILE
	NULL=$NULL
	EOF
}

# VALUES =======================================================================

KIBI=1024
MEBI=$(($KIBI * $KIBI))
GIBI=$(($MEBI * $KIBI))

case "$(uname)" in
	Darwin) CPU_CORES=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
	Linux) CPU_CORES=$(grep -c '^processor' /proc/cpuinfo) ;;
	*) CPU_CORES=1 ;;
esac

# MESSAGES =====================================================================

if [ -t 1 ] && [ -t 2 ]; then
    ESC_ERROR="\033[31m"
    ESC_SUCCESS="\033[32m"
    ESC_WARNING="\033[33m"
    ESC_MISC="\033[34m"
    ESC_SPECIAL="\033[35m"
    ESC_BOLD="\033[1m"
    ESC_RESET="\033[0m"
    ESC_SAVE="\033[s"
    ESC_RESTORE="\033[u"
else
    ESC_ERROR=''
    ESC_SUCCESS=''
    ESC_WARNING=''
    ESC_MISC=''
    ESC_SPECIAL=''
    ESC_BOLD=''
    ESC_RESET=''
    ESC_SAVE=''
    ESC_RESTORE="\n"
fi

FATAL="[${ESC_ERROR}FATAL${ESC_RESET}]"
FAIL="[${ESC_ERROR}FAIL${ESC_RESET}]"
WARN="[${ESC_WARNING}WARN${ESC_RESET}]"
_OK_="[${ESC_SUCCESS} OK ${ESC_RESET}]"
SKIP="[${ESC_SUCCESS}SKIP${ESC_RESET}]"
INFO="[${ESC_MISC}INFO${ESC_RESET}]"
ALRT="[${ESC_SPECIAL} !! ${ESC_RESET}]"
ABRT="[${ESC_ERROR}ABORT${ESC_RESET}]"
PEND="[ .. ]"
DEBUG="[${ESC_SPECIAL}DEBUG${ESC_RESET}]"

_print()
{
    _print_prefix="$1"
    _print_newline=$2
    _print_format="$3"
    test $_print_newline = $FALSE || _print_format="$_print_format\n"
    shift 3
    printf "${_print_prefix}$_print_format" "$@"
}

info()  { _print "$INFO " $TRUE "$@"; }
warn()  { _print "$WARN " $TRUE "$@" >&2; }
skip()  { _print "$SKIP " $TRUE "$@"; }
fail()  { _print "$FAIL " $TRUE "$@" >&2; }
pend()  { _print "$PEND " $TRUE "$@"; }
pend_() { _print "$PEND " $FALSE "$@"; }
alert() { _print "$ALRT " $TRUE "$@"; }
abort() { _print "\n$ABRT " $TRUE "$@" >&2; }
ok()    { _print "$_OK_ " $TRUE "$@"; }
debug() { _print "$DEBUG " $TRUE "$@"; }

_print_emphasised()
{
	_emphasised_colour="$1"
	_emphasised_format="$2"
	shift 2
	printf "${ESC_BOLD}${_emphasised_colour}>>>\033[0m $_emphasised_format" \
	       "$@"
}

success() { _print_emphasised "$ESC_SUCCESS" "$@"; }
failure() { _print_emphasised "$ESC_ERROR"   "$@"; }

track()
{
	track_msg="$1"
	shift

	if [ $VERBOSE -eq $TRUE ]; then
		track_status=$SUCCESS
		"$@" || track_status=$?
		test $track_status -ne $SKIPPED || track_status=$SUCCESS
		return $track_status
	else
		printf "$ESC_SAVE"; _print "$PEND " $FALSE '%s' "$track_msg"
		track_status=$SUCCESS
		{ "$@" || track_status=$?; } >>"$LOGFILE" 2>>"$LOGFILE"
		case $track_status in
			$SUCCESS) track_print=ok ;;
			$SKIPPED) track_print=skip ;;
			*) track_print=fail ;;
		esac
		printf "$ESC_RESTORE"; $track_print '%s' "$track_msg"

		if [ $track_print != fail ]; then
			return $SUCCESS
		else
			return $FAILURE
		fi
	fi
}

# USER INPUT ===================================================================

ask()
{
	ask_colour="$1"
	ask_default="$2"
	ask_format="$3"
	shift 3
	case "$ask_default" in
		[Yy]*) ask_sel='[Y/n]' ;;
		[Nn]*) ask_sel='[y/N]' ;;
		'') ask_sel='[y/n]' ;;
		*) die_internal "ask(): invalid default '%s'" "$ask_default" ;;
	esac
	while true; do
		_print_emphasised "$ask_colour" "$ask_format $ask_sel " "$@"
		read a || exit 255
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
	if [ $VERBOSE -eq $FALSE ]; then
		if ask "$ESC_ERROR" 'yes' 'Examine %s?' "$LOGFILE"; then
			less "$LOGFILE"
		else
			echo '"the error logs to ignore - to dark side the path is" -- Yoda'
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
	die_format="$1"
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

# LANGUAGE =====================================================================

lang_body()
{
	lang_body="$1"
	test "$(strlast "$1")" != "$2" || lang_body="${1%?}"
	printf '%s' "$lang_body"
}

lang_plural()
{
	lang_plural_last='s'
	test "$(strlast "$1")" != 'y' || lang_plural_last='ies'
	printf '%s%s' "$(lang_body "$1" 'y')" "$lang_plural_last"
}

lang_continuous()
{
	printf '%sing' "$(lang_body "$1" 'e')"
}

# S2E/CHEF =====================================================================

ARCHS='i386 x86_64 arm'
TARGETS='release debug'
MODES='normal asan libmemtracer'
DEFAULT_ARCH="${CHEF_ARCH:-"i386"}"
DEFAULT_TARGET="${CHEF_TARGET:-"release"}"
DEFAULT_MODE="${CHEF_MODE:-"normal"}"
DEFAULT_RELEASE="${CHEF_RELEASE:-"$DEFAULT_ARCH:$DEFAULT_TARGET:$DEFAULT_MODE"}"

parse_release()
{
	IFS=: read ARCH TARGET MODE <<- EOF
	$(echo "$1:")
	EOF

	if ! list_contains "$ARCHS" "${ARCH:="$DEFAULT_ARCH"}"; then
		die_help 'Unknown architecture: %s' "$ARCH"
	fi
	if ! list_contains "$TARGETS" "${TARGET:="$DEFAULT_TARGET"}"; then
		die_help 'Unknown target: %s' "$TARGET"
	fi
	if ! list_contains "$MODES" "${MODE:="$DEFAULT_MODE"}"; then
		die_help 'Unknown mode: %s' "$MODE"
	fi
	RELEASE="$ARCH:$TARGET:$MODE"
	RELEASEPATH="$CHEFROOT_BUILD/$ARCH-$TARGET-$MODE"
	case "$TARGET" in
		release)
			ASSERTS='Release+Asserts'
			LLVM_BUILD="$LLVM_BUILD_RELEASE" ;;
		debug)
			ASSERTS='Debug+Asserts'
			LLVM_BUILD="$LLVM_BUILD_DEBUG" ;;
		*) die_internal 'parse_release(): unknown target %s' "$TARGET" ;;
	esac
}
