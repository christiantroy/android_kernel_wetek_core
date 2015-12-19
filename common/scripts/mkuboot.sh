#!/bin/bash

#
# Build U-Boot image when `mkimage' tool is available.
#

unamestr=`uname`
if [[ "${unamestr}" == 'Linux' ]]; then
   MKIMAGE=$srctree/arch/$ARCH/boot/mkimage
elif [[ "${unamestr}" == 'Darwin' ]]; then
   MKIMAGE=$srctree/arch/$ARCH/boot/mkimage-osx
fi

if [ -z "${MKIMAGE}" ]; then
	MKIMAGE=$(type -path mkimage)
	if [ -z "${MKIMAGE}" ]; then
		# Doesn't exist
		echo '"mkimage" command not found - U-Boot images will not be built' >&2
		exit 1;
	fi
fi

# Call "mkimage" to create U-Boot image
${MKIMAGE} "$@"
