MACHINE=overo

# export an alternative OETMP before sourcing this file if you
# don't want the default
if [[ -z "${OETMP}" ]]; then
	OETMP=${OVEROTOP}/tmp
fi

SYSROOTSDIR=${OETMP}/sysroots
STAGEDIR=${SYSROOTSDIR}/`uname -m`-linux/usr

export KERNELDIR=${SYSROOTSDIR}/${MACHINE}/usr/src/kernel

PATH=${PATH}:${STAGEDIR}/bin:${STAGEDIR}/bin/armv7a-vfp-neon-poky-linux-gnueabi

unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS MACHINE

export ARCH="arm"
export CROSS_COMPILE="arm-poky-linux-gnueabi-"
export CC="arm-poky-linux-gnueabi-gcc"
export LD="arm-poky-linux-gnueabi-ld"
export STRIP="arm-poky-linux-gnueabi-strip"
export AR="arm-poky-linux-gnueabi-ar"

