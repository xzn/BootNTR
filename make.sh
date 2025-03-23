#!/usr/bin/env bash
rm -r output/*
CONFIG_VER=0x$(cat romfs/*.bin | sha512sum | cut -b1-8)
echo "#define CONFIG_VERSION $CONFIG_VER" > config_ver_new.h
if ! cmp -s config_ver_new.h config_ver.h
then
mv config_ver_new.h config_ver.h
else
rm config_ver_new.h
fi
make -j$(nproc) $@
