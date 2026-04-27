#!/usr/bin/env bash

# If your project makes use of third-party libraries, you can
# install them here.

# The script should contain one command per line, and each command
# must start with either `sudo apt-get install` or `git clone`.
# Any other commands will be ignored by markers.


# Install libcheck
sudo apt-get update
sudo apt-get install -y check pkg-config

# Install AFL++ for fuzzing later
sudo apt-get install -y afl++

# Install Valgrind - dynamic analyser 
sudo apt-get install -y valgrind