#!/usr/bin/env sh
#
# This script builds a user-defined configuration of S²E-chef inside a prepared
# docker container.
#
# Maintainer: Tinu Weber <martin.weber@epfl.ch>

. "$(readlink -f "$(dirname "$0")")/utils.sh"

export C_INCLUDE_PATH='/usr/include:/usr/include/x86_64-linux-gnu'
export CPLUS_INCLUDE_PATH="$C_INCLUDE_PATH:/usr/include/x86_64-linux-gnu/c++/4.8"
COMPS='lua stp klee qemu tools guest'
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
		wget "$lua_baseurl/$lua_tarball" || return $FAILURE
	fi
	tar xzf "$lua_tarball"
	mv "$(basename "$lua_tarball" .tar.gz)" "$BUILDPATH"
}

lua_compile()
{
	make -j$JOBS linux || return $FAILURE
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
	|| return $FAILURE
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
	|| return $FAILURE
}

klee_compile()
{
	if [ "$TARGET" = 'debug' ]; then
		klee_buildopts='ENABLE_OPTIMIZED=0'
	else
		klee_buildopts='ENABLE_OPTIMIZED=1'
	fi
	make -j$JOBS $klee_buildopts || return $FAILURE
}

# QEMU =========================================================================

qemu_configure()
{
	"$SRCPATH"/configure \
		--with-klee="$BUILDPATH_ROOT/klee/$ASSERTS" \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		$(test "$TARGET" = 'debug' && echo '--enable-debug') \
		--prefix="$BUILDPATH_ROOT/opt" \
		--cc="$LLVM_NATIVE_CC" \
		--cxx="$LLVM_NATIVE_CXX" \
		--target-list="$ARCH-s2e-softmmu,$ARCH-softmmu" \
		--enable-llvm \
		--enable-s2e \
		--with-pkgversion=S2E \
		--extra-cflags=-mno-sse3 \
		--extra-cxxflags=-mno-sse3 \
		--disable-virtfs \
		--disable-fdt \
		--with-stp="$BUILDPATH_ROOT/stp" \
		$(test "$MODE" = 'asan' && printf '%s' '--enable-address-sanitizer') \
		$QEMU_FLAGS \
	|| return $FAILURE
}

qemu_install()
{
	make install || return $FAILURE
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
	|| return $FAILURE
}

# GUEST TOOLS ==================================================================

guest_configure()
{
	"$SRCPATH"/configure || return $FAILURE
}

guest_compile()
{
	case "$ARCH" in
		i386) guest_cflags='-m32' ;;
		x86_64) guest_cflags='-m64' ;;
	esac
	make -j$JOBS CFLAGS="$guest_cflags" || return $FAILURE
}

# ALL ==========================================================================

generic_prepare()
{
	mkdir "$BUILDPATH"
}

generic_compile()
{
	make -j$JOBS || return $FAILURE
}

all_build()
{
	for component in $COMPS
	do
		BUILDPATH="$BUILDPATH_ROOT/$component"
		SRCPATH="$SRCPATH_ROOT/$component"
		LOGFILE="${BUILDPATH}.log"
		rm -f "$LOGFILE"

		# Exclude/force-build component?
		if list_contains "$COMPS_FORCE" "$component"; then
			info 'force-building %s' "$component"
			rm -rf "$BUILDPATH"
		elif list_contains "$COMPS_EXCLUDE" "$component"; then
			skip '%s: excluded' "$component"
			continue
		fi

		# Build:
		for action in prepare configure compile install
		do
			# action-specific:
			case $action in
				prepare)
					configure=$FALSE
					test ! -d "$BUILDPATH" || continue
					configure=$TRUE ;;
				configure)
					test $configure -eq $TRUE || continue ;;
			esac
			if [ $action = prepare ]; then
				cd "$BUILDPATH_ROOT"
			else
				cd "$BUILDPATH"
			fi

			# default action:
			handler=${component}_$action
			is_command $handler || handler=generic_$action
			is_command $handler || continue   # if default action == nothing

			# action:
			if ! track "$(lang_continuous "$action") $component" $handler; then
				examine_logs
				if ask "$ESC_ERROR" 'yes' 'Restart?'; then
					CODE_TERM='restart'
				else
					CODE_TERM='abort'
				fi
				if ! case $action in (prepare|configure) false ;; esac; then
					if ! track 'cleaning up' rm -rf "$BUILDPATH"; then
						echo "Oh dear... you've got some issues"
					fi
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

	exec docker run --rm -it \
		-v "$SRCPATH_ROOT":"$DOCKER_HOSTPATH_IN" \
		-v "$BUILDPATH_ROOT":"$DOCKER_HOSTPATH_OUT" \
		"$DOCKER_IMAGE" \
		"$DOCKER_HOSTPATH_IN/$RUNDIR/$RUNNAME" \
			-i "$DOCKER_HOSTPATH_IN" \
			-o "$DOCKER_HOSTPATH_OUT" \
			-f "$COMPS_FORCE" \
			-x "$COMPS_EXCLUDE" \
			-j $JOBS \
			-L "$LLVM_BASE" \
			-q "$QEMU_FLAGS" \
			$(test $VERBOSE -eq $FALSE && printf '%s' '-s') \
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
	  -i PATH    Path to the build input directory (Chef source root)
	             [default=$SRCPATH_ROOT]
	  -o PATH    Path to the build output directory
	             [default=$BUILDPATH_ROOT]
	  -f COMPS   Force-rebuild components COMPS from scratch
	             [default='$COMPS_FORCE']
	  -x COMPS   Exclude components COMPS (-f overrides this)
	             [default='$COMPS_EXCLUDE']
	  -j N       Compile with N jobs [default=$JOBS]
	  -L PATH    Path to where the LLVM-3.2 files are installed
	             [default=$LLVM_BASE]
	  -q FLAGS   Additional flags passed to qemu's \`configure\` script
	  -s         Silent: redirect compilation messages/warnings/errors into log file

	  -y         Dry run: print build-related variables and exit
	  -l         List existing builds and exit
	  -h         Display this help and exit

	Components:
	  $COMPS
	  You may specify 'all'

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
	for build in $(find "$BUILDPATH_ROOT" -maxdepth 1 -mindepth 1 -type d); do
		basename "$build" | sed 's/-/:/g'
	done
}

dry_run()
{
	util_dryrun
	cat <<- EOF
	BUILDPATH_ROOT=$BUILDPATH_ROOT
	COMPS='$COMPS'
	COMPS_FORCE='$COMPS_FORCE'
	COMPS_EXCLUDE='$COMPS_EXCLUDE'
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
	COMPS_FORCE=''
	COMPS_EXCLUDE=''
	JOBS=$CPU_CORES
	LIST=$FALSE
	LLVM_BASE='/opt/s2e/llvm'
	QEMU_FLAGS=''
	#VERBOSE=${CHEF_VERBOSE:-$DEFAULT_VERBOSE}
	VERBOSE=${CHEF_VERBOSE:-$TRUE}

	# Options:
	while getopts :df:hi:j:lL:o:q:sx:y opt; do
		case "$opt" in
			d) DOCKERIZED=$TRUE ;;
			i) SRCPATH_ROOT="$OPTARG" ;;
			o) BUILDPATH_ROOT="$(readlink -f "$OPTARG")" ;;
			f)
				COMPS_FORCE="$OPTARG"
				test "$COMPS_FORCE" != 'all' || COMPS_FORCE="$COMPS"
				;;
			x)
				COMPS_EXCLUDE="$OPTARG"
				test "$COMPS_EXCLUDE" != 'all' || COMPS_EXCLUDE="$COMPS"
				;;
			j) JOBS="$OPTARG" ;;
			l) LIST=$TRUE ;;
			L)
				LLVM_BASE="$OPTARG"
				LLVM_SRC="$LLVM_BASE/llvm-3.2.src"
				LLVM_BUILD="$LLVM_BASE/llvm-3.2.build"
				LLVM_NATIVE="$LLVM_BASE/llvm-3.2-native"
				LLVM_NATIVE_CC="$LLVM_NATIVE/bin/clang"
				LLVM_NATIVE_CXX="$LLVM_NATIVE/bin/clang++"
				LLVM_NATIVE_LIB="$LLVM_NATIVE/lib"
				;;
			q) QEMU_FLAGS="$OPTARG" ;;
			s) VERBOSE=$FALSE ;;
			y) DRYRUN=$TRUE ;;
			h) help; exit 1 ;;
			'?') die_help 'Invalid option: -%s' "$OPTARG";;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
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
	test $# -eq 0 || die_help "trailing arguments: $@"

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
