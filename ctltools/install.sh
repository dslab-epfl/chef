#!/usr/bin/env sh

# This script installs Z3 and LLVM on the system.
#
# Maintainers:
#   Tinu Weber <martin.weber@epfl.ch>

. "$(readlink -f "$(dirname "$0")")/utils.sh"

COMPS='z3'

# Z3 ===========================================================================

z3_install()
{
	# Fix install location:
	printf "%%s/^PREFIX=.*\$/PREFIX=$(sedify "$INSTALLDIR")/g\nwq\n" \
	| ex -s config.mk \
	|| return $FAILURE

	# Install:
	make -C build install || return $FAILURE
}

# PROTOBUF =====================================================================

protobuf_install()
{
	# Install location should be fine
	make install || return $FALSE
	ldconfig || return $FALSE
}

# ALL ==========================================================================

all_install()
{
	for component in $COMPONENTS; do
		BUILDPATH="$DATAROOT_BUILD/llvm/$component"
		LOGFILE="${BUILDPATH}_install.log"
		cd "$BUILDPATH"

		handler=${component}_install
		if is_command "$handler"; then
			if ! track "installing $component" $handler; then
				examine_logs
				return $FAILURE
			fi
		else
			fail 'component %s not found, skipping' "$component"
			return $FAILURE
		fi
	done
	success 'installation to %s successful!' "$INSTALLDIR"
}

# MAIN =========================================================================

usage()
{
	echo "Usage: $INVOKENAME [OPTIONS ...] COMPONENTS ..."
}

help()
{
	usage
	cat <<- EOF

	$INVOKENAME: Install S²E-Chef on the system

	Options:
	  -t PATH  Location to which the components are installed
	           [default=$INSTALLDIR]
	  -h       Display help message and exit
	  -v       Verbose output
	EOF
}

get_options()
{
	INSTALLDIR='/usr/local'

	while getopts :ht:v opt; do
		case "$opt" in
			h) help; exit 1 ;;
			t) INSTALLDIR="$OPTARG" ;;
			v) VERBOSE=$TRUE ;;
			'?') die_help 'Unknown option: -%s' "$OPTARG" ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))

	test -d "$INSTALLDIR" || die 1 '%s: directory not found' "$INSTALLDIR"
	test -w "$INSTALLDIR" || die 1 '%s: write permission denied' "$INSTALLDIR"
}

get_components()
{
	COMPONENTS="$@"
}

main()
{
	get_options "$@"
	shift $ARGSHIFT
	get_components "$@"

	all_install || exit $FAILURE
}

set -e
main "$@"
set +e
