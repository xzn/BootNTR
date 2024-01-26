#!/usr/bin/env bash
rm -r output/*
make -j$(nproc) $@
