# Shell script utilities shared by most ccli scripts
# Include with:
#
#     . "$(readlink -f "$(dirname "$0")")/utils.sh"
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

# VARIABLES ====================================================================

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
RUNDIR="$(basename "$RUNPATH")"
SRCPATH_ROOT="$(dirname "$RUNPATH")"
SRCDIR_ROOT="$(basename "$SCRPATH_ROOT")"

if [ -z "$INVOKENAME" ]; then
	INVOKENAME="$RUNNAME"
fi

DOCKER_IMAGE='dslab/s2e-chef:v0.6'
DOCKER_HOSTPATH='/host'

# MESSAGES =====================================================================

FATAL_="[\033[31mFATAL\033[0m]"
FAIL_="[\033[31mFAIL\033[0m]"
WARN_="[\033[33mWARN\033[0m]"
_OK__="[\033[32m OK \033[0m]"
SKIP_="[\033[34mSKIP\033[0m]"

note()
{
	note_format="$1"
	shift
	printf "       $note_format\n" "$@"
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
	important_colour=$1
	important_format="$2"
	shift 2
	printf "\033[1;${important_colour}m>>>\033[0m $important_format" "$@"
}

success()
{
	success_format="$1"
	shift
	emphasised 32 "$success_format" "$@"
}

fail()
{
	fail_format="$1"
	shift
	emphasised 31 "$fail_format" "$@"
}

track()
{
	track_msg="$1"
	shift

	if [ $VERBOSE -eq $TRUE ]; then
		if "$@"; then
			return $TRUE
		else
			return $FALSE
		fi
	else
		if [ -z "$LOGFILE" ]; then
			die_internal "track(): no log file specified"
		fi
		printf "\033[s[ .. ] %s" "$track_msg"
		track_status=$_OK__
		{ "$@" || track_status=$FAIL_; } >>"$LOGFILE" 2>>"$LOGFILE"
		printf "\033[u%s %s\n" "$track_status" "$track_msg"
		test $track_status -eq $_OK__ && return $TRUE || return $FALSE
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
	if is_function usage; then
		usage >&2
	fi
	die 1 'Run `%s -h` for help.' "$INVOKENAME"
}

# Internal reasons to crash
die_internal()
{
	die_internal_format="$1"
	die 127 "internal error: $die_internal_format\n" "$@"
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

# Test if a function is defined
is_function()
{
	if type "$1" >/dev/null 2>&1; then
		return $TRUE
	else
		return $FALSE
	fi
}

# Give human readable representation for boolean
as_boolean()
{
	if [ $1 -eq $TRUE ]; then
		echo 'true'
	else
		echo 'false'
	fi
}

# COMMANDS =====================================================================

# Search for coreutil's `which`
IFS_="$IFS"
IFS=:
WHICH='which'
for p in "$PATH"; do
	if [ -x "$p/which" ]; then
		WHICH="$p/which"
		break
	fi
done
IFS="$IFS_"

# DEBUG ========================================================================

util_dryrun()
{
	cat <<- EOF
	RUNNAME=$RUNNAME
	RUNPATH=$RUNPATH
	RUNDIR=$RUNDIR
	SRCPATH_ROOT=$SRCPATH_ROOT
	SRCDIR_ROOT=$SRCDIR_ROOT
	INVOKENAME=$INVOKENAME
	DOCKER_IMAGE=$DOCKER_IMAGE
	DOCKER_HOSTPATH=$DOCKER_HOSTPATH
	FATAL_=$FATAL_
	FAIL_=$FAIL_
	WARN_=$WARN_
	_OK__=$_OK__
	SKIP_=$SKIP_
	EOF
}
