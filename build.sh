#!/usr/bin/env sh

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
REPOPATH="$RUNPATH"
REPODIR="$(basename "$REPOPATH")"

# HELPERS ======================================================================

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
	die_format="$1"
	if [ -n "$die_format" ]; then
		shift
		printf "$die_format\n" "$@" >&2
	fi
	die 1 "Run \`$0 -h\` for help."
}

track()
{
	track_ok=0
	track_msg="$1"
	shift
	printf "[    ] %s ..." "$track_msg"
	"$@" >/dev/null || track_ok=1
	printf "\r[\033[%s\033[0m] %s    \n" \
		"$(test $track_ok -eq 0 && printf "32m OK " || printf "31mFAIL")" \
		"$track_msg"
	return $track_ok
}

note()
{
	printf "[\033[1mNOTE\033[0m] %s\n" "$1"
}

skip()
{
	printf "[\033[34mSKIP\033[0m] %s\n" "$1"
}

warn()
{
	printf "[\033[33mWARN\033[0m] %s\n" "$1"
}

check_builddir()
{
	if [ $FORCE -eq 0 ]; then
		note "${1}: force-rebuild, ignoring status file"
		rm -rf "$1"
		rm -f "${1}.status"
	elif [ -e "${1}" ]; then
		if [ ! -e "${1}.status" ]; then
			warn "${1}: corresponding status file not found (skipping)"
			return 1
		else
			if [ "$(cat "${1}.status")" = '0' ]; then
				skip "$1: already built successfully, not rebuilding"
				return 1
			else
				note "$1: previously failed attempt to build, rebuilding"
			fi
		fi
	fi
	return 0
}

mark_builddir()
{
	echo $1 > "${2}.status"
	test $1 -eq 0 || exit 2
}

# LUA ==========================================================================

lua_build()
{
	lua_name='lua'
	lua_version='5.1'
	lua_vname="$lua_name-$lua_version"
	lua_baseurl='http://www.lua.org/ftp'
	lua_tarball="${lua_vname}.tar.gz"
	lua_builddir="$(basename "$lua_tarball" .tar.gz)"
	lua_buildpath="$BUILDPATH/$lua_builddir"

	# Build directory:
	check_builddir "$lua_buildpath" || return 0
	if [ -e "$lua_tarball" ]; then
		skip "$lua_tarball found, not downloading again"
	else
		wget "$lua_baseurl/$lua_tarball"
	fi
	tar xzf "$lua_tarball"
	cd "$lua_buildpath"

	# Build:
	track 'building lua' \
		make -j$JOBS linux || \

	# Finish:
	mark_builddir $? "$lua_buildpath"
}

# STP ==========================================================================
# STP does not seem to allow building outside the source directory, so in order
# not pollute stuff, we need to copy the entire thing.
# Yay!

stp_build()
{
	stp_srcpath="$REPOPATH/stp"
	stp_buildpath="$BUILDPATH/stp"

	# Build directory:
	check_builddir "$stp_buildpath" || return 0
	cp -r "$stp_srcpath" "$stp_buildpath"
	cd "$stp_buildpath"

	# Configure:
	track 'configuring STP' scripts/configure \
		--with-prefix="$stp_buildpath" \
		--with-fpic \
		--with-gcc="$LLVM_NATIVE_CC" \
		--with-g++="$LLVM_NATIVE_CXX" \
		$(test $ASAN -eq 0 && echo '--with-address-sanitizer')

	# Build:
	track 'building STP' make -j$JOBS

	# Finish:
	mark_builddir $? "$stp_buildpath"
}

# KLEE =========================================================================

klee_build()
{
	klee_srcpath="$REPOPATH/klee"
	klee_buildpath="$BUILDPATH/opt"

	# Build directory:
	check_builddir "$klee_buildpath" || return 0
	mkdir -p "$klee_buildpath"
	cd "$klee_buildpath"

	# Configure:
	if [ "$MODE" = 'debug' ]; then
		klee_cxxflags='-g -O0'
		klee_ldflags='-g'
	fi
	if [ $ASAN -eq 0 ]; then
		klee_cxxflags="$klee_cxxflags -fsanitize=address"
		klee_ldflags="$klee_ldflags -fsanizite=address"
	fi
	track 'configuring KLEE' "$klee_srcpath"/configure \
		--prefix="$klee_buildpath" \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--target=x86_64 \
		--enable-exceptions \
		--with-stp="$stp_buildpath" \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
		$klee_cxxflags \
		$klee_ldflags

	# Build:
	if [ "$MODE" = 'debug' ]; then
		klee_buildopts='ENABLE_OPTIMIZED=0'
	else
		klee_buildopts='ENABLE_OPTIMIZED=1'
	fi
	track 'building KLEE' make -j$JOBS $klee_buildopts

	# Finish:
	mark_builddir $? "$klee_buildpath"
}

# LIBMEMTRACER =================================================================

libmt_build()
{
	libmt_srcpath="$REPOPATH/libmemtracer"
	libmt_buildpath="$BUILDPATH/libmemtracer"

	# Build directory:
	check_builddir "$libmt_buildpath" || return 0
	mkdir -p "$libmt_buildpath"
	cd "$libmt_buildpath"

	# Configure:
	track 'configuring libmemtracer' "$libmt_srcpath"/configure \
		--enable-debug \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \

	# Build:
	libmt_buildopts="CLANG_CC=$LLVM_NATIVE_CC CLANG_CXX=$LLVM_NATIVE_CXX"
	track 'building libmemtracer' make -j$JOBS $libmt_buildopts

	# Finish:
	mark_builddir $? "$libmt_buildpath"
}

# LIBVMI =======================================================================

libvmi_build()
{
	libvmi_srcpath="$REPOPATH/libvmi"
	libvmi_buildpath="$BUILDPATH/libvmi"

	# Build directory:
	check_builddir "$libvmi_buildpath" || return 0
	mkdir -p "$libvmi_buildpath"
	cd "$libvmi_buildpath"

	# Configure:
	track 'configuring libvmi' "$libvmi_srcpath"/configure \
		--with-llvm="$LLVM_BUILD_SUB" \
		--with-libmemtracer-src="$libmt_srcpath" \
		$(test "$MODE" = 'debug' && echo '--enable-debug') \
		CC=$LLVM_NATIVE_CC \
		CXX=$LLVM_NATIVE_CXX

	# Build:
	track 'building libvmi' make -j$JOBS

	# Finish:
	mark_builddir $? "$libvmi_buildpath"
}

# QEMU =========================================================================

qemu_build()
{
	qemu_srcpath="$REPOPATH/qemu"
	qemu_buildpath="$BUILDPATH/qemu"

	return

	# Build directory:
	#check_builddir "$qemu_buildpath" || return 0
	#mkdir -p "$qemu_buildpath"
}

# TOOLS ========================================================================

tools_build()
{
	true
}

# GUEST TOOLS ==================================================================

guest_build()
{
	true
}

# TEST SUITE ===================================================================

test_build()
{
	true
}

# ALL ==========================================================================

all_build()
{
	lua_build
	stp_build
	klee_build
	libmt_build
	libvmi_build
	qemu_build
	tools_build
	guest_build
	test_build
}

# MAIN =========================================================================

usage()
{
	cat >&2 <<- EOF

	$RUNNAME: build S²E-chef in a specific configuration.

	Usage: $0 [OPTIONS ...] ARCH MODE

	Options:
	    -a          Build with Address Sanitizer
	    -b PATH     Build chef in PATH [default=$BUILDDIR]
	    -c          Clean (instead of build)
	    -f          Force-rebuild
	    -h          Display this help
	    -j N        Compile with N jobs [default=$JOBS]
	    -l PATH     Path to where the native the LLVM-3.2 files are installed
	                [default=$LLVM_BASE]

	Architectures:
	    i386        Build 32 bit x86
	    x86_64      Build 64 bit x86

	Modes:
	    release     Release mode
	    debug       Debug mode

	EOF
	exit 1
}

get_options()
{
	# Default values:
	ASAN=1
	BUILDDIR='./build'
	CLEAN=1
	FORCE=1
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux)  JOBS=$(grep -c '^processor' /proc/cpuinfo)             ;;
		*)      JOBS=2                                                 ;;
	esac
	LLVM_BASE='/opt/s2e/llvm'

	# Options:
	while getopts ab:cfhj:kl: opt; do
		case "$opt" in
			a) ASAN=0 ;;
			b) BUILDDIR="$OPTARG" ;;
			c) CLEAN=0 ;;
			f) FORCE=0 ;;
			h) usage ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			'?') die_help ;;
		esac
	done

	# Dependent values:
	LLVM_NATIVE="$LLVM_BASE/llvm-3.2-native"
	LLVM_SRC="$LLVM_BASE/llvm-3.2.src"
	LLVM_BUILD="$LLVM_BASE/llvm-3.2.build"
	LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
	LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"
}

get_architecture()
{
	ARCH="$1"
	case "$ARCH" in
		i386|x86_64) ;;
		'') die_help "missing architecture" ;;
		*) die_help "invalid architecture: '%s'" "$ARCH" ;;
	esac
}

get_mode()
{
	MODE="$1"
	case "$MODE" in
		release) LLVM_BUILD_SUB="$LLVM_BUILD/Release+Asserts";;
		debug)   LLVM_BUILD_SUB="$LLVM_BUILD/Debug+Asserts"  ;;
		'') die_help "missing mode" ;;
		*) die_help "invalid mode: '%s'" "$MODE" ;;
	esac
}

main()
{
	# Command line arguments:
	get_options "$@"
	shift $(($OPTIND - 1))
	get_architecture "$@"
	shift
	get_mode "$@"
	shift

	# Check for trailing arguments:
	test -z "$1" || die_help "trailing arguments: $@"

	# Build/clean each configuration:
	BUILDPATH="$(readlink -f "$BUILDDIR")"

	# Clean:
	if [ $CLEAN -eq 0 ]; then
		track "removing $BUILDPATH" rm -rf $BUILDPATH
		continue
	fi

	# Build:
	test $ASAN -eq 0 && BUILDPATH="$BUILDPATH-asan"
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Build in build directory:
	all_build
}

set -e
main "$@"
set +e
