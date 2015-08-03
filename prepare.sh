#!/usr/bin/env sh

# This script prepares the environment to start building/using Chef.
#
# Maintainers
#   ayekat (Tinu Weber <martin.weber@epfl.ch>)

SRCROOT="$(readlink -f "$(dirname "$0")")"
. "$SRCROOT/libccli/utils.sh"

# INTERACTION ==================================================================

die_unsupported()
{
	cat >&2 <<- EOF
	$OS_NAME is currently unsupported. However the addition of distribution
	support is usually simple, and we may have just forgotten your distribution.
	Please open a feature request at https://github.com/dslab-epfl/chef/issues/,
	and we will happily add support for your operating system.
	EOF
	exit 255
}

note_os()
{
	if [ "$OS_NAME" = 'Ubuntu' ] && [ "$OS_VERSION" = '14.04' ]; then
		cat <<- EOF
		Since you are running $OS_NAME $OS_VERSION, it is possible to build and
		run Chef natively on your machine, without docker. However, this comes
		with the disadvantage of needing to install more packages on your system
		in order to fullfil the Chef dependencies, and you will need to
		additionally compile LLVM, which will take a considerable amount of
		time.

		It is thus recommended to install docker.
		EOF
	else
		OS_STRING="${OS_NAME}$(test "$OS_NAME" = 'Ubuntu'&&echo " $OS_VERSION")"
		cat <<- EOF
		Since you are running $OS_STRING, it is very unlikely that Chef will run
		on your system without using docker; the installation of docker is thus
		necessary to continue the preparation.
		EOF
	fi
}

note_ccli()
{
	cat <<- EOF
	Chef consists of various parts that are controlled through a common command
	line interface. Parts of it are written in python, which requires some
	modules that must be installed in order for it to work properly.
	EOF
	echo
	ask "$ESC_WARNING" '' 'Install ccli dependencies?' || return $FAILURE
}

ask_docker()
{
	cat <<- EOF
	Chef is rather strict concerning its dependencies and has been developed
	and tested specifically against Ubuntu 14.04. However, to allow other
	distributions and/or versions to use Chef, a Linux container image has been
	prepared with docker; the image contains all necessary dependencies to
	compile and run Chef.

	EOF
	note_os
	echo
	ask "$ESC_WARNING" '' 'Install docker?' || return $FAILURE
}

check_sudo()
{
	if is_command sudo; then
		return
	fi
	cat >&2 <<- EOF
	Warning: the \`sudo\` command has not been found on your system. It is very
	likely that the setup will fail due to missing permissions to install
	packages on your system.
	EOF
}

# UTILITIES ====================================================================

os_detect()
{
	OS_NAME=''
	OS_VERSION=''
	if [ -e /etc/lsb-release ]; then
		OS_NAME="$(grep '^DISTRIB_ID' /etc/lsb-release | cut -d'=' -f2)"
		OS_VERSION="$(grep '^DISTRIB_RELEASE' /etc/lsb-release | cut -d'=' -f2)"
	elif [ -e /etc/os-release ]; then
		OS_NAME="$(grep '^NAME=' /etc/os-release | cut -d'=' -f2)"
		OS_VERSION="$(grep '^VERSION_ID=' /etc/os-release | cut -d'=' -f2)"
	fi
}

# GENERIC ======================================================================

package_manager_install()
{
	package_description="$1"
	shift
	package_list=''
	for p in "$@"; do
		pname="$(get_package_name "$p")"
		! package_manager_check "$pname" || continue;
		if package_manager_check_aur "$pname"; then
			package_manager_install_aur "$pname"
		else
			package_list="$package_list $pname"
		fi
	done
	if [ -n "$package_list" ]; then
		note '%s' "$package_description"
		case "$OS_NAME" in
			Arch) sudo pacman -S $package_list ;;
			Debian) sudo aptitude install $package_list ;;
			Ubuntu) sudo apt-get install $package_list ;;
			*) die_unsupported ;;
		esac
	else
		ok '%s' "$package_description"
	fi
}

package_manager_check()
{
	case "$OS_NAME" in
		Arch) pacman -Qi "$1" >"$NULL" 2>"$NULL" || return $FAILURE ;;
		Debian)
			test "$(aptitude search "?installed($1)" | wc -l)" != '0' \
			|| return $FAILURE ;;
		Ubuntu)
			test "$(dpkg --get-selections "$1" 2>"$NULL" | wc -l)" != '0' \
			|| return $FAILURE ;;
		*)
			die_unsupported ;;
	esac
}

package_manager_builddep()
{
	packages="$1"
	case "$OS_NAME" in
		Debian|Ubuntu) apt-get build-dep $1 ;;
		Arch) die_unsupported ;; # lay aside your sanity!
		*) die_unsupported ;;
	esac
}

# DISTRO-SPECIFIC ==============================================================

get_package_name()
{
	# TODO add version switch (package names may change with different versions)
	package="$1"
	if [ "$OS_NAME" = 'Arch' ]; then
		case "$package" in
			python3)           printf 'python'; return ;;
			python3-netifaces) printf 'python-netifaces'; return ;;
			python3-psutil)    printf 'python-psutil'; return ;;
			python3-yaml)      printf 'python-yaml'; return ;;
			python3-requests)  printf 'python-requests'; return ;;
		esac
	elif [ "$OS_NAME" = 'Debian' ]; then
		case "$package" in
			docker) printf 'docker.io'; return ;;
			qemu)   printf 'qemu-kvm'; return ;;
		esac
	elif [ "$OS_NAME" = 'Ubuntu' ]; then
		case "$package" in
			docker) printf 'lxc-docker'; return ;;
			qemu)   printf 'qemu-kvm'; return ;;
		esac
	fi
	printf '%s' "$package"
}

# ARCH-SPECIFIC ================================================================

package_manager_install_aur()
{
	# Arch Linux packages are sometimes unofficial and managed by the community.
	# They lay in an "Arch User Repository" and cannot be installed through the
	# ordinary package manager.
	if ! package_manager_check "$1"; then
		tmpdir="$(mktemp -d "/tmp/s2e_chef_prepare_${1}_XXXXXXXX")"
		git clone "https://aur4.archlinux.org/${1}.git" "$tmpdir/${1}.git"
		cd "$tmpdir/${1}.git"
		makepkg -i
		cd -
		rm -rf "$tmpdir"
	fi
}

package_manager_check_aur()
{
	test "$OS_NAME" = 'Arch' || return $FAILURE
	case "$1" in
		python-netifaces) return $SUCCESS ;;
		*) return $FAILURE ;;
	esac
}

# DEPENDENCIES =================================================================

prepare_dependencies_docker()
{
	package_manager_install 'Installing dependencies: docker' docker acl
}

prepare_dependencies_s2e()
{
	package_manager_install 'Installing dependencies: S²E' \
		build-essential \
		subversion \
		git \
		gettext \
		python-docutils \
		python-pygments \
		nasm \
		unzip \
		wget \
		liblua5.1-dev \
		libsdl1.2-dev \
		libsigc++-2.0-dev \
		binutils-dev \
		libiberty-dev \
		libc6-dev-i386
	package_manager_builddep llvm-3.3 qemu
}

prepare_dependencies_chef()
{
	package_manager_install 'Installing dependencies: chef' \
		gdb \
		strace \
		libdwarf-dev \
		libelf-dev \
		libboost-dev \
		libsqlite3-dev \
		libmemcached-dev \
		libbost-serialization-dev \
		libbost-system-dev \
		libc6-dev-i386
}

prepare_dependencies_ccli()
{
	package_manager_install 'Installing dependencies: ccli' \
		coreutils \
		python3 \
		python3-netifaces \
		python3-psutil \
		python3-requests \
		python3-yaml \
		qemu
}

prepare_dependencies()
{
	check_sudo
	if ask_docker; then
		USE_DOCKER=$TRUE
		prepare_dependencies_docker
	elif [ "$OS_NAME" = 'Ubuntu' ] && [ "$OS_VERSION" = '14.04' ]; then
		USE_DOCKER=$FALSE
		prepare_dependencies_s2e
		prepare_dependencies_chef
	else
		die 1 'Cannot continue preparation'
	fi
	prepare_dependencies_ccli
}

# WORKSPACE ====================================================================

prepare_workspace_tree()
{
	if [ -d "$DATAROOT" ]; then
		skip '%s: directory already exists' "$DATAROOT"
		return
	fi
	mkdir -p "$DATAROOT" || return $FAILURE
	mkdir -p "$DATAROOT_VM" || return $FAILURE
	mkdir -p "$DATAROOT_EXPDATA" || return $FAILURE
}

prepare_workspace_permissions()
{
	if ! is_command setfacl; then
		warn 'ACL utilities not found, workspace incompatible with docker use.'
		return
	fi
	setfacl -d -m user:431:rwx "$WSROOT" || return $FAILURE
	setfacl -d -m user:431:rwx "$WSROOT" || return $FAILURE
}

prepare_workspace()
{
	info 'Initialising Chef data tree in %s' "$DATAROOT"
	test -d "$WSROOT" || die '%s: directory not found' "$WSROOT"
	test -w "$WSROOT" || die '%s: write permissions denied' "$WSROOT"

	prepare_workspace_tree
	prepare_workspace_permissions
}

# DRY RUN ======================================================================

dryrun()
{
	cat <<- EOF
	OS_NAME=$OS_NAME
	OS_VERSION=$OS_VERSION
	EOF
	util_dryrun
}

# MAIN =========================================================================

usage()
{
	echo "Usage: $INVOKENAME [OPTIONS ...] ACTIONS ..."
}

help()
{
	usage
	cat <<- EOF

	Options:
	  -f         Force: initialise data root even if it exists
	  -h         Display this help and exit
	  -y         Dry run: display variable values and exit

	Actions:
	  dependencies
	  workspace
	EOF
}

get_options()
{
	FORCE=$FALSE
	DRYRUN=$FALSE

	while getopts :fhy opt; do
		case "$opt" in
			f) FORCE=$TRUE ;;
			h) help; exit 1 ;;
			y) DRYRUN=$TRUE ;;
			'?') die_help 'Unknown option: -%s' "$OPTARG" ;;
		esac
	done
	ARGSHIFT=$(($OPTIND - 1))
}

get_actions()
{
	test $# -gt 0 || die_help 'Missing action(s)'

	VALID_ACTIONS='dependencies workspace'
	ACTIONS="$@"
	for action in $ACTIONS; do
		if ! list_contains "$VALID_ACTIONS" $action; then
			die_help 'Invalid action: %s' $action
		fi
	done
}

main()
{
	LOGFILE="$WSROOT/prepare.log"

	get_options "$@"
	shift $ARGSHIFT
	get_actions "$@"

	os_detect
	if [ $DRYRUN -eq $TRUE ]; then
		dryrun
		exit 1
	fi
	for action in $ACTIONS; do
		prepare_$action
	done
	success "Successfully prepared system for use with S²E-Chef\n"
}

set -e
main "$@"
set +e
