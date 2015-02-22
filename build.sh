#!/usr/bin/env sh

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
SRCPATH_BASE="$RUNPATH"
SRCDIR_BASE="$(basename "$SRCPATH_BASE")"

# HELPERS ======================================================================
# Various helper functions for displaying error/warning/normal messages and
# checking, setting and displaying the compilation status of all the components.
# Some also interact with the user.

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

note()
{
	note_format="$1"
	shift
	printf "       $note_format\n" "$@"
}

skip()
{
	skip_format="$1"
	shift
	printf "[\033[34mSKIP\033[0m] $skip_format\n" "$@"
}

warn()
{
	warn_format="$1"
	shift
	printf "[\033[33mWARN\033[0m] $warn_format\n" "$@" >&2
}

success()
{
	printf "\033[1;32m>>>\033[0m Build successful in %s.\n" "$BUILDPATH_BASE"
}

fail()
{
	printf "\033[1;31m>>>\033[0m Build failed.\n"
	confirm "Do you want to examine $LOGFILE?" && less "$LOGFILE"
	exit 2
}

error()
{
	error_format="$1"
	shift
	printf "[\033[31mFATAL\033[0m] $error_format\n" "$@" >&2
	exit 127
}

check_status()
{
	if [ $FORCE -eq 0 ]; then
		rm -rf "$BUILDPATH"
		printf 1 > "$STATUSFILE"
		return 1
	elif [ -e "$BUILDPATH" ]; then
		if [ ! -e "$STATUSFILE" ]; then
			warn "%s not found, rebuilding" "$STATUSFILE"
			rm -rf "$BUILDPATH"
			printf 1 > "$STATUSFILE"
			return 1
		else
			if [ "$(cat "$STATUSFILE")" = '0' ]; then
				skip '%s' "$BUILDPATH"
				return 0
			else
				note '%s previously failed, rebuilding' "$BUILDPATH"
				return 1
			fi
		fi
	else
		return 1
	fi
}

set_status()
{
	printf '%d' "$1" >"$STATUSFILE"
}

confirm()
{
	confirm_msg="$1"
	shift
	while true; do
		printf "$confirm_msg [Y/n] "
		read a
		case "$a" in
			[Yy]*|'') return 0;;
			[Nn]*) return 1;;
		esac
	done
}

track()
{
	track_msg="$1"
	shift

	printf "[    ] %s ..." "$track_msg"
	track_status=0
	if [ $VERBOSE -eq 0 ]; then
		{ "$@" || track_status=1; } 2>&1 | tee -a "$LOGFILE"
	else
		{ "$@" || track_status=1; } >>"$LOGFILE" 2>>"$LOGFILE"
	fi
	printf "\r[\033[%s\033[0m] %s    \n" \
		"$(test $track_status -eq 0 && printf "32m OK " || printf "31mFAIL")" \
		"$track_msg"
	test $track_status -eq 0 || fail
}

# LUA ==========================================================================

lua_build()
{
	lua_name='lua'
	lua_version='5.1'
	lua_vname="$lua_name-$lua_version"
	lua_baseurl='http://www.lua.org/ftp'
	lua_tarball="${lua_vname}.tar.gz"

	# Build directory:
	if [ -e "$lua_tarball" ]; then
		skip "%s: Existing, not downloading again" "$lua_tarball"
	else
		track 'Downloading LUA' wget "$lua_baseurl/$lua_tarball"
	fi
	tar xzf "$lua_tarball"
	mv "$(basename "$lua_tarball" .tar.gz)" "$BUILDPATH"
	cd "$BUILDPATH"

	# Build:
	track 'Building LUA' make -j$JOBS linux
}

# STP ==========================================================================
# STP does not seem to allow building outside the source directory, so in order
# not pollute stuff, we need to copy the entire thing.
# Yay!

stp_build()
{
	stp_srcpath="$SRCPATH_BASE/stp"

	# Build directory:
	cp -r "$stp_srcpath" "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring STP' scripts/configure \
		--with-prefix="$BUILDPATH" \
		--with-fpic \
		--with-gcc="$LLVM_NATIVE_CC" \
		--with-g++="$LLVM_NATIVE_CXX" \
		$(test $ASAN -eq 0 && echo '--with-address-sanitizer')

	# Build:
	track 'Building STP' make -j$JOBS
}

# KLEE =========================================================================

klee_build()
{
	klee_srcpath="$SRCPATH_BASE/klee"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ "$MODE" = 'debug' ]; then
		klee_cxxflags='-g -O0'
		klee_ldflags='-g'
	fi
	if [ $ASAN -eq 0 ]; then
		klee_cxxflags="$klee_cxxflags -fsanitize=address"
		klee_ldflags="$klee_ldflags -fsanizite=address"
	fi
	track 'Configuring KLEE' "$klee_srcpath"/configure \
		--prefix="$BUILDPATH_BASE/opt" \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--target=x86_64 \
		--enable-exceptions \
		--with-stp="$BUILDPATH_BASE/stp" \
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
	track 'Building KLEE' make -j$JOBS $klee_buildopts
}

# LIBMEMTRACER =================================================================

libmemtracer_build()
{
	libmemtracer_srcpath="$SRCPATH_BASE/libmemtracer"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring libmemtracer' "$libmemtracer_srcpath"/configure \
		--enable-debug \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX"

	# Build:
	libmemtracer_buildopts="CLANG_CC=$LLVM_NATIVE_CC CLANG_CXX=$LLVM_NATIVE_CXX"
	track 'Building libmemtracer' make -j$JOBS $libmemtracer_buildopts
}

# LIBVMI =======================================================================

libvmi_build()
{
	libvmi_srcpath="$SRCPATH_BASE/libvmi"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring libvmi' "$libvmi_srcpath"/configure \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		--with-libmemtracer-src="$SRCPATH_BASE/libmemtracer" \
		$(test "$MODE" = 'debug' && echo '--enable-debug') \
		CC=$LLVM_NATIVE_CC \
		CXX=$LLVM_NATIVE_CXX

	# Build:
	track 'Building libvmi' make -j$JOBS
}

# QEMU =========================================================================

qemu_build()
{
	qemu_srcpath="$SRCPATH_BASE/qemu"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	case "$ARCH" in
		x86_64) qemu_target_list='x86_64-s2e-softmmu,x86_64-softmmu' ;;
		i386) qemu_target_list='i386-s2e-softmmu,i386-softmmu' ;;
		*) error 'qemu: internal error when determining architecture' ;;
	esac
	track 'Configuring qemu' "$qemu_srcpath"/configure \
	#echo \
		--with-klee="$BUILDPATH_BASE/klee/$ASSERTS" \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		--with-libvmi-libdir="$BUILDPATH_BASE/libvmi" \
		$(test "$MODE" = 'debug' && echo '--enable-debug') \
		--prefix="$BUILDPATH_BASE/opt" \
		--cc="$LLVM_NATIVE_CC" \
		--cxx="$LLVM_NATIVE_CXX" \
		--target-list="$qemu_target_list" \
		--enable-llvm \
		--enable-s2e \
		--with-pkgversion=S2E \
		--enable-boost \
		--with-liblua="$BUILDPATH_BASE/lua/src" \
		--extra-cxxflags=-Wno-deprecated \
		--with-libvmi-incdir="$SRCPATH_BASE/libvmi/include" \
		--disable-virtfs \
		--with-stp="$BUILDPATH_BASE/stp" \
		$(test $ASAN -eq 0 && printf '--enable-address-sanitizer') \
		$(test $WITH_LIBMT -eq 0 \
		 && echo "--extra-ldflags='-L$BUILDPATH_BASE/libmemtracer -lmemtracer'"\
		 && echo '--enable-memory-tracer') \
		"$QEMU_FLAGS"

	# Build:
	track 'Building qemu' make -j$JOBS
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

tests_build()
{
	true
}

# ALL ==========================================================================

all_build()
{
	for BUILDDIR in lua stp klee libmemtracer libvmi qemu tools guest tests; do
		BUILDPATH="$BUILDPATH_BASE/$BUILDDIR"
		STATUSFILE="${BUILDPATH}.status"
		LOGFILE="${BUILDPATH}.log"
		printf '' > "$LOGFILE"
		if ! check_status; then
			case "$BUILDDIR" in
				lua) lua_build ;;
				stp) stp_build ;;
				klee) klee_build ;;
				libmemtracer) libmemtracer_build ;;
				libvmi) libvmi_build ;;
				qemu) qemu_build ;;
				tools) tools_build ;;
				guest) guest_build ;;
				tests) tests_build ;;
				*) error 'Unhandled component: %s' "$BUILDDIR"
			esac
		fi
		LOGFILE='/dev/null'
		printf '0' >"$STATUSFILE"
	done
	success
}

# MAIN =========================================================================

usage()
{
	cat >&2 <<- EOF

	$RUNNAME: build S²E-chef in a specific configuration.

	Usage: $0 [OPTIONS ...] ARCH MODE

	Options:
	    -a          Build chef with Address Sanitizer
	    -b PATH     Build chef in PATH [default=$BUILDDIR_BASE]
	    -f          Force-rebuild
	    -h          Display this help
	    -j N        Compile with N jobs [default=$JOBS]
	    -l PATH     Path to where the native LLVM-3.2 files are installed [default=$LLVM_BASE]
	    -m          Build chef with libmemtracer
	    -q FLAGS    Additional flags passed to qemu's \`configure\` script
	    -v          Verbose: show compilation messages/warnings/errors on the console

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
	BUILDDIR_BASE='./build'
	FORCE=1
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux) JOBS=$(grep -c '^processor' /proc/cpuinfo) ;;
		*) JOBS=2 ;;
	esac
	LLVM_BASE='/opt/s2e/llvm'
	WITH_LIBMT=1
	QEMU_FLAGS=''
	VERBOSE=1

	# Options:
	while getopts ab:fhj:l:mq:v opt; do
		case "$opt" in
			a) ASAN=0 ;;
			b) BUILDDIR_BASE="$OPTARG" ;;
			f) FORCE=0 ;;
			h) usage ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			m) WITH_LIBMT=0 ;;
			q) QEMU_FLAGS="$OPTARG" ;;
			v) VERBOSE=0 ;;
			'?') die_help ;;
		esac
	done

	# Check conflicts:
	if [ $WITH_LIBMT -eq 0 ] && [ $ASAN -eq 0 ]; then
		die 1 'Cannot use libmemtracer and Address Sanitizer simultaneously.'
	fi

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
		release) ASSERTS='Release+Asserts' ;;
		debug) ASSERTS='Debug+Asserts' ;;
		'') die_help "missing mode" ;;
		*) die_help "invalid mode: '%s'" "$MODE" ;;
	esac
}

main()
{
	LOGFILE='/dev/null'

	# Command line arguments:
	get_options "$@"
	shift $(($OPTIND - 1))
	get_architecture "$@"
	shift
	get_mode "$@"
	shift
	test -z "$1" || die_help "trailing arguments: $@"

	# Build:
	BUILDPATH_BASE="$(readlink -f "$BUILDDIR_BASE")"
	mkdir -p "$BUILDPATH_BASE"
	cd "$BUILDPATH_BASE"

	# Build in build directory:
	all_build
}

set -e
main "$@"
set +e
