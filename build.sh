#!/usr/bin/env sh

runname="$(basename "$0")"
runpath="$(readlink -f "$(dirname "$0")")"
repopath="$runpath"
repodir="$(basename "$repopath")"

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

# LUA ==========================================================================

lua_name='lua'
lua_version='5.1'
lua_dir="$lua_name-$lua_version"
lua_baseurl='http://www.lua.org/ftp'
lua_tarball="${lua_dir}.tar.gz"

lua_build()
{
	# Get source:
	if [ -d "$lua_dir" ]; then
		if [ $FORCE -ne 0 ]; then
			skip "$lua_dir found, not rebuilding"
			return
		else
			skip "$lua_dir found, not extracting again"
			rm -rf "$lua_dir"
		fi
	fi

	# Extract source:
	if [ -e "$lua_tarball" ]; then
		skip "$lua_tarball found, not downloading again"
	else
		wget "$lua_baseurl/$lua_tarball"
	fi
	tar xzf "$lua_tarball"

	# Build:
	milestone 'building lua' make -j$JOBS -C "$lua_dir" linux
}

# STP ==========================================================================
# STP does not seem to allow building outside the source directory, so in order
# not pollute stuff, we need to copy the entire thing.
# Yay!

stp_dir='stp'
stp_path="$repopath/$stp_dir"

stp_build()
{
	stp_llvm_native="$LLVM_BASE/llvm-3.2-native"

	# Get source:
	if [ -d "$stp_dir" ]; then
		if [ $FORCE -ne 0 ]; then
			skip "$stp_dir found, not rebuilding again"
			return
		else
			rm -rf "$stp_dir"
		fi
	fi
	cp -r "$stp_path" "$stp_dir"
	cd "$stp_dir"

	# Configure:
	milestone 'configuring STP' scripts/configure \
		--with-prefix=$(pwd) \
		--with-fpic \
		--with-g++="$stp_llvm_native/bin/clang++" \
		--with-gcc="$stp_llvm_native/bin/clang" \
		$(test $ASAN -eq 0 && echo '--with-address-sanitizer')

	# Build:
	milestone 'building STP' make
}

# KLEE =========================================================================

klee_build()
{
	warn 'klee build not implemented yet'
}

# LIBMEMTRACER =================================================================

libmt_build()
{
	warn 'libmemtracer build not implemented yet'
}

# LIBVMI =======================================================================

libvmi_build()
{
	warn 'libvmi build not implemented yet'
}

# QEMU =========================================================================

qemu_build()
{
	warn 'qemu build not implemented yet'
}

# TOOLS ========================================================================

tools_build()
{
	warn 'tools build not implemented yet'
}

# GUEST TOOLS ==================================================================

guest_build()
{
	warn 'guest tools build not implemented yet'
}

# TEST SUITE ===================================================================

test_build()
{
	warn 'test suite build not implemented yet'
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

	$runname: build S²E-chef in a specific configuration.

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

	# Build/clean each configuration:
	BUILDBASE="$(readlink -f "$BUILDBASE")"
	for a in $ARCH; do
		for m in $MODE; do
			buildpath="$BUILDBASE/$a/$m"

			# Clean:
			if [ $CLEAN -eq 0 ]; then
				milestone "removing $buildpath" rm -rf $buildpath
				continue
			fi

			# Build:
			test $ASAN -eq 0 && buildpath="$buildpath-asan"
			test -d "$buildpath" && note "$buildpath already exists"
			mkdir -p "$buildpath"
			cd "$buildpath"

			# Build in build directory:
			all_build
		done
	done
}

set -e
main "$@"
set +e
