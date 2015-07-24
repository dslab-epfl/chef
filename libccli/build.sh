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
DOCKER_HOSTPATH_IN='/host-in'
DOCKER_HOSTPATH_OUT='/host-out'

# LUA ==========================================================================

lua_prepare()
{
	lua_name='lua'
	lua_version='5.1'
	lua_vname="$lua_name-$lua_version"
	lua_baseurl='http://www.lua.org/ftp'
	lua_tarball="${lua_vname}.tar.gz"

	if [ ! -e "$lua_tarball" ]; then
		wget "$lua_baseurl/$lua_tarball" || return $FALSE
	fi
	tar xzf "$lua_tarball"
	mv "$(basename "$lua_tarball" .tar.gz)" "$BUILDPATH"
}

lua_build()
{
	make -j$JOBS linux || return $FALSE
}

# STP ==========================================================================
# STP does not seem to allow building outside the source directory, so as not to
# pollute stuff, we need to copy the entire thing.
# Yay!

stp_prepare()
{
	cp -r "$SRCPATH" "$BUILDPATH"
}

stp_configure()
{
	scripts/configure \
		--with-prefix="$BUILDPATH" \
		--with-fpic \
		--with-gcc="$LLVM_NATIVE_CC" \
		--with-g++="$LLVM_NATIVE_CXX" \
		$(test "$MODE" = 'asan' && echo '--with-address-sanitizer') \
	|| return $FALSE
}

stp_build()
{
	make -j$JOBS || return $FALSE
}

# KLEE =========================================================================

klee_configure()
{
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
	"$SRCPATH"/configure \
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
}

klee_build()
{
	if [ "$TARGET" = 'debug' ]; then
		klee_buildopts='ENABLE_OPTIMIZED=0'
	else
		klee_buildopts='ENABLE_OPTIMIZED=1'
	fi
	make -j$JOBS $klee_buildopts || return $FALSE
}

# LIBMEMTRACER =================================================================

libmemtracer_configure()
{
	"$SRCPATH"/configure \
		--enable-debug \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
	|| return $FALSE
}

libmemtracer_build()
{
	libmemtracer_buildopts="CLANG_CC=$LLVM_NATIVE_CC CLANG_CXX=$LLVM_NATIVE_CXX"
	make -j$JOBS $libmemtracer_buildopts || return $FALSE
}

# LIBVMI =======================================================================

libvmi_configure()
{
	"$SRCPATH"/configure \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		--with-libmemtracer-src="$SRCPATH_ROOT/libmemtracer" \
		$(test "$TARGET" = 'debug' && echo '--enable-debug') \
		CC=$LLVM_NATIVE_CC \
		CXX=$LLVM_NATIVE_CXX \
	|| return $FALSE
}

libvmi_build()
{
	make -j$JOBS || return $FALSE
}

# QEMU =========================================================================

qemu_configure()
{
	case "$MODE" in
		asan) qemu_confopt='--enable-address-sanitizer' ;;
		libmemtracer) qemu_confopt='--enable-memory-tracer' ;;
		*) ;;
	esac
	"$SRCPATH"/configure \
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
}

qemu_build()
{
	make -j$JOBS || return $FALSE
}

qemu_install()
{
	make install || return $FALSE
	cp "$ARCH-s2e-softmmu/op_helper.bc" \
		"$BUILDPATH_ROOT/opt/share/qemu/op_helper.bc.$ARCH"
	cp "$ARCH-softmmu/qemu-system-$ARCH" \
		"$BUILDPATH_ROOT/opt/bin/qemu-system-$ARCH"
	cp "$ARCH-s2e-softmmu/qemu-system-$ARCH" \
		"$BUILDPATH_ROOT/opt/bin/qemu-system-$ARCH-s2e"
}

# TOOLS ========================================================================

tools_configure()
{
	"$SRCPATH"/configure \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--with-s2esrc="$SRCPATH_ROOT/qemu" \
		--target=x86_64 \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
		ENABLE_OPTIMIZED=$(test "$TARGET" = 'release' && echo 1 || echo 0) \
		REQUIRES_RTTI=1 \
	|| return $FALSE
}

tools_build()
{
	make -j$JOBS || return $FALSE
}

# GUEST TOOLS ==================================================================

guest_configure()
{
	"$SRCPATH"/configure || return $FALSE
}

guest_build()
{
	case "$ARCH" in
		i386) guest_cflags='-m32' ;;
		x86_64) guest_cflags='-m64' ;;
	esac
	make -j$JOBS CFLAGS="$guest_cflags" || return $FALSE
}

# GMOCK ========================================================================

gmock_prepare()
{
	gmock_name='gmock'
	gmock_version='1.6.0'
	gmock_vname="$gmock_name-$gmock_version"
	gmock_baseurl='http://googlemock.googlecode.com/files'
	gmock_zip="${gmock_vname}.zip"
	gmock_dir="$(basename "$gmock_zip" .zip)"

	if [ ! -e "$gmock_zip" ]; then
		wget "$gmock_baseurl/$gmock_zip" || return $FALSE
	fi
	unzip -q "$gmock_zip"
	mv "$gmock_dir" "$BUILDPATH"
}

gmock_configure()
{
	./configure \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
	|| return $FALSE
}

gmock_build()
{
	gtest_path="$LLVM_SRC/utils/unittest/googletest"

	make -j$JOBS || return $FALSE
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
		-c "$BUILDPATH/src/gmock-all.cc" \
	|| return $FALSE
	ar -rv libgmock.a gmock-all.o || return $FALSE
}

# TEST SUITE ===================================================================

test_configure()
{
	"$SRCPATH"/configure \
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
}

tests_build()
{
	tests_isopt=$(test "$TARGET" = 'debug' && echo 0 || echo 1)
	tests_buildopts="REQUIRES_RTTI=1 REQUIRE_EH=1 ENABLE_OPTIMIZED=$tests_isopt"
	make -j$JOBS $tests_buildopts || return $FALSE
}

# ALL ==========================================================================

all_build()
{
	for component in $COMPONENTS
	do
		BUILDPATH="$BUILDPATH_ROOT/$component"
		SRCPATH="$SRCPATH_ROOT/$component"
		FORCE=$FALSE

		# Exclude component?
		excluded=$FALSE
		for i in $EXCLUDED; do
			if [ "$i" = "$component" ]; then
				skip '%s: excluded' "$component"
				excluded=$TRUE
				break
			fi
		done
		test $excluded -eq $FALSE || continue

		# Force-rebuild component?
		for c in $FORCE_COMPS; do
			if [ "$c" = "$component" ]; then
				info 'force-building %s' "$component"
				rm -r "$BUILDPATH"
				break
			fi
		done

		# Log file:
		LOGFILE="${BUILDPATH}.log"
		rm -f "$LOGFILE"

		# Build:
		for action in prepare configure build install
		do
			msg="${action}ing"

			# action-specific:
			case "$action" in
				prepare)
					msg='preparing'
					test ! -d "$BUILDPATH" || continue
					FORCE=$TRUE ;;
				configure)
					msg='configuring'
					test $FORCE -eq $TRUE || continue ;;
			esac
			if [ "$action" = prepare ]; then
				cd "$BUILDPATH_ROOT"
			else
				cd "$BUILDPATH"
			fi

			# default action:
			if ! is_command "${component}_${action}"; then
				if [ "$action" = prepare ]; then
					mkdir "$BUILDPATH"
				fi
				continue
			fi

			# action:
			if ! track "$msg $component" "${component}_${action}"; then
				examine_logs
				if ask $COLOUR_ERROR 'yes' 'Restart?'; then
					CODE_TERM='restart'
				else
					CODE_TERM='abort'
				fi
				return
			fi
		done

		LOGFILE="$NULL"
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
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH_IN" \
		-v "$BUILDPATH_ROOT":"$DOCKER_HOSTPATH_OUT" \
		"$DOCKER_IMAGE" \
		"$DOCKER_HOSTPATH_IN/$RUNDIR/$RUNNAME" \
			-f "$FORCE_COMPS" \
			-i "$DOCKER_HOSTPATH_IN" \
			-j $JOBS \
			-L "$LLVM_BASE" \
			-o "$DOCKER_HOSTPATH_OUT" \
			-q "$QEMU_FLAGS" \
			$(test $VERBOSE -eq $FALSE && printf '%s' '-s') \
			-x "$EXCLUDED" \
			"$RELEASE"
}

# MAIN =========================================================================

usage()
{
	cat <<- EOF
	Usage: $INVOKENAME [OPTIONS ...] [[ARCH]:[TARGET]:[MODE]]
	EOF
}

help_list_with_default()
{
	default="$1"
	shift
	for e in "$@"; do
		if [ "$e" = "$default" ]; then
			printf '  [%s]' "$e"
		else
			printf '  %s' "$e"
		fi
	done
}

help()
{
	usage

	cat <<- EOF

	Options:
	  -d         Dockerized (wrap build process inside docker container)
	  -f COMPS   Force-rebuild (configure+make) components COMPS
	  -i PATH    Path to the Chef source directory root
	             [default=$SRCPATH_ROOT]
	  -h         Display this help and exit
	  -j N       Compile with N jobs [default=$JOBS]
	  -l         List existing builds and exit
	  -L PATH    Path to where the LLVM-3.2 files are installed
	             [default=$LLVM_BASE]
	  -o PATH    Path to the build output directory
	             [default=$BUILDPATH_ROOT]
	  -q FLAGS   Additional flags passed to qemu's \`configure\` script
	  -s         Silent: redirect compilation messages/warnings/errors into log file
	  -x COMPS   Exclude components COMPS (has higher priority than -f)
	             [default='$EXCLUDED']
	  -y         Dry run: print build-related variables and exit

	Components:
	  $COMPONENTS

	Architectures:
	$(help_list_with_default "$DEFAULT_ARCH" $ARCHS)

	Targets:
	$(help_list_with_default "$DEFAULT_TARGET" $TARGETS)

	Modes:
	$(help_list_with_default "$DEFAULT_MODE" $MODES)
	EOF
}

list()
{
	for build in "$BUILDPATH_ROOT"/*; do
		echo "$(basename "$build")" | sed 's/-/:/g'
	done
}

dry_run()
{
	util_dryrun
	cat <<- EOF
	COMPONENTS='$COMPONENTS'
	BUILDPATH_ROOT=$BUILDPATH_ROOT
	FORCE_COMPS='$FORCE_COMPS'
	EXCLUDED='$EXCLUDED'
	JOBS=$JOBS
	LLVM_BASE=$LLVM_BASE
	QEMU_FLAGS='$QEMU_FLAGS'
	LLVM_SRC=$LLVM_SRC
	LLVM_BUILD=$LLVM_BUILD
	LLVM_NATIVE=$LLVM_NATIVE
	LLVM_NATIVE_CC=$LLVM_NATIVE_CC
	LLVM_NATIVE_CXX=$LLVM_NATIVE_CXX
	LLVM_NATIVE_LIB=$LLVM_NATIVE_LIB
	EOF
}

get_options()
{
	# Default values:
	#BUILDPATH_ROOT set in utils.sh
	#SRCPATH_ROOT set in utils.sh
	DOCKERIZED=$DEFAULT_DOCKERIZED
	DRYRUN=$FALSE
	FORCE_COMPS=''
	EXCLUDED='gmock tests'
	case "$(uname)" in
		Darwin) JOBS=$(sysctl hw.ncpu | cut -d ':' -f 2); alias cp=gcp ;;
		Linux) JOBS=$(grep -c '^processor' /proc/cpuinfo) ;;
		*) JOBS=1 ;;
	esac
	LIST=$FALSE
	LLVM_BASE='/opt/s2e/llvm'
	QEMU_FLAGS=''
	#VERBOSE=${CCLI_VERBOSE:-$DEFAULT_VERBOSE}
	VERBOSE=${CCLI_VERBOSE:-$TRUE}

	# Options:
	while getopts :df:hi:j:lL:o:q:sx:y opt; do
		case "$opt" in
			d) DOCKERIZED=$TRUE ;;
			f) FORCE_COMPS="$OPTARG" ;;
			h) help; exit 1 ;;
			i) SRCPATH_ROOT="$OPTARG" ;;
			j) JOBS="$OPTARG" ;;
			l) LIST=$TRUE ;;
			L) LLVM_BASE="$OPTARG" ;;
			o) BUILDPATH_ROOT="$OPTARG" ;;
			q) QEMU_FLAGS="$OPTARG" ;;
			s) VERBOSE=$FALSE ;;
			x) EXCLUDED="$OPTARG" ;;
			y) DRYRUN=$TRUE ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))

	BUILDPATH_ROOT="$(readlink -f "$BUILDPATH_ROOT")"
	LLVM_SRC="$LLVM_BASE/llvm-3.2.src"
	LLVM_BUILD="$LLVM_BASE/llvm-3.2.build"
	LLVM_NATIVE="$LLVM_BASE/llvm-3.2-native"
	LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
	LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"
	LLVM_NATIVE_LIB="$LLVM_NATIVE/lib"
}

get_release()
{
	if [ -z "$1" ]; then
		ARGSHIFT=0
	else
		ARGSHIFT=1
	fi
	split_release "$1"  # sets RELEASE, ARCH, TARGET and MODE
	case "$TARGET" in
		release) ASSERTS='Release+Asserts' ;;
		debug) ASSERTS='Debug+Asserts' ;;
		*) die_internal 'get_release(): Unknown target %s' "$TARGET" ;;
	esac
}

main()
{
	LOGFILE='./build.log'

	# Command line arguments:
	get_options "$@"
	shift $ARGSHIFT
	if [ $LIST -eq $TRUE ]; then
		list
		exit 1
	fi
	get_release "$@"
	shift $ARGSHIFT
	test $# -eq $TRUE || die_help "trailing arguments: $@"

	if [ $DRYRUN -eq $TRUE ]; then
		dry_run
		exit 1
	fi

	test -d "$BUILDPATH_ROOT" || mkdir "$BUILDPATH_ROOT"

	if [ $DOCKERIZED -eq $TRUE ]; then
		setfacl -m user:$(id -u):rwx "$BUILDPATH_ROOT"
		setfacl -m user:431:rwx "$BUILDPATH_ROOT"
		setfacl -d -m user:$(id -u):rwx "$BUILDPATH_ROOT"
		setfacl -d -m user:431:rwx "$BUILDPATH_ROOT"
		docker_build
	else
		info 'Building %s (jobs=%d)' "$RELEASE" "$JOBS"
		BUILDPATH_ROOT="$BUILDPATH_ROOT/$ARCH-$TARGET-$MODE"
		CODE_TERM='restart'
		while [ "$CODE_TERM" = 'restart' ]; do
			if ! mkdir -p "$BUILDPATH_ROOT"; then
				die 1 'Permission denied'
			fi
			all_build
		done
		case "$CODE_TERM" in
			'success') exit 0 ;;
			'abort') exit 2 ;;
			*) die_internal 'main(): unknown termination code: %s' "$CODE_TERM" ;;
		esac
	fi
}

set -e
main "$@"
set +e
