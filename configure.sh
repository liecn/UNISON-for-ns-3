#!/bin/bash
# Configure ns-3 with UNISON multi-threaded support and examples enabled

CXXFLAGS=-w ./ns3 configure --enable-mtp --enable-examples --build-profile=optimized --disable-tests --enable-python-bindings --disable-werror --disable-warnings
