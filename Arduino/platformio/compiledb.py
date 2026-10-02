# List the toolchain's own include dirs (avr/pgmspace.h, ...) in
# compile_commands.json, so editors find them without querying avr-g++.
Import("env")
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)
