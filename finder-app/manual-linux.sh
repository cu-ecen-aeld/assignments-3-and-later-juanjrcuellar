#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Author of the assignment: Juan Jose Rodriguez

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
    # Normalize path
    OUTDIR=$(realpath ${OUTDIR})
	echo "Using passed directory ${OUTDIR} for output"
fi

if [ ! -d "${OUTDIR}" ]; then
    echo "Specified directory in ${OUTDIR} does not exist. Creating one"
    mkdir -p ${OUTDIR}
    if [ ! -d "${OUTDIR}" ]; then
        echo "${OUTDIR} could not be created. Exiting"
		exit 1
	fi
    echo "${OUTDIR} created"
else
    echo "Specified directory in ${OUTDIR} exists already"
fi

echo ''
echo "******************"
echo "Linux Kernel Build"
echo "******************"
echo " ARCH: ${ARCH}"
echo " TOOLCHAIN: ${CROSS_COMPILE}"

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

    # DONE: Add your kernel build steps here
    echo "Cleaning all previous generated files and config..."
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper

    echo "Generating new config..."
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig

    echo "Generating kernel image..."
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    echo "Kernel build done"
else
    echo "A Linux Image exists already in ${OUTDIR}/linux-stable/arch/${ARCH}/boot"
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo ''
echo "***************"
echo "Root Filesystem"
echo "***************"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# DONE: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
echo "Base directories for the root filesystem created in ${OUTDIR}/rootfs"

echo ''
echo "*******"
echo "busybox"
echo "*******"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}

    # DONE: Configure busybox
    echo "Configuring Busybox..."
    make distclean
    make defconfig
else
    cd busybox
fi

# DONE: Make and install busybox
echo "Building and installing Busybox..."
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo ''
echo "********************"
echo "Library dependencies"
echo "********************"
cd "${OUTDIR}/rootfs"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# DONE: Add library dependencies to rootfs

SYSROOT=$(realpath $(aarch64-none-linux-gnu-gcc --print-sysroot))
PROG_INT_DIR=${SYSROOT}/lib/ld-linux-aarch64.so.1
LIBM_DIR=${SYSROOT}/lib64/libm.so.6
LIBRESOLV_DIR=${SYSROOT}/lib64/libresolv.so.2
LIBC_DIR=${SYSROOT}/lib64/libc.so.6

if [ ! -e ${PROG_INT_DIR} ]; then
    echo "PROGRAM_INTERPRETER does not exist. Exiting"
    exit 1
fi

echo "Program interpreter could be found in ${PROG_INT_DIR}"
echo "Copying it to ${OUTDIR}/rootfs/lib"
cp ${PROG_INT_DIR} ${OUTDIR}/rootfs/lib

if [ ! -e ${LIBM_DIR} ]; then
    echo "LIBM_DIR does not exist. Exiting"
    exit 1
fi

echo "libm.so.6 could be found in ${LIBM_DIR}"
echo "Copying it to ${OUTDIR}/rootfs/lib64"
cp ${LIBM_DIR} ${OUTDIR}/rootfs/lib64

if [ ! -e ${LIBRESOLV_DIR} ]; then
    echo "LIBRESOLV_DIR does not exist. Exiting"
    exit 1
fi

echo "libresolv.so.2 could be found in ${LIBRESOLV_DIR}"
echo "Copying it to ${OUTDIR}/rootfs/lib64"
cp ${LIBRESOLV_DIR} ${OUTDIR}/rootfs/lib64

if [ ! -e ${LIBC_DIR} ]; then
    echo "LIBC_DIR does not exist. Exiting"
    exit 1
fi

echo "libc.so.6 could be found in ${LIBC_DIR}"
echo "Copying it to ${OUTDIR}/rootfs/lib64"
cp ${LIBC_DIR} ${OUTDIR}/rootfs/lib64

# DONE: Make device nodes
echo ''
echo "************"
echo "Device nodes"
echo "************"

cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

echo "devices nodes dev/null and dev/console created"

# DONE: Clean and build the writer utility
echo ''
echo "**************"
echo "Writer utility"
echo "**************"

cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE} all
echo "Writer utility built"

# DONE: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

cp -v ./writer ${OUTDIR}/rootfs/home
cp -v ./finder.sh ${OUTDIR}/rootfs/home
cp -v ./finder-test.sh ${OUTDIR}/rootfs/home
mkdir -p -v ${OUTDIR}/rootfs/home/conf
cp -v ../conf/* ${OUTDIR}/rootfs/home/conf
cp -v autorun-qemu.sh ${OUTDIR}/rootfs/home/
echo "All finder scripts and executables copied"

# DONE: Chown the root directory
echo ''
echo "************************"
echo "chown the root directory"
echo "************************"

cd "${OUTDIR}/rootfs"
sudo chown -R root:root *
echo "done."

# DONE: Create initramfs.cpio.gz
echo ''
echo "*********"
echo "initramfs"
echo "*********"

find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
echo "initramfs.cpio.gz created"

echo ''
echo "Linux build finished succesfully"
