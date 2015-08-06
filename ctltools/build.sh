#!/usr/bin/env sh
set -e

# This script builds S²E-Chef.
#
# Maintainers:
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

. "$(readlink -f "$(dirname "$0")")/utils.sh"

export C_INCLUDE_PATH='/usr/include:/usr/include/x86_64-linux-gnu'
export CPLUS_INCLUDE_PATH="$C_INCLUDE_PATH:/usr/include/x86_64-linux-gnu/c++/4.8"

COMPS_CHEF='lua stp klee qemu tools guest'
COMPS_Z3='z3'
COMPS_PROTOBUF='protobuf'
COMPS_LLVM='clang compiler-rt llvm-native llvm llvm'

# Z3 ===========================================================================

z3_url='http://download-codeplex.sec.s-msft.com/Download/SourceControlFileDownload.ashx?ProjectName=z3&changeSetId=dd62ca5eb36c2a62ee44fc5a79fc27c883de21ae'
z3_tarball='z3.zip'

z3_fetch()
{
	if [ -e "$z3_tarball" ]; then
		return $SKIPPED
	fi
	if ! wget -O "$z3_tarball" "$z3_url"; then
		rm -f "$z3_tarball"
		return $FAILURE
	fi
}

z3_extract()
{
	unzip -d "$BUILDPATH" "$z3_tarball" || return $FAILURE
}

z3_configure()
{
	python2 scripts/mk_make.py || return $FAILURE
}

z3_compile()
{
	make -j$JOBS -C build || return $FAILURE
}

# PROTOBUF =====================================================================

protobuf_fetch()
{
	protobuf_dirname="protobuf-2.6.0"
	protobuf_tarball="${protobuf_dirname}.tar.gz"
	protobuf_url="https://protobuf.googlecode.com/svn/rc/$protobuf_tarball"

	if [ -e "$protobuf_tarball" ]; then
		return $SKIPPED
	fi
	if ! wget -O "$protobuf_tarball" "$protobuf_url"; then
		rm -f "$protobuf_tarball"
		return $FAILURE
	fi
}

protobuf_extract()
{
	tar xvzf "$protobuf_tarball" || return $FAILURE
	mv "$protobuf_dirname" "$BUILDPATH" || return $FAILURE
}

protobuf_configure()
{
	./configure || return $FAILURE
}

# LLVM (GENERIC: CLANG, COMPILER-RT, LLVM) =====================================

llvm_generic_fetch()
{
	llvm_generic_prog="$1"
	llvm_generic_vprog="$llvm_generic_prog-$LLVM_VERSION"
	llvm_generic_srcdir="${llvm_generic_vprog}.src"
	llvm_generic_tarball="${llvm_generic_srcdir}.tar.gz"
	llvm_generic_urlbase="http://llvm.org/releases/$LLVM_VERSION"
	llvm_generic_url="$llvm_generic_urlbase/$llvm_generic_tarball"

	if [ -e "$llvm_generic_tarball" ]; then
		return $SKIPPED
	fi
	if ! wget -O "$llvm_generic_tarball" "$llvm_generic_url"; then
		rm -f "$llvm_generic_tarball"
		return $FAILURE
	fi
}

llvm_generic_extract()
{
	tar -xzf "$llvm_generic_tarball" || return $FAILURE
	if [ "$(readlink -f "$llvm_generic_srcdir")" != "$SRCPATH" ]; then
		mv "$llvm_generic_srcdir" "$SRCPATH" || return $FAILURE
	fi
}

llvm_generic_patch()
{
	case "$llvm_generic_prog" in
		llvm|clang) llvm_generic_patchname=memorytracer ;;
		compiler-rt) llvm_generic_patchname=asan4s2e ;;
		*) die_internal 'llvm_generic_patch(): invalid program: %s' \
		   "$llvm_generic_prog" ;;
	esac
	llvm_generic_patch="$llvm_generic_vprog-${llvm_generic_patchname}.patch"
	patch -d "$SRCPATH" -p0 -i "$CHEFROOT_SRC/llvm/$llvm_generic_patch" \
	|| return $FAILURE
}

# CLANG ========================================================================

clang_prepare()   { SRCPATH="$BUILDPATH"; }
clang_fetch()     { llvm_generic_fetch   clang || return $?; }
clang_extract()   { llvm_generic_extract clang || return $?; }
clang_configure() { llvm_generic_patch   clang || return $?; }
clang_compile()   { :; } # override generic_compile

# COMPILER-RT ==================================================================

compiler_rt_prepare()   { SRCPATH="$BUILDPATH"; }
compiler_rt_fetch()     { llvm_generic_fetch   compiler-rt || return $?; }
compiler_rt_extract()   { llvm_generic_extract compiler-rt || return $?; }
compiler_rt_configure() { llvm_generic_patch   compiler-rt || return $?; }
compiler_rt_compile()   { :; } # override generic_compile

# LLVM NATIVE ==================================================================

llvm_native_prepare()
{
	# This is a little tricky:
	# - Source in llvm-native.src
	# - Build in llvm-native.build
	# - Install in llvm-native
	SRCPATH="${LLVM_NATIVE}.src"
	BUILDPATH="${LLVM_NATIVE}.build"
	INSTALLPATH="$LLVM_NATIVE"
	echo
	echo "LLVM native:"
	echo "  SRCPATH=$SRCPATH"
	echo "  BUILDPATH=$BUILDPATH"
	echo "  INSTALLPATH=$INSTALLPATH"
	echo
}

llvm_native_fetch()   { llvm_generic_fetch   llvm || return $?; }

llvm_native_extract()
{
	llvm_generic_extract llvm || return $FAILURE
	mkdir "$BUILDPATH" || return $FAILURE
}

llvm_native_configure()
{
	llvm_generic_patch || return $FAILURE
	cp -r "$BUILDPATH_BASE/clang" "$SRCPATH/tools/clang"
	cp -r "$BUILDPATH_BASE/compiler-rt" "$SRCPATH/projects/compiler-rt"
	"$SRCPATH"/configure \
		--prefix="$INSTALLPATH" \
		--enable-jit \
		--enable-optimized \
		--disable-assertions \
	|| return $FAILURE
}

llvm_native_compile() {
	make ENABLE_OPTIMIZED=1 -j$JOBS || return $FAILURE
}

llvm_native_install() {
	make install || return $FAILURE
}

# LLVM =========================================================================

llvm_prepare()
{
	# This is a little less tricky than llvm-native:
	# - Source in llvm-3.2.src
	# - Build in llvm-3.2.build
	SRCPATH="$LLVM_SRC"
	BUILDPATH="$LLVM_BUILD"
	echo
	echo "LLVM:"
	echo "  SRCPATH=$SRCPATH"
	echo "  BUILDPATH=$BUILDPATH"
	echo "  INSTALLPATH=$INSTALLPATH"
	echo
}

llvm_fetch()   { llvm_generic_fetch   llvm || return $?; }
llvm_extract()
{
	llvm_generic_extract llvm || return $FAILURE
	mkdir -p "$BUILDPATH" || return $FAILURE
}

llvm_configure()
{
	case "$TARGET" in
		release) llvm_configure_options='--enable-optimized' ;;
		debug) llvm_configure_options='--disable-optimized' ;;
		*) die_internal 'llvm_configure(): invalid target: %s' "$TARGET"
	esac
	"$SRCPATH"/configure \
		--enable-jit \
		--target=x86_64 \
		--enable-targets=x86 \
		$llvm_configure_options \
		CC="$LLVM_NATIVE_CC" \
		CXX="$LLVM_NATIVE_CXX" \
	|| return $FAILURE
}

llvm_compile()
{
	case "$TARGET" in
		release) llvm_make_options='ENABLE_OPTIMIZED=1' ;;
		debug) llvm_make_options='ENABLE_OPTIMIZED=0' ;;
		*) die_internal 'llvm_compile(): invalid target: %s' "$TARGET"
	esac
	make $llvm_make_options REQUIRES_RTTI=1 -j$JOBS || return $FAILURE
}

# LUA ==========================================================================

lua_dir="lua-5.1"
lua_tarball="${lua_dir}.tar.gz"
lua_url="http://www.lua.org/ftp/$lua_tarball"

lua_fetch()
{
	if [ -e "$lua_tarball" ]; then
		return $SKIPPED
	fi
	if ! wget -O "$lua_tarball" "$lua_url"; then
		rm -f "$lua_tarball"
		return $FAILURE
	fi
}

lua_extract()
{
	tar xvzf "$lua_tarball" || return $FAILURE
	mv "$lua_dir" "$BUILDPATH" || return $FAILURE
}

lua_compile()
{
	make -j$JOBS linux || return $FAILURE
}

# STP ==========================================================================

stp_extract()
{
	cp -r "$SRCPATH" "$BUILDPATH" || return $FAILURE
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
		--prefix="$BUILDPATH_BASE/opt" \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--target=x86_64 \
		--enable-exceptions \
		--with-stp="$BUILDPATH_BASE/stp" \
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
		--with-klee="$BUILDPATH_BASE/klee/$ASSERTS" \
		--with-llvm="$LLVM_BUILD/$ASSERTS" \
		$(test "$TARGET" = 'debug' && echo '--enable-debug') \
		--prefix="$BUILDPATH_BASE/opt" \
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
		--with-stp="$BUILDPATH_BASE/stp" \
		$(test "$MODE" = 'asan' && printf '%s' '--enable-address-sanitizer') \
		$QEMU_FLAGS \
	|| return $FAILURE
}

# TOOLS ========================================================================

tools_configure()
{
	"$SRCPATH"/configure \
		--with-llvmsrc="$LLVM_SRC" \
		--with-llvmobj="$LLVM_BUILD" \
		--with-s2esrc="$CHEFROOT_SRC/qemu" \
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
		*) die_internal 'guest_compile(): unknown architecture: %s' "$ARCH"
	esac
	make -j$JOBS CFLAGS="$guest_cflags" || return $FAILURE
}

# ALL/GENERIC ==================================================================

generic_extract()
{
	mkdir -p "$BUILDPATH" || return $FAILURE
}

generic_compile()
{
	make -j$JOBS || return $FAILURE
}

all_build()
{
	llvm_seen=$FALSE
	for component in $COMPS
	do
		BUILDPATH="$BUILDPATH_BASE/$component"
		SRCPATH="$CHEFROOT_SRC/$component"
		LOGFILE="${BUILDPATH}.log"
		rm -f "$LOGFILE"
		cd "$BUILDPATH_BASE"

		# XXX LLVM Hack (is run twice):
		if [ "$component" = 'llvm' ]; then
			if [ $llvm_seen -eq $FALSE ]; then
				# first encounter with LLVM:
				llvm_seen=$TRUE
				TARGET=release
			else
				# second encounter with LLVM:
				TARGET=debug
			fi
			parse_release "$ARCH:$TARGET:$MODE" # sets LLVM_BUILD
		fi

		# Prepare:
		prepare_handler="$(funcify "$component")_prepare"
		if is_command "$prepare_handler"; then
			if ! track "preparing $component" "$prepare_handler"; then
				examine_logs
				return $FAILURE
			fi
		fi

		# Exclude/force-build component?
		if list_contains "$COMPS_FORCE" "$component"; then
			info 'force-building %s' "$component"
			rm -rf "$BUILDPATH"
		elif list_contains "$COMPS_EXCLUDE" "$component"; then
			skip '%s: excluded' "$component"
			continue
		fi

		# Build:
		requires_configure=$FALSE
		for action in fetch extract configure compile install
		do
			# if at *any* point there's no build path, we'll need to configure:
			if [ ! -d "$BUILDPATH" ]; then
				requires_configure=$TRUE
			fi

			# determine whether to skip:
			if [ "$action" = 'extract' ] && [ -d "$BUILDPATH" ] \
			|| [ "$action" = 'configure' ] && [ $requires_configure -ne $TRUE ]; then
				skip '%s %s' "$(lang_continuous $action)" "$component"
				continue
			fi

			# CWD:
			case "$action" in
				fetch|extract) cd "$BUILDPATH_BASE" ;;
				configure|compile|install) cd "$BUILDPATH" ;;
				*) die_internal 'Unknown action: %s' "$action"
			esac

			handler="$(funcify "$component")_$action"
			is_command "$handler" || handler="generic_$action"
			is_command "$handler" || continue   # if default action == nothing
			if ! track "$(lang_continuous "$action") $component" \
				"$handler"
			then
				examine_logs
				return $FAILURE
			fi
		done

		LOGFILE="$NULL"
	done
	success "Build complete in %s.\n" "$BUILDPATH_BASE"
	return $SUCCESS
}

# MAIN =========================================================================

usage() { echo "Usage: $INVOKENAME [OPTIONS ...] [[ARCH]:[TARGET]:[MODE]]"; }
help()
{
	usage
	cat <<- EOF

	Options:
	  -p PROC    Change procedure (see below for more information)
	  -f COMPS   Force-rebuild components COMPS from scratch
	             [default='$COMPS_FORCE']
	  -x COMPS   Exclude components COMPS (-f overrides this)
	             [default='$COMPS_EXCLUDE']
	  -c COMPS   Only build components COMPS (instead of all)
	  -j N       Compile with N jobs [default=$JOBS]
	  -q FLAGS   Additional flags passed to qemu's \`configure\` script
	  -s         Silent: redirect compilation messages/warnings/errors into log files
	  -l         List existing builds and exit
	  -y         Dry run: print build-related variables and exit
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

	Procedures:
	  [chef]  z3  protobuf  llvm
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

list_builds()
{
	for build in $(find "$CHEFROOT_BUILD" -maxdepth 1 -mindepth 1 -type d); do
		basename "$build" | sed 's/-/:/g'
	done
	exit 1
}

dry_run()
{
	util_dryrun
	cat <<- EOF
	COMPS='$COMPS'
	COMPS_FORCE='$COMPS_FORCE'
	COMPS_EXCLUDE='$COMPS_EXCLUDE'
	JOBS=$JOBS
	LIST=$LIST
	PROCEDURE=$PROCEDURE
	QEMU_FLAGS='$QEMU_FLAGS'
	EOF
	exit 1
}

get_options()
{
	DRYRUN=$FALSE
	LIST=$FALSE
	VERBOSE=$TRUE  # override utils.sh
	PROCEDURE=chef
	COMPS_FORCE=''
	COMPS_EXCLUDE=''
	JOBS=$CPU_CORES
	QEMU_FLAGS=''

	while getopts :p:f:x:c:j:q:slyh opt; do
		case "$opt" in
			p) PROCEDURE="$OPTARG" ;;
			f) COMPS_FORCE="$OPTARG" ;;
			x) COMPS_EXCLUDE="$OPTARG" ;;
			c) COMPS="$OPTARG" ;;
			j) JOBS="$OPTARG" ;;
			q) QEMU_FLAGS="$OPTARG" ;;
			s) VERBOSE=$FALSE ;;
			l) LIST=$TRUE ;;
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
	parse_release "$1"
}

main()
{
	if [ ! -d "$CHEFROOT_BUILD" ]; then
		if ! track "initialise build directory: $CHEFROOT_BUILD" \
			mkdir "$CHEFROOT_BUILD"
		then
			exit 1
		fi
	fi
	LOGFILE="$CHEFROOT_BUILD/build.log"

	# Command line arguments:
	get_options "$@"
	shift $ARGSHIFT
	get_release "$@"
	shift $ARGSHIFT
	test $# -eq 0 || die_help "trailing arguments: $@"

	# Procedure:
	case "$PROCEDURE" in
		chef|default|'')
			COMPS="$COMPS_CHEF"
			BUILDPATH_BASE="$RELEASEPATH" ;;
		llvm)
			COMPS="$COMPS_LLVM"
			BUILDPATH_BASE="$LLVM_BASE" ;;
		z3)
			COMPS="$COMPS_Z3"
			BUILDPATH_BASE="$CHEFROOT_BUILD_DEPS" ;;
		protobuf)
			COMPS="$COMPS_PROTOBUF"
			BUILDPATH_BASE="$CHEFROOT_BUILD_DEPS" ;;
		*) die_help 'invalid procedure: %s' "$PROCEDURE" ;;
	esac
	test -d "$CHEFROOT_BUILD" || mkdir "$CHEFROOT_BUILD"

	# Forced/excluded components:
	test "$COMPS_FORCE" != 'all' || COMPS_FORCE="$COMPS"
	test "$COMPS_EXCLUDE" != 'all' || COMPS_EXCLUDE="$COMPS"    # uhm...

	# Special action exit:
	test $DRYRUN -eq $FALSE || dryrun
	test $LIST -eq $FALSE || list_builds

	mkdir -p "$BUILDPATH_BASE" || die 1 'Permission denied'
	info 'building to %s (jobs: %d)' "$BUILDPATH_BASE" $JOBS
	all_build
}
main "$@"
