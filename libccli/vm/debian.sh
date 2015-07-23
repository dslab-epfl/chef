#!/usr/bin/env sh

# This script installs a "prepared-for-Chef" Debian in a directory through
# debootstrap.
# It is not meant for direct use, as there is no argument checking or help
# messages.

# Usage: debian.sh [DIRECTORY]
# XXX if DIRECTORY is omitted, it assumes to be running inside the chroot

DEBIAN_RELEASE='wheezy'
DEBIAN_URL='http://http.debian.net/debian'
DEBIAN_PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'
DEBIAN_LANG='en_GB.UTF-8'
DEBIAN_LANGUAGE='en_GB:en'

CHROOT_ROOT='/ccli'

# UTILITIES ====================================================================

# The location of utils.sh depends on whether this script is invoked inside the
# chroot as part of the whole process, or called directly outside the chroot.
# => sourced in main()

stamp()
{
	stamp_action="$1"
	stamp_file="${DIRECTORY}${CHROOT_ROOT}/${stamp_action}.stamp"
	stamp_function="debian_$stamp_action"
	shift

	if [ -e "$stamp_file" ]; then
		skip '%s: previously completed' "$stamp_action"
		return
	fi

	pend "$stamp_action"
	if ! "$stamp_function" "$@"; then
		return $FALSE
	fi
	ok "$stamp_action"
	touch "$stamp_file"
	return 0
}

# INSTALLATION (OUTSIDE CHROOT) ================================================

debian_mount()
{
	mkdir -p "$DIRECTORY"
	track "mount disk image: $RAW => $DIRECTORY" \
		mount -o loop "$RAW" "$DIRECTORY" \
	|| return $FALSE
	CLEANUP="umount $CLEANUP"
	mkdir -p "${DIRECTORY}$CHROOT_ROOT"
}

debian_debootstrap()
{
	debootstrap --arch i386 "$DEBIAN_RELEASE" "$DIRECTORY" "$DEBIAN_URL" \
	|| return $FALSE
}

debian_mount_sysfs()
{
	if ! track 'mount sysfs' mount -t sysfs sysfs "$DIRECTORY"/sys; then
		examine_logs
		return $FALSE
	fi
	CLEANUP="umount_sysfs $CLEANUP"
}

debian_mount_proc()
{
	if ! track 'mount proc' mount -t proc proc "$DIRECTORY"/proc; then
		examine_logs
		return $FALSE
	fi
	CLEANUP="umount_proc $CLEANUP"
}

debian_chroot()
{
	cp "$RUNPATH/$RUNNAME" "${DIRECTORY}$CHROOT_ROOT/$RUNNAME"
	cp "$UTILS" "${DIRECTORY}$CHROOT_ROOT/utils.sh"
	PATH="$DEBIAN_PATH" chroot "$DIRECTORY" "$CHROOT_ROOT/$RUNNAME" \
	|| return $FALSE
}

debian_umount_proc()
{
	if ! track 'unmount proc' umount "$DIRECTORY"/proc; then
		examine_logs
		return $FALSE
	fi
}

debian_umount_sysfs()
{
	if ! track 'unmount sysfs' umount "$DIRECTORY"/sys; then
		examine_logs
		return $FALSE
	fi
}

debian_umount()
{
	if ! track 'unmount disk image' umount "$DIRECTORY"; then
		examine_logs
		return $FALSE
	fi
}

# INSTALLATION (INSIDE CHROOT) =================================================

debian_packages()
{
	cat >/etc/apt/sources.list <<- EOF
	deb http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE main contrib non-free
	deb-src http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE main

	deb http://security.debian.org/ $DEBIAN_RELEASE/updates main
	deb-src http://security.debian.org/ $DEBIAN_RELEASE/updates main

	# $DEBIAN_RELEASE-updates, previously known as 'volatile'
	deb http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE-updates main
	deb-src http://ftp.ch.debian.org/debian/ $DEBIAN_RELEASE-updates main
	EOF
}

debian_update()
{
	aptitude -y update
}

debian_locale()
{
	aptitude -y install locales
	dpkg-reconfigure locales  # is handled by debconf-set-selections
}

# MAIN =========================================================================

interrupt()
{
	INTERRUPTED=$TRUE
	if [ $FID -ne 0 ]; then
		if [ "$FNAME" = 'debootstrap' ]; then
			pkill -1 debootstrap # debootstrap spawns multiple processes
		else
			kill -1 $FID
		fi
	fi
}

cleanup()
{
	for c in $CLEANUP; do
		if ! debian_$c; then
			warn 'could not %s' "$c"
		fi
	done
}

main()
{
	RAW="$1"
	RETVAL=0
	if [ -z "$RAW" ]; then
		# Running inside chroot:
		UTILS="$CHROOT_ROOT"/utils.sh
		. "$UTILS"
		DIRECTORY=''

		for action in packages update locale; do
			if ! stamp $action; then
				fail '%s' "$action"
				die 1
			fi
		done
	else
		# Invoked:
		UTILS="$(dirname "$(readlink -f "$(dirname "$0")")")/utils.sh"
		. "$UTILS"
		DIRECTORY="${RAW}.mount"
		LOGFILE="${RAW}.log"
		INTERRUPTED=$FALSE
		FID=0
		CLEANUP=''
		trap interrupt INT
		trap interrupt TERM
		for i in mount debootstrap mount_sysfs mount_proc chroot; do
			test $INTERRUPTED = $FALSE || break
			stamp=''
			prefix='debian_'
			FNAME=''
			if [ $i = debootstrap ]; then
				FNAME=debootstrap
				stamp='stamp'
				prefix=''
			fi
			if ! case $i in (debootstrap|chroot) false ;; esac; then
				$stamp ${prefix}$i &
				FID=$!
				wait $FID || RETVAL=$?
			else
				$stamp ${prefix}$i || RETVAL=$?
			fi
			test $RETVAL -eq $TRUE || break
		done
		cleanup
	fi
	return $RETVAL
}

set -e
main "$@"
set +e
