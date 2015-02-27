#!/usr/bin/env sh

RUNNAME="$(basename "$0")"
RUNPATH="$(readlink -f "$(dirname "$0")")"
SRCPATH_BASE="$RUNPATH"
SRCDIR_BASE="$(basename "$SRCPATH_BASE")"
COMPONENTS='lua stp klee libmemtracer libvmi qemu tools guest gmock tests'
USERNAME="$(id -un)"

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
	test $VERBOSE -ne 0 && printf "       $note_format\n" "$@"
}

skip()
{
	skip_format="$1"
	shift
	test $VERBOSE -ne 0 && printf "[\033[34mSKIP\033[0m] $skip_format\n" "$@"
}

warn()
{
	warn_format="$1"
	shift
	test $VERBOSE -ne 0 && printf "[\033[33mWARN\033[0m] $warn_format\n" "$@">&2
}

success()
{
	printf "\033[1;32m>>>\033[0m Successfully built S²E-chef in %s.\n" \
	       "$BUILDPATH_BASE"
}

fail()
{
	confirm "\033[1;31m>>>\033[0m Build failed. Examine $LOGFILE?" && \
	        less "$LOGFILE"
	exit 2
}

internal_error()
{
	internal_error_format="$1"
	shift
	printf "[\033[31mFATAL\033[0m] Internal Error: $internal_error_format\n" \
	       "$@" >&2
	exit 127
}

check_stamp()
{
	if [ $FORCE -eq 0 ]; then
		# Build from scratch (forced):
		rm -rf "$BUILDPATH"
		rm -f "$STAMPFILE"
		return 1
	elif [ -e "$BUILDPATH" ]; then
		# Build from last state (if previously failed):
		if [ -e "$STAMPFILE" ]; then
			skip '%s' "$BUILDPATH"
			return 0
		else
			note '%s previously failed, rebuilding' "$BUILDPATH"
			return 1
		fi
	else
		# Build from scratch:
		return 1
	fi
}

set_stamp()
{
	touch "$STAMPFILE"
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

	test $VERBOSE -ne 0 && printf "[    ] %s ..." "$track_msg"
	track_status=0
	if [ $VERBOSE -eq 0 ]; then
		{ "$@" || track_status=1; } 2>&1 | tee -a "$LOGFILE"
	else
		{ "$@" || track_status=1; } >>"$LOGFILE" 2>>"$LOGFILE"
	fi
	test $VERBOSE -ne 0 && printf "\r[\033[%s\033[0m] %s    \n" \
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
	if [ ! -d "$BUILDPATH" ]; then
		if [ -e "$lua_tarball" ]; then
			skip '%s found, not downloading again' "$lua_tarball"
		else
			track 'Downloading LUA' wget "$lua_baseurl/$lua_tarball"
		fi
		tar xzf "$lua_tarball"
		mv "$(basename "$lua_tarball" .tar.gz)" "$BUILDPATH"
	fi
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
		$(test "$MODE" = 'asan' && echo '--with-address-sanitizer')

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
	if [ "$TARGET" = 'debug' ]; then
		klee_cxxflags='-g -O0'
		klee_ldflags='-g'
	fi
	if [ "$MODE" = 'asan' ]; then
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
		CXXFLAGS="$klee_cxxflags" \
		LDFLAGS="$klee_ldflags"

	# Build:
	if [ "$TARGET" = 'debug' ]; then
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
		$(test "$TARGET" = 'debug' && echo '--enable-debug') \
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
	case "$MODE" in
	asan)
		qemu_confopt='--enable-address-sanitizer' ;;
	libmemtracer)
		qemu_confopt="--enable-memory-tracer"
		;;
	*)
		;;
	esac
	track 'Configuring qemu' "$qemu_srcpath"/configure \
		--with-klee="$BUILDPATH_BASE/klee/$ASSERTS" \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		--with-libvmi-libdir="$BUILDPATH_BASE/libvmi" \
		$(test "$TARGET" = 'debug' && echo '--enable-debug') \
		--prefix="$BUILDPATH_BASE/opt" \
		--cc="$LLVM_NATIVE_CC" \
		--cxx="$LLVM_NATIVE_CXX" \
		--target-list="$ARCH-s2e-softmmu,$ARCH-softmmu" \
		--enable-llvm \
		--enable-s2e \
		--with-pkgversion=S2E \
		--enable-boost \
		--with-liblua="$BUILDPATH_BASE/lua/src" \
		--extra-cxxflags=-Wno-deprecated \
		--with-libvmi-incdir="$SRCPATH_BASE/libvmi/include" \
		--disable-virtfs \
		--with-stp="$BUILDPATH_BASE/stp" \
		$qemu_confopt \
		$(test "$MODE" = 'libmemtracer' && \
		  echo "--extra-ldflags='-L$BUILDPATH_BASE/libmemtracer -lmemtracer'")\
		$QEMU_FLAGS

	# Build:
	track 'Building qemu' make -j$JOBS
}

# TOOLS ========================================================================

tools_build()
{
	tools_srcdir="$SRCPATH_BASE/tools"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring tools' "$tools_srcdir"/configure \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--with-s2esrc="$SRCPATH_BASE/qemu" \
		--target=x86_64 \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
		ENABLE_OPTIMIZED=$(test "$TARGET" = 'release' && echo 1 || echo 0) \
		REQUIRES_RTTI=1

	# Build:
	track 'Building tools' make -j$JOBS
}

# GUEST TOOLS ==================================================================

guest_build()
{
	guest_srcpath="$SRCPATH_BASE/guest"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring guest tools' "$guest_srcpath"/configure

	# Build:
	case "$ARCH" in
		i386) guest_cflags='-m32' ;;
		x86_64) guest_cflags='-m64' ;;
		*) internal_error 'Unknown architecture: %s' "$ARCH" ;;
	esac
	track 'Building guest tools' make -j$JOBS CFLAGS="$guest_cflags"
}

# GMOCK ========================================================================

gmock_build_build()
{
	make -j$JOBS
	cd lib
	"$LLVM_NATIVE_CC" \
		-D__STDC_LIMIT_MACROS \
		-D__STDC_CONSTANT_MACROS \
		-I"$LLVM_SRC/include" \
		-I"$LLVM_NATIVE/include" \
		-I"$gtest_path/include" \
		-I"$gtest_path" \
		-I"$BUILDPATH/include" \
		-I"$BUILDPATH" \
		-c "$BUILDPATH/src/gmock-all.cc"
	ar -rv libgmock.a gmock-all.o
}

gmock_build()
{
	gmock_name='gmock'
	gmock_version='1.6.0'
	gmock_vname="$gmock_name-$gmock_version"
	gmock_baseurl='http://googlemock.googlecode.com/files'
	gmock_zip="${gmock_vname}.zip"
	gtest_path="$LLVM_SRC/utils/unittest/googletest"

	# Build directory:
	if [ ! -d "$BUILDPATH" ]; then
		if [ -e "$gmock_zip" ]; then
			skip '%s found, not downloading again' "$gmock_zip"
		else
			track 'Downloading Google Mock' wget "$gmock_baseurl/$gmock_zip"
		fi
		unzip -q "$gmock_zip"
		mv "$(basename "$gmock_zip" .zip)" "$BUILDPATH"
	fi
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring Google Mock' ./configure \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX"

	# Build:
	track 'Building Google Mock' gmock_build_build
}

# TEST SUITE ===================================================================

tests_build()
{
	tests_srcpath="$SRCPATH_BASE/testsuite"

	# Build directory:
	mkdir -p "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	track 'Configuring test suite' "$tests_srcpath"/configure \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--with-s2e-src="$SRCPATH_BASE/qemu" \
		--with-s2eobj-release="$BUILDPATH_BASE/qemu" \
		--with-s2eobj-debug="$BUILDPATH_BASE/qemu" \
		--with-klee-src="$SRCPATH_BASE/klee" \
		--with-klee-obj="$BUILDPATH_BASE/klee" \
		--with-gmock="$BUILDPATH_BASE/gmock" \
		--with-stp="$BUILDPATH_BASE/stp" \
		--with-clang-profile-lib="$LLVM_NATIVE_LIB" \
		--target=x86_64 \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
		REQUIRES_EH=1

	# Build:
	tests_isopt=$(test "$TARGET" = 'debug' && echo 0 || echo 1)
	tests_buildopts="REQUIRES_RTTI=1 REQUIRE_EH=1 ENABLE_OPTIMIZED=$tests_isopt"
	track 'Building test suite' make -j$JOBS $tests_buildopts
}

# ALL ==========================================================================

all_build()
{
	for BUILDDIR in $COMPONENTS
	do
		# Test whether this component needs to be ignored or not
		cont=1
		for i in $IGNORED; do
			if [ "$BUILDDIR" = "$i" ]; then
				skip '%s: ignored' "$BUILDDIR"
				cont=0
				break
			fi
		done
		test $cont -ne 0 || continue

		# Build:
		cd "$BUILDPATH_BASE"
		BUILDPATH="$BUILDPATH_BASE/$BUILDDIR"
		STAMPFILE="${BUILDPATH}.stamp"
		LOGFILE="${BUILDPATH}.log"
		if ! check_stamp; then
			printf '' > "$LOGFILE"
			${BUILDDIR}_build
		fi
		LOGFILE='/dev/null'
		set_stamp
	done
	success
}

# DOCKER =======================================================================

docker_image_exists()
{
	docker inspect "$1" >/dev/null 2>&1
}

docker_prepare_image()
{
	dockerimg="$USERNAME/s2e-$1"
	dockerfile="$DOCKERPATH/image/$1/Dockerfile"

	if docker_image_exists "$dockerimg"; then
		skip '%s: image already exists' "$dockerimg"
		return
	fi

	# XXX 'FROM' line correction, temporary, until docker hub is up to date:
	if [ $1 = 'chef' ]; then
		# FIXME assuming $USERNAME contains no problematic characters:
		printf "%%s/stefanbucur/%s/g\nw\nq\n" "$USERNAME" | \
			ex -s "$dockerfile" || true
	fi
	track "Building $1 docker image" docker build \
		--rm \
		--tag="$dockerimg" \
		"$(dirname "$dockerfile")"
}

docker_prepare()
{
	if [ -d "$DOCKERPATH" ]; then
		cd "$DOCKERPATH"
		track 'Updating s2e_docker repository' git pull
		cd -
	else
		track 'Cloining s2e_docker repository' \
			git clone "https://github.com/stefanbucur/s2e_docker" "$DOCKERPATH"
	fi

	for i in base chef; do
		docker_prepare_image "$i"
	done
}

docker_build()
{
	dockerimg="$USERNAME/s2e-chef"
	dockercont="zopf_${ARCH}_${TARGET}_${MODE}"

	if ! docker_image_exists "$dockerimg"; then
		die 2 '%s: image does not exist' "$dockerimg"
	fi
	note 'Building chef in %s:%s' "$dockerimg" "$dockercont"
	if docker_image_exists "$dockercont"; then
		if [ $FORCE -eq 0 ]; then
			docker rm "$dockercont"
		else
			docker start -a -i "$dockercont"
		fi
	else
		docker run \
			--name="$dockercont" \
			-t \
			-i \
			-v "$SRCPATH_BASE":/host \
			"$dockerimg" \
			/host/"$RUNNAME" -z $BUILDARGS
	fi
	docker commit "$dockercont" "$dockerimg:$dockercont"
	docker rm "$dockercont"
}

# MAIN =========================================================================

usage()
{
	cat >&2 <<- EOF
	$RUNNAME: build S²E-chef in a specific configuration.

	Usage: $0 [OPTIONS ...] ARCH TARGET [MODE]

	Architectures:
	    i386
	    x86_64

	Targets:
	    release
	    debug

	Mode:
	    normal [default]
	    asan
	    libmemtracer

	Options:
	    -b PATH    Build chef in PATH [default=$BUILDDIR_BASE]
		-d PATH    Download s2e_docker repository to PATH [default=$DOCKERPATH]
	    -f         Force-rebuild
	    -h         Display this help
	    -i COMPS   Ignore components COMPS (see below for a list) [default='$IGNORED_DEFAULT']
	    -j N       Compile with N jobs [default=$JOBS]
	    -l PATH    Path to where the native LLVM-3.2 files are installed [default=$LLVM_BASE]
	    -q FLAGS   Additional flags passed to qemu's \`configure\` script
	    -v         Verbose: show compilation messages/warnings/errors on the console
	    -z         Direct mode (you won't need this)

	Components:
	EOF
	echo "    $COMPONENTS"
	echo
	exit 1
}

get_options()
{
	# Default values:
	BUILDDIR_BASE='./build'
	DIRECT=1
	DOCKERPATH='./s2e_docker'
	FORCE=1
	IGNORED=''
	IGNORED_DEFAULT='gmock tests'
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux) JOBS=$(grep -c '^processor' /proc/cpuinfo) ;;
		*) JOBS=2 ;;
	esac
	LLVM_BASE='/opt/s2e/llvm'
	VERBOSE=1

	# Options:
	while getopts b:d:fhi:j:l:q:vz opt; do
		case "$opt" in
			b) BUILDDIR_BASE="$OPTARG" ;;
			d) DOCKERPATH="$OPTARG" ;;
			f) FORCE=0 ;;
			h) usage ;;
			i) IGNORED="$IGNORE $OPTARG" ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			v) VERBOSE=0 ;;
			z) DIRECT=0 ;;
			'?') die_help ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))

	# Dependent values:
	LLVM_SRC="$LLVM_BASE/llvm-3.2.src"
	LLVM_BUILD="$LLVM_BASE/llvm-3.2.build"
	LLVM_NATIVE="$LLVM_BASE/llvm-3.2-native"
	LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
	LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"
	LLVM_NATIVE_LIB="$LLVM_NATIVE/lib"

	test -z "$IGNORED" && IGNORED="$IGNORED_DEFAULT"
}

get_architecture()
{
	ARCH="$1"
	case "$ARCH" in
		i386|x86_64) ;;
		'') die_help "missing architecture" ;;
		*) die_help "invalid architecture: '%s'" "$ARCH" ;;
	esac
	ARGSHIFT=1
}

get_target()
{
	TARGET="$1"
	case "$TARGET" in
		release) ASSERTS='Release+Asserts' ;;
		debug) ASSERTS='Debug+Asserts' ;;
		'') die_help "missing target" ;;
		*) die_help "invalid target: '%s'" "$TARGET" ;;
	esac
	ARGSHIFT=1
}

get_mode()
{
	MODE="$1"
	case "$MODE" in
		asan|libmemtracer|normal) ARGSHIFT=1 ;;
		'') MODE='normal'; ARGSHIFT=0 ;;
		*) die_help "invalid mode: '%s'" "$MODE" ;;
	esac
}

main()
{
	LOGFILE='./build.log'
	BUILDARGS="$@"

	# Command line arguments:
	get_options "$@"
	shift $ARGSHIFT
	get_architecture "$@"
	shift $ARGSHIFT
	get_target "$@"
	shift $ARGSHIFT
	get_mode "$@" || shift
	shift $ARGSHIFT
	test $# -eq 0 || die_help "trailing arguments: $@"

	LOGFILE="$(readlink -f "$LOGFILE")"
	printf '' >"$LOGFILE"

	if [ $DIRECT -eq 0 ]; then
		# Build:
		BUILDPATH_BASE="$(readlink -f "$BUILDDIR_BASE")"
		mkdir -p "$BUILDPATH_BASE"
		cd "$BUILDPATH_BASE"
		all_build
	else
		# Wrap build in docker:
		DOCKERPATH="$(readlink -f "$DOCKERPATH")"
		docker_prepare
		docker_build
	fi
}

set -e
main "$@"
set +e
