#!/usr/bin/env sh

# WARNING!
# This script makes use of the `local' keyword. Although it is not POSIX
# compliant, all shells on Linux (dash, ash, bash, zsh, ...) support it.

runpath="$(readlink -f "$(dirname "$0")")"

version=3.2
baseurl="http://llvm.org/releases/$version"
basedir=/opt/s2e/llvm
prefix="$basedir/llvm-${version}-native"

cores=1
test -e /proc/cpuinfo && cores=$(grep -c '^processor' /proc/cpuinfo)

export C_INCLUDE_PATH='/usr/include:/usr/include/x86_64-linux-gnu'
export CPLUS_INCLUDE_PATH="$C_INCLUDE_PATH:/usr/include/x86_64-linux-gnu/c++/4.8"

# HELPERS ======================================================================

die()
{
	retval=$1
	shift
	format="$1"
	test -n "$format" && shift
	printf "$(tput setaf 1)FATAL$(tput sgr0): ${format}\n" "$@" >&2
	exit $retval
}

fxp()
{
	local prog="$1"
	local vprog="$prog-$version"
	local srcdir="${vprog}.src"
	local srcball="${srcdir}.tar.gz"

	# Fetch:
	wget "$baseurl/$srcball"

	# Extract:
	tar -xzf "$srcball"

	# Patch:
	case "$prog" in
		llvm|clang) local patchname=memorytracer;;
		compiler-rt) local patchname=asan4s2e;;
	esac
	local patch="$runpath/$vprog-${patchname}.patch"
	patch -d "$srcdir" -p0 -i "$patch"

	# Clean up:
	rm -f "$srcball"
}

# COMMAND: FETCH ===============================================================

# Native LLVM is required to build the target LLVMs:
buildnative()
{
	srcdir="$basedir/llvm-${version}.src-native"
	builddir="$srcdir"

	# Prepare source directory:
	cp -r "llvm-${version}.src" "$srcdir"
	fxp clang
	mv "clang-${version}.src" "$srcdir/tools/clang"
	fxp compiler-rt
	mv "compiler-rt-${version}.src" "$srcdir/projects/compiler-rt"

	# Prepare build directory:
	mkdir -p "$builddir"
	cd "$builddir"

	# Build & install:
	"$srcdir/configure" \
		--prefix="$prefix" \
		--enable-jit \
		--enable-optimized \
		--disable-assertions
	make ENABLE_OPTIMIZED=1 -j$cores
	make install

	# Clean up:
	cd "$basedir"
	rm -rf "$srcdir" "$builddir"
}

fetch()
{
	# Get LLVM:
	cd "${basedir}"
	fxp llvm

	# Build and install native LLVM:
	buildnative
}

# COMMAND: BUILD ===============================================================

build_usage()
{
	cat <<- EOF

	$0 build: Build LLVM in a given mode

	Usage: $0 build MODE

	Modes:
	    release   Build LLVM in Release mode (=> Release+Asserts)
	    debug     Build LLVM in Debug mode (=> Debug+Asserts)

	EOF
	exit 1
}

build()
{
	srcdir="$basedir/llvm-${version}.src"
	builddir="$basedir/llvm-${version}.build"

	# Build mode:
	mode="$1"
	test -n "$mode" || build_usage
	shift
	case "$mode" in
	release)
		copt='--enable-optimized'
		mopt=1
		;;
	debug)
		copt='--disable-optimized'
		mopt=0
		;;
	*)
		build_usage
		;;
	esac

	# Prepare build directory:
	mkdir -p "$builddir"
	cd "$builddir"

	# Configure:
	"$srcdir/configure" \
		--enable-jit \
		--target=x86_64 \
		--enable-targets=x86 \
		$copt \
		CC="$prefix/bin/clang" \
		CXX="$prefix/bin/clang++"

	# Build:
	make ENABLE_OPTIMIZED=$mopt REQUIRES_RTTI=1 -j$cores
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF

	$0: Download and build LLVM for the S²E base image

	Usage: $0 COMMAND [ARGS ...]

	Commands:
	    fetch    Fetch the LLVM source, build and install native LLVM
	    build    Build target LLVM

	EOF
	exit 1
}

main()
{
	cmd="$1"
	test -n "$cmd" || usage
	shift
	set -e
	case "$cmd" in
		fetch) fetch "$@";;
		build) build "$@";;
		*) usage;;
	esac
	set +e
}

main "$@"
