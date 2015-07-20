#!/usr/bin/env sh

# This script installs a "prepared-for-Chef" Debian in a directory through
# debootstrap.
# It is not meant for direct use, as there is no argument checking.

# Usage: debian.sh [DIRECTORY]
# XXX if DIRECTORY is omitted, it assumes to be running inside the chroot

DEBIAN_RELEASE='wheezy'
DEBIAN_URL='http://http.debian.net/debian'
CHROOT_ROOT='/ccli'

# UTILITIES ====================================================================

# The location of utils.sh depends on whether this script is invoked inside the
# chroot as part of the whole process, or called directly outside the chroot.

# => sourced in main()

stamp()
{
	stampfile="${DIRECTORY}${CHROOT_ROOT}/${1}.stamp"
	function="$1"
	shift
	if [ -e "$stampfile" ]; then
		skip '%s(): stamp file %s exists' "$function" "$stampfile"
		return
	fi
	retval=$TRUE
	if "$function" "$@"; then
		retval=$TRUE
		note "$_OK__ %s()" "$function"
		touch "$stampfile"
	else
		retval=$FALSE
		note "$FAIL_ %s()" "$function"
	fi
	return $retval
}

# INSTALLATION (OUTSIDE CHROOT) ================================================

_mount()
{
	mount -o loop,rw,offset=$MEBI "$RAW" "$DIRECTORY"
}

_debootstrap()
{
	if ! debootstrap --arch i386 "$DEBIAN_RELEASE" "$DIRECTORY" "$DEBIAN_URL"
	then
		fail "debootstrap failed\n"
		return $FALSE
	fi
}

_chroot()
{
	mount -t sysfs sysfs "$DIRECTORY"/sys
	mount -t proc proc "$DIRECTORY"/proc
	ln -f "$RUNPATH/$RUNNAME" "${DIRECTORY}$CHROOT_ROOT/$RUNNAME"
	ln -f "$UTILS" "${DIRECTORY}$CHROOT_ROOT/utils.sh"
	PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' \
		chroot "$DIRECTORY" "$CHROOT_ROOT/$RUNNAME" \
	|| fail "chroot failed\n"
	umount "$DIRECTORY"/sys
	umount "$DIRECTORY"/proc
}

_umount()
{
	umount "$DIRECTORY"
}

# INSTALLATION (INSIDE CHROOT) =================================================

_base()
{
	# Locale
	printf "%s/#\\(en_GB.UTF-8\\)/\\1/g\nwq\n" | ex -s /etc/locale.gen
	cat > /etc/default/locale <<- EOF
	LANG="en_GB.UTF-8"
	LANGUAGE="en_GB:en"
	EOF
}

_update()
{
	cat >>/etc/apt/sources.list <<- EOF
	deb http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE main contrib non-free
	deb-src http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE main

	deb http://security.debian.org/ $DEBIAN_RELEASE/updates main
	deb-src http://security.debian.org/ $DEBIAN_RELEASE/updates main

	# $DEBIAN_RELEASE-updates, previously known as 'volatile'
	deb http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE-updates main
	deb-src http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE-updates main
	EOF
	apt-get -y update
}

_grub()
{
	aptitude -y install grub
	ls /dev
	return $FALSE
}

_kernel()
{
	: # TODO custom kernel
}

# MAIN =========================================================================

main()
{
	RAW="$1"
	if [ -z "$RAW" ]; then
		# Running inside chroot:
		UTILS=/ccli/utils.sh
		. "$UTILS"
		DIRECTORY=''
		stamp _base
		stamp _update
		stamp _grub
		stamp _kernel
	else
		# Invoked:
		UTILS="$(dirname "$(readlink -f "$(dirname "$0")")")/utils.sh"
		. "$UTILS"
		DIRECTORY="${RAW}.mount"
		mkdir -p "$DIRECTORY/ccli"
		_mount
		stamp _debootstrap && _chroot
		_umount
	fi
}

set -e
main "$@"
set +e
