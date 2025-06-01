#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir --parents "${OUTDIR}"
if [ $? -ne 0 ]; then
	echo "ERROR: failed creating directory '$OUTDIR'" >&2
	exit 1
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Add your kernel build steps here
		cp "$FINDER_APP_DIR/conf/.config" ./
		make -j4 "CROSS_COMPILE=$CROSS_COMPILE" "ARCH=$ARCH" all
fi

echo "Adding the Image in outdir"
cp "$OUTDIR/linux-stable/arch/arm64/boot/Image" "$OUTDIR/"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir "$OUTDIR/rootfs"
cd "$OUTDIR/rootfs"
mkdir --parents bin dev etc home lib lib64 proc sys sbin tmp usr var
mkdir --parents usr/bin usr/lib usr/sbin
mkdir --parents var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
		make distclean
		make defconfig
else
    cd busybox
fi

# Make and install busybox
make -j4 "CROSS_COMPILE=$CROSS_COMPILE" "ARCH=$ARCH"
make -j4 "CROSS_COMPILE=$CROSS_COMPILE" "ARCH=$ARCH" "CONFIG_PREFIX=$OUTDIR/rootfs" install

cd "$OUTDIR/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
TOOLCHAIN_FS=$(dirname $(which "${CROSS_COMPILE}readelf"))/..
cp "$TOOLCHAIN_FS/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1" "$OUTDIR/rootfs/lib/"
cp "$TOOLCHAIN_FS/aarch64-none-linux-gnu/libc/lib64/libm.so.6" "$OUTDIR/rootfs/lib64/"
cp "$TOOLCHAIN_FS/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2" "$OUTDIR/rootfs/lib64/"
cp "$TOOLCHAIN_FS/aarch64-none-linux-gnu/libc/lib64/libc.so.6" "$OUTDIR/rootfs/lib64/"

# Make device nodes
cd "$OUTDIR/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# Clean and build the writer utility
cd "$FINDER_APP_DIR"
make "CROSS_COMPILE=$CROSS_COMPILE" clean all

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp writer "$OUTDIR/rootfs/home/"
cp finder.sh "$OUTDIR/rootfs/home/"
cp -rL conf "$OUTDIR/rootfs/home/"
cp finder-test.sh "$OUTDIR/rootfs/home/"
cp autorun-qemu.sh "$OUTDIR/rootfs/home/"

# Chown the root directory
sudo chown --recursive root:root "$OUTDIR/rootfs"

# Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
gzip -f "$OUTDIR/initramfs.cpio"
