#!/bin/sh

set -e
. "$(readlink -f "$(dirname "$0")")/utils.sh"

usage() { echo "Usage: $INVOKENAME [OPTIONS ...] COMPONENTS ..."; }
help()
{
	usage
	cat <<- EOF

	$INVOKENAME: Install S²E-Chef on the system

	Options:
	  -t PATH  Location to which the components are installed
	           [default=$INSTALLDIR]
	  -h       Display help message and exit
	EOF
}

# Get options:
INSTALLDIR='/usr/local'
while getopts :ht: opt; do
	case "$opt" in
		h) help; exit 1 ;;
		t) INSTALLDIR="$OPTARG" ;;
		'?') die_help 'Unknown option: -%s' "$OPTARG" ;;
	esac
done
shift $(($OPTIND - 1))

# Check:
test -d "$INSTALLDIR" || die 1 '%s: directory not found' "$INSTALLDIR"
test -w "$INSTALLDIR" || die 1 '%s: write permissions denied' "$INSTALLDIR"

# Z3:
if [ "$1" = 'z3' ] || [ "$2" = 'z3' ]; then
	cd "$CHEFROOT_BUILD_DEPS/z3"
	printf "%%s/^PREFIX=.*\$/PREFIX=$(sedify "$INSTALLDIR")/g\nwq\n" | ex -s config.mk
	make -C build install
fi

# protobuf:
if [ "$1" = 'protobuf' ] || [ "$2" = 'protobuf' ]; then
	cd "$CHEFROOT_BUILD_DEPS/protobuf"
	make install
	ldconfig
fi
