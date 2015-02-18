#!/usr/bin/env sh

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
REPOPATH="$RUNPATH"
REPODIR="$(basename "$REPOPATH")"

# HELPERS ======================================================================

die()
{
	retval=$1
	format="$2"
	shift 2
	printf "$format\n" "$@" >&2
	exit $retval
}

die_help()
{
	format="$1"
	if [ -n "$format" ]; then
		shift
		printf "$format\n" "$@" >&2
	fi
	die 1 "Run \`$0 -h\` for help."
}

die_clean()
{
	rm -rf "$1"
	exit 2
}

track()
{
	track_ok=0
	track_msg="$1"
	shift
	printf "[    ] %s ..." "$track_msg"
	{
		if [ $QUIET -eq 0 ]; then
			"$@" >/dev/null 2>&1
		else
			"$@" >/dev/null
		fi
	} || track_ok=1
	printf "\r[\033[%s\033[0m] %s    \n" \
		"$(test $track_ok -eq 0 && printf "32m OK " || printf "31mFAIL")" \
		"$track_msg"
	return $track_ok
}

note()
{
	msg="$1"
	shift
	printf "[\033[1mNOTE\033[0m] %s\n" "$msg"
}

skip()
{
	msg="$1"
	shift
	printf "[\033[34mSKIP\033[0m] %s\n" "$msg"
}

warn()
{
	msg="$1"
	shift
	printf "[\033[33mWARN\033[0m] %s\n" "$msg"
}

check_builddir()
{
	if [ -d "$1" ]; then
		if [ $FORCE -ne 0 ]; then
			skip "$1 exists, not rebuilding"
			return 1
		else
			rm -rf "$1"
		fi
	fi
	true
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
		die_clean "$lua_buildpath"
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
	track 'building STP' \
		make -j$JOBS || \
		die_clean "$stp_buildpath"
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
	track 'building KLEE' \
		make -j$JOBS $klee_buildopts || \
		die_clean "$klee_buildpath"
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
	track 'building libmemtracer' \
		make -j$JOBS $libmt_buildopts || \
		die_clean "$libmt_buildpath"
}

# LIBVMI =======================================================================

libvmi_build()
{
	true
}

# QEMU =========================================================================

qemu_build()
{
	true
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

	Usage: $0 [OPTION ...] ARCH MODE [FLAGS]

	Options:
	    -b PATH     Build chef in PATH [$BUILDBASE]
	    -c          Clean (instead of build) [false]
	    -f          Force-rebuild [false]
	    -h          Display this help
	    -j N        Compile with N jobs [$JOBS]
	    -l PATH     Path to where the native the LLVM-3.2 files are installed
	                [$LLVM_BASE]
	    -q          Suppress compilation warnings

	Architectures:
	    i386        Build 32 bit x86
	    x86_64      Build 64 bit x86
	    all-archs   Build both 32 bit and 64 bit x86

	Modes:
	    release     Release mode
	    debug       Debug mode
	    all-modes   Both release and debug mode

	Flags:
	    asan        Build with Address Sanitizer

	EOF
	exit 1
}

get_options()
{
	# Default values:
	BUILDBASE='./build'
	CLEAN=1
	FORCE=1
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux)  JOBS=$(grep -c '^processor' /proc/cpuinfo)             ;;
		*)      JOBS=2                                                 ;;
	esac
	LLVM_BASE='/opt/s2e/llvm'
	QUIET=1

	# Options:
	while getopts b:cfhj:l:q opt; do
		case "$opt" in
			b) BUILDBASE="$OPTARG" ;;
			c) CLEAN=0 ;;
			f) FORCE=0 ;;
			h) usage ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			q) QUIET=0 ;;
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
		all-archs) ARCH='i386 x86_64' ;;
		'') die_help "missing architecture" ;;
		*) die_help "invalid architecture: '%s'" "$ARCH" ;;
	esac
}

get_mode()
{
	MODE="$1"
	case "$MODE" in
		release|debug) ;;
		all-modes) MODE='release debug' ;;
		'') die_help "missing mode" ;;
		*) die_help "invalid mode: '%s'" "$MODE" ;;
	esac
}

get_flags()
{
	ASAN=1
	while [ -n "$1" ]; do
		flag="$1"
		case "$flag" in
			asan) ASAN=0 ;;
			*) usage ;;
		esac
		shift
	done
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
	get_flags "$@"

	# Check for trailing arguments:
	test -z "$1" || die_help "trailing arguments: $@"

	# Build/clean each configuration:
	BUILDBASE="$(readlink -f "$BUILDBASE")"
	for a in $ARCH; do
		for m in $MODE; do
			BUILDPATH="$BUILDBASE/$a/$m"

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
		done
	done
}

set -e
main "$@"
set +e
