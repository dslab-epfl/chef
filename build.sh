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

milestone()
{
	msg="$1"
	shift
	printf "[    ] %s ..." "$msg"
	if "$@" >/dev/null; then
		printf "\r[ \033[32mOK\033[0m ] %s     \n" "$msg"
	else
		printf "\r[\033[31mFAIL\033[0m] %s     \n" "$msg"
		exit 2
	fi
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
			skip "$1 found, not rebuilding"
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
	check_builddir "$lua_builddir" || return
	if [ -e "$lua_tarball" ]; then
		skip "$lua_tarball found, not downloading again"
	else
		wget "$lua_baseurl/$lua_tarball"
	fi
	tar xzf "$lua_tarball"

	# Build:
	milestone 'building lua' make -j$JOBS -C "$lua_buildpath" linux
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
	check_builddir "$stp_buildpath" || return
	cp -r "$stp_srcpath" "$stp_buildpath"
	cd "$stp_buildpath"

	# Configure:
	milestone 'configuring STP' scripts/configure \
		--with-prefix="$stp_buildpath" \
		--with-fpic \
		--with-gcc="$LLVM_NATIVE_CC" \
		--with-g++="$LLVM_NATIVE_CXX" \
		$(test $ASAN -eq 0 && echo '--with-address-sanitizer')

	# Build:
	milestone 'building STP' make -j$JOBS
}

# KLEE =========================================================================

klee_build()
{
	klee_srcpath="$REPOPATH/klee"
	klee_buildpath="$BUILDPATH/klee"

	# Build directory:
	check_builddir "$klee_buildpath" || return
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
	milestone 'configuring KLEE' "$klee_srcpath"/configure \
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
	milestone 'building KLEE' \
		make -j$JOBS -C "$klee_buildpath" $klee_buildopts
}

# LIBMEMTRACER =================================================================

libmt_build()
{
	true
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

	Architectures:
	    i386         Build 32 bit x86
	    x86_64       Build 64 bit x86
	    all-archs    Build both 32 bit and 64 bit x86

	Modes:
	    release      Release mode
	    debug        Debug mode
	    all-modes    Both release and debug mode

	Flags:
	    asan         Build with Address Sanitizer

	EOF
	exit 1
}

main()
{
	# Options (default values):
	BUILDBASE='./build'
	CLEAN=1
	FORCE=1
	LLVM_BASE='/opt/s2e/llvm'
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux)  JOBS=$(grep -c '^processor' /proc/cpuinfo)             ;;
		*)      JOBS=2                                                 ;;
	esac

	# Options:
	while getopts b:cfhj:l: opt; do
		case "$opt" in
			b) BUILDBASE="$OPTARG" ;;
			c) CLEAN=0 ;;
			f) FORCE=0 ;;
			h) usage ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			'?') die_help ;;
		esac
	done
	shift $(($OPTIND - 1))

	# Architecture:
	ARCH="$1"
	case "$ARCH" in
		i386|x86_64) ;;
		all-archs) ARCH='i386 x86_64' ;;
		'') die_help "missing architecture" ;;
		*) die_help "invalid architecture: '%s'" "$ARCH" ;;
	esac
	shift

	# Mode:
	MODE="$1"
	case "$MODE" in
		release|debug) ;;
		all-modes) MODE='release debug' ;;
		'') die_help "missing mode" ;;
		*) die_help "invalid mode: '%s'" "$MODE" ;;
	esac
	shift

	# Flags:
	ASAN=1
	while [ -n "$1" ]; do
		flag="$1"
		case "$flag" in
			asan) ASAN=0 ;;
			*) usage ;;
		esac
		shift
	done

	# Native LLVM:
	LLVM_NATIVE="$LLVM_BASE/llvm-3.2-native"
	LLVM_SRC="$LLVM_BASE/llvm-3.2.src"
	LLVM_BUILD="$LLVM_BASE/llvm-3.2.build"
	LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
	LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"

	# Build/clean each configuration:
	BUILDBASE="$(readlink -f "$BUILDBASE")"
	for a in $ARCH; do
		for m in $MODE; do
			BUILDPATH="$BUILDBASE/$a/$m"

			# Clean:
			if [ $CLEAN -eq 0 ]; then
				milestone "removing $BUILDPATH" rm -rf $BUILDPATH
				continue
			fi

			# Build:
			test $ASAN -eq 0 && BUILDPATH="$BUILDPATH-asan"
			test -d "$BUILDPATH" && note "$BUILDPATH already exists"
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
