#!/bin/sh

# Generate configs
android/gen-config.sh android/
android/gen-version.sh android/

# Run waf
./waf configure -T release
./waf
