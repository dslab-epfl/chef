#!/usr/bin/env sh
#
# This script builds a user-defined configuration of S²E-chef inside a prepared
# docker container.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

. "$(readlink -f "$(dirname "$0")")/utils.sh"

export C_INCLUDE_PATH='/usr/include:/usr/include/x86_64-linux-gnu'
export CPLUS_INCLUDE_PATH="$C_INCLUDE_PATH:/usr/include/x86_64-linux-gnu/c++/4.8"
COMPONENTS='lua stp klee libmemtracer libvmi qemu tools guest gmock tests'

# HELPERS ======================================================================
# Various helper functions for displaying error/warning/normal messages and
# checking, setting and displaying the compilation status of all the components.
# Some also interact with the user.

check_stamp()
{
	if [ $FORCE -eq $TRUE ]; then
		# Build from scratch (forced):
		rm -rf "$BUILDPATH"
		rm -f "$STAMPFILE"
		return $FALSE
	elif [ -e "$BUILDPATH" ]; then
		# Build from last state (if previously failed):
		if [ -e "$STAMPFILE" ]; then
			test $CHECK -eq $TRUE || skip '%s' "$BUILDPATH"
			return $TRUE
		else
			note '%s previously failed, rebuilding' "$BUILDPATH"
			return $FALSE
		fi
	else
		# Build from scratch:
		return $FALSE
	fi
}

set_stamp()
{
	touch "$STAMPFILE"
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
			track 'Downloading LUA' wget "$lua_baseurl/$lua_tarball" \
			|| return $FALSE
		fi
		tar xzf "$lua_tarball"
		mv "$(basename "$lua_tarball" .tar.gz)" "$BUILDPATH"
	fi
	cd "$BUILDPATH"

	# Build:
	track 'Building LUA' make -j$JOBS linux \
	|| return $FALSE
}

# STP ==========================================================================
# STP does not seem to allow building outside the source directory, so as not to
# pollute stuff, we need to copy the entire thing.
# Yay!

stp_build()
{
	stp_srcpath="$SRCPATH_ROOT/stp"

	# Build directory:
	cp -r "$stp_srcpath" "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring STP' scripts/configure \
			--with-prefix="$BUILDPATH" \
			--with-fpic \
			--with-gcc="$LLVM_NATIVE_CC" \
			--with-g++="$LLVM_NATIVE_CXX" \
			$(test "$MODE" = 'asan' && echo '--with-address-sanitizer') \
		|| return $FALSE
	fi

	# Build:
	track 'Building STP' make -j$JOBS \
	|| return $FALSE
}

# KLEE =========================================================================

klee_build()
{
	klee_srcpath="$SRCPATH_ROOT/klee"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		klee_cxxflags=''
		klee_cflags=''
		klee_ldflags=''
		if [ "$TARGET" = 'debug' ]; then
			klee_cxxflags="$klee_cxxflags -g -O0"
			klee_cflags="$klee_cflags -g -O0"
			klee_ldflags="$klee_ldflags -g"
		fi
		if [ "$MODE" = 'asan' ]; then
			klee_cxxflags="$klee_cxxflags -fsanitize=address"
			klee_cflags="$klee_cflags -fsanitize=address"
			klee_ldflags="$klee_ldflags -fsanizite=address"
		fi
		track 'Configuring KLEE' "$klee_srcpath"/configure \
			--prefix="$BUILDPATH_ROOT/opt" \
			--with-llvmsrc="$LLVM_SRC" \
			--with-llvmobj="$LLVM_BUILD" \
			--target=x86_64 \
			--enable-exceptions \
			--with-stp="$BUILDPATH_ROOT/stp" \
			CC="$LLVM_NATIVE_CC" \
			CXX="$LLVM_NATIVE_CXX" \
			CFLAGS="$klee_cflags" \
			CXXFLAGS="$klee_cxxflags" \
			LDFLAGS="$klee_ldflags" \
		|| return $FALSE
	fi

	# Build:
	if [ "$TARGET" = 'debug' ]; then
		klee_buildopts='ENABLE_OPTIMIZED=0'
	else
		klee_buildopts='ENABLE_OPTIMIZED=1'
	fi
	track 'Building KLEE' make -j$JOBS $klee_buildopts \
	|| return $FALSE
}

# LIBMEMTRACER =================================================================

libmemtracer_build()
{
	libmemtracer_srcpath="$SRCPATH_ROOT/libmemtracer"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring libmemtracer' "$libmemtracer_srcpath"/configure \
			--enable-debug \
			CC="$LLVM_NATIVE_CC" \
			CXX="$LLVM_NATIVE_CXX" \
		|| return $FALSE
	fi

	# Build:
	libmemtracer_buildopts="CLANG_CC=$LLVM_NATIVE_CC CLANG_CXX=$LLVM_NATIVE_CXX"
	track 'Building libmemtracer' make -j$JOBS $libmemtracer_buildopts \
	|| return $FALSE
}

# LIBVMI =======================================================================

libvmi_build()
{
	libvmi_srcpath="$SRCPATH_ROOT/libvmi"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring libvmi' "$libvmi_srcpath"/configure \
			--with-llvm="$LLVM_BUILD/$ASSERTS" \
			--with-libmemtracer-src="$SRCPATH_ROOT/libmemtracer" \
			$(test "$TARGET" = 'debug' && echo '--enable-debug') \
			CC=$LLVM_NATIVE_CC \
			CXX=$LLVM_NATIVE_CXX \
		|| return $FALSE
	fi

	# Build:
	track 'Building libvmi' make -j$JOBS \
	|| return $FALSE
}

# QEMU =========================================================================

qemu_install()
{
	make install
	cp "$ARCH-s2e-softmmu/op_helper.bc" \
		"$BUILDPATH_ROOT/opt/share/qemu/op_helper.bc.$ARCH"
	cp "$ARCH-softmmu/qemu-system-$ARCH" \
		"$BUILDPATH_ROOT/opt/bin/qemu-system-$ARCH"
	cp "$ARCH-s2e-softmmu/qemu-system-$ARCH" \
		"$BUILDPATH_ROOT/opt/bin/qemu-system-$ARCH-s2e"
}

qemu_build()
{
	qemu_srcpath="$SRCPATH_ROOT/qemu"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		case "$MODE" in
			asan) qemu_confopt='--enable-address-sanitizer' ;;
			libmemtracer) qemu_confopt='--enable-memory-tracer' ;;
			*) ;;
		esac
		track 'Configuring qemu' "$qemu_srcpath"/configure \
			--with-klee="$BUILDPATH_ROOT/klee/$ASSERTS" \
			--with-llvm="$LLVM_BUILD/$ASSERTS" \
			--with-libvmi-libdir="$BUILDPATH_ROOT/libvmi" \
			$(test "$TARGET" = 'debug' && echo '--enable-debug') \
			--prefix="$BUILDPATH_ROOT/opt" \
			--cc="$LLVM_NATIVE_CC" \
			--cxx="$LLVM_NATIVE_CXX" \
			--target-list="$ARCH-s2e-softmmu,$ARCH-softmmu" \
			--enable-llvm \
			--enable-s2e \
			--with-pkgversion=S2E \
			--enable-boost \
			--with-liblua="$BUILDPATH_ROOT/lua/src" \
			--extra-cxxflags=-Wno-deprecated \
			--with-libvmi-incdir="$SRCPATH_ROOT/libvmi/include" \
			--disable-virtfs \
			--with-stp="$BUILDPATH_ROOT/stp" \
			$qemu_confopt \
			--extra-ldflags="$(test "$MODE" = 'libmemtracer' && \
			  echo "-L$BUILDPATH_ROOT/libmemtracer -lmemtracer")"\
			$QEMU_FLAGS \
		|| return $FALSE
	fi

	# Build:
	track 'Building qemu' make -j$JOBS \
	|| return $FALSE

	# Install:
	track 'Installing qemu' qemu_install \
	|| return $FALSE
}

# TOOLS ========================================================================

tools_build()
{
	tools_srcdir="$SRCPATH_ROOT/tools"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring tools' "$tools_srcdir"/configure \
			--with-llvmsrc="$LLVM_SRC" \
			--with-llvmobj="$LLVM_BUILD" \
			--with-s2esrc="$SRCPATH_ROOT/qemu" \
			--target=x86_64 \
			CC="$LLVM_NATIVE_CC" \
			CXX="$LLVM_NATIVE_CXX" \
			ENABLE_OPTIMIZED=$(test "$TARGET" = 'release' && echo 1 || echo 0) \
			REQUIRES_RTTI=1 \
		|| return $FALSE
	fi

	# Build:
	track 'Building tools' make -j$JOBS \
	|| return $FALSE
}

# GUEST TOOLS ==================================================================

guest_build()
{
	guest_srcpath="$SRCPATH_ROOT/guest"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring guest tools' "$guest_srcpath"/configure \
		|| return $FALSE
	fi

	# Build:
	case "$ARCH" in
		i386) guest_cflags='-m32' ;;
		x86_64) guest_cflags='-m64' ;;
		*) die_internal "guest_build(): invalid architecture '%s'" "$ARCH" ;;
	esac
	track 'Building guest tools' make -j$JOBS CFLAGS="$guest_cflags" \
	|| return $FALSE
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
			track 'Downloading Google Mock' wget "$gmock_baseurl/$gmock_zip" \
			|| return $FALSE
		fi
		unzip -q "$gmock_zip"
		mv "$(basename "$gmock_zip" .zip)" "$BUILDPATH"
	fi
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring Google Mock' ./configure \
			CC="$LLVM_NATIVE_CC" \
			CXX="$LLVM_NATIVE_CXX" \
		|| return $FALSE
	fi

	# Build:
	track 'Building Google Mock' gmock_build_build \
	|| return $FALSE
}

# TEST SUITE ===================================================================

tests_build()
{
	tests_srcpath="$SRCPATH_ROOT/testsuite"

	# Build directory:
	test -d "$BUILDPATH" || mkdir "$BUILDPATH"
	cd "$BUILDPATH"

	# Configure:
	if [ $STAMPED -eq $FALSE ]; then
		track 'Configuring test suite' "$tests_srcpath"/configure \
			--with-llvmsrc="$LLVM_SRC" \
			--with-llvmobj="$LLVM_BUILD" \
			--with-s2e-src="$SRCPATH_ROOT/qemu" \
			--with-s2eobj-release="$BUILDPATH_ROOT/qemu" \
			--with-s2eobj-debug="$BUILDPATH_ROOT/qemu" \
			--with-klee-src="$SRCPATH_ROOT/klee" \
			--with-klee-obj="$BUILDPATH_ROOT/klee" \
			--with-gmock="$BUILDPATH_ROOT/gmock" \
			--with-stp="$BUILDPATH_ROOT/stp" \
			--with-clang-profile-lib="$LLVM_NATIVE_LIB" \
			--target=x86_64 \
			CC="$LLVM_NATIVE_CC" \
			CXX="$LLVM_NATIVE_CXX" \
			REQUIRES_EH=1 \
		|| return $FALSE
	fi

	# Build:
	tests_isopt=$(test "$TARGET" = 'debug' && echo 0 || echo 1)
	tests_buildopts="REQUIRES_RTTI=1 REQUIRE_EH=1 ENABLE_OPTIMIZED=$tests_isopt"
	track 'Building test suite' make -j$JOBS $tests_buildopts \
	|| return $FALSE
}

# ALL ==========================================================================

all_build()
{
	for BUILDDIR in $COMPONENTS
	do
		BUILDPATH="$BUILDPATH_ROOT/$BUILDDIR"

		# Test whether this component needs to be ignored or not
		cont=$FALSE
		for i in $IGNORED; do
			if [ "$BUILDDIR" = "$i" ]; then
				skip '%s: ignored' "$BUILDDIR"
				cont=$TRUE
				break
			fi
		done
		test $cont -ne $TRUE || continue

		# Test whether we run `make` on this component anyway:
		CHECK=$FALSE
		for i in $CHECKED; do
			if [ "$BUILDDIR" = "$i" ]; then
				CHECK=$TRUE
				break
			fi
		done

		# Test if there is a stamp on this component:
		STAMPFILE="${BUILDPATH}.stamp"
		if check_stamp; then
			STAMPED=$TRUE
		else
			STAMPED=$FALSE
		fi

		# Build:
		cd "$BUILDPATH_ROOT"
		LOGFILE="${BUILDPATH}.log"
		if [ $CHECK -eq $TRUE ] || [ $STAMPED -eq $FALSE ]; then
			rm -f "$LOGFILE"
			if ! ${BUILDDIR}_build; then
				fail "Build failed.\n"
				examine_logs
				if ask $COLOUR_ERROR 'yes' "Restart?"; then
					CODE_TERM='restart'
				else
					CODE_TERM='abort'
				fi
				return
			fi
		fi
		LOGFILE='/dev/null'
		set_stamp
	done
	success "Successfully built S²E-chef in %s.\n" "$BUILDPATH_ROOT"
	CODE_TERM='success'
}

# DOCKER =======================================================================

docker_build()
{
	if ! docker_image_exists "$DOCKER_IMAGE"; then
		die '%s: image not found' "$DOCKER_IMAGE"
	fi

	exec docker run \
		--rm \
		-t \
		-i \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH" \
		"$DOCKER_IMAGE" \
		"$DOCKER_HOSTPATH/$RUNDIR/$RUNNAME" \
			-b "$DOCKER_HOSTPATH/build/$ARCH-$TARGET-$MODE" \
			-c "$CHECKED" \
			$(test $FORCE -eq $TRUE && printf '%s' '-f') \
			-i "$IGNORED" \
			-j$JOBS \
			-l "$LLVM_BASE" \
			-q "$QEMU_FLAGS" \
			$(test $SILENT -eq $TRUE && printf '%s' '-s') \
			-z \
			"$ARCH:$TARGET:$MODE"
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] [[ARCH]:[TARGET]:[MODE]]
	EOF
}

help()
{
	usage

	cat <<- EOF

	Options:
	  -c COMPS   Force-\`make\` components COMPS
	             [default='$CHECKED']
	  -f         Force-rebuild
	  -h         Display this help
	  -i COMPS   Ignore components COMPS (has higher priority than -c)
	             [default='$IGNORED']
	  -j N       Compile with N jobs [default=$JOBS]
	  -l PATH    Path to where the native LLVM-3.2 files are installed
	             [default=$LLVM_BASE]
	  -q FLAGS   Additional flags passed to qemu's \`configure\` script
	  -s         Silent: redirect compilation messages/warnings/errors into log file
	  -y         Dry run: print build-related variables and exit
	  -z         Direct mode (build directly on machine, instead inside docker)

	Components:
	  $COMPONENTS

	Architectures:
	$(for arch in $ARCHS; do
		if [ "$arch" = "$DEFAULT_ARCH" ]; then
			printf '  [%s]' "$arch"
		else
			printf '  %s' "$arch"
		fi
	done)

	Targets:
	$(for target in $TARGETS; do
		if [ "$target" = "$DEFAULT_TARGET" ]; then
			printf '  [%s]' "$target"
		else
			printf '  %s' "$target"
		fi
	done)

	Modes:
	$(for mode in $MODES; do
		if [ "$mode" = "$DEFAULT_MODE" ]; then
			printf '  [%s]' "$mode"
		else
			printf '  %s' "$mode"
		fi
	done)
	EOF
}

get_options()
{
	# Default values:
	CHECKED="$COMPONENTS"
	DIRECT=$FALSE
	DRYRUN=$FALSE
	FORCE=$FALSE
	IGNORED='gmock tests'
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux) JOBS=$(grep -c '^processor' /proc/cpuinfo) ;;
		*) JOBS=2 ;;
	esac
	LLVM_BASE='/opt/s2e/llvm'
	QEMU_FLAGS=''
	SILENT=${CCLI_SILENT_BUILD:=$FALSE}

	# Options:
	while getopts c:fhi:j:l:q:syz opt; do
		case "$opt" in
			c) CHECKED="$OPTARG" ;;
			f) FORCE=$TRUE ;;
			h) help; exit 1 ;;
			i) IGNORED="$OPTARG" ;;
			j) JOBS="$OPTARG" ;;
			l) LLVM_BASE="$OPTARG" ;;
			q) QEMU_FLAGS="$OPTARG" ;;
			s) SILENT=$TRUE ;;
			y) DRYRUN=$TRUE ;;
			z) DIRECT=$TRUE ;;
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
	VERBOSE=$(! $(as_boolean $SILENT); echo $?)
}

get_release()
{
	RELEASE="$1"
	if [ -z "$RELEASE" ]; then
		ARGSHIFT=0
		RELEASE="$DEFAULT_RELEASE"
	else
		ARGSHIFT=1
	fi
	split_release $RELEASE    # sets ARCH, TARGET and MODE, exits on error
}

main()
{
	LOGFILE='./build.log'

	# Command line arguments:
	get_options "$@"
	shift $ARGSHIFT
	get_release "$@"
	shift $ARGSHIFT
	test $# -eq $TRUE || die_help "trailing arguments: $@"

	if [ $DRYRUN -eq $TRUE ]; then
		util_dryrun
		cat <<- EOF
		COMPONENTS='$COMPONENTS'
		BUILDPATH_ROOT=$BUILDPATH_ROOT
		CHECKED='$COMPONENTS'
		DIRECT=$(as_boolean $DIRECT)
		FORCE=$(as_boolean $FORCE)
		IGNORED='$IGNORED'
		JOBS=$JOBS
		LLVM_BASE=$LLVM_BASE
		QEMU_FLAGS='$QEMU_FLAGS'
		SILENT=$(as_boolean $SILENT) (CCLI_SILENT_BUILD=$(as_boolean $CCLI_SILENT_BUILD))
		LLVM_SRC=$LLVM_SRC
		LLVM_BUILD=$LLVM_BUILD
		LLVM_NATIVE=$LLVM_NATIVE
		LLVM_NATIVE_CC=$LLVM_NATIVE_CC
		LLVM_NATIVE_CXX=$LLVM_NATIVE_CXX
		LLVM_NATIVE_LIB=$LLVM_NATIVE_LIB
		EOF
		exit 1
	fi

	if [ $DIRECT -eq $TRUE ]; then
		info 'Building %s with %d cores' "$RELEASE" "$JOBS"
		CODE_TERM='restart'
		while [ "$CODE_TERM" = 'restart' ]; do
			# Build directly:
			test -d "$BUILDPATH_ROOT" || mkdir "$BUILDPATH_ROOT"
			cd "$BUILDPATH_ROOT"
			all_build
		done
		case "$CODE_TERM" in
			'success') exit 0 ;;
			'abort') exit 2 ;;
			*) die_internal 'main(): unknown termination code: %s' "$CODE_TERM" ;;
		esac
	else
		# Wrap build in docker:
		mkdir -p "$BUILDPATH_ROOT"
		setfacl -m user:$(id -u):rwx "$BUILDPATH_ROOT"
		setfacl -m user:431:rwx "$BUILDPATH_ROOT"
		setfacl -d -m user:$(id -u):rwx "$BUILDPATH_ROOT"
		setfacl -d -m user:431:rwx "$BUILDPATH_ROOT"
		docker_build
	fi
}

set -e
main "$@"
set +e
