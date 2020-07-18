
# MiSTer Batch control

This is a simple application to control the [MiSTer
fpga](https://github.com/MiSTer-devel)). It main purpose is to load ROM files
since this features is not supported by the MiSTer out of the box.

# Build

The build should be straightforward:

```
arm-linux-gnueabihf-gcc -O2 -o /media/user/MiSTer_Data/fbmenu/LCR mbc.c
```

However you can find pre-compiled binaries in the [release
page](https://github.com/pocomane/MiSTer_Batch_Control/releases).

# Usage

Running the `mbc` without arguments will give minimal help. It is meant to be
run like:

```
mbc COMMAND [ARG1 [ARG2 [...]]]
```

# The load_all_as command

```
mbc load_all SYSTEM /PATH/TO/CORE /PATH/TO/ROM
```

This will load the core and the rom. The `SYSTEM` parameter is needed to match
MiSTer specific directories in an internal database. The supported systems can
be retrieved running `mbc` without arguments.


# The load_all command

```
mbc load_all /PATH/TO/CORE /PATH/TO/ROM
```

This is similar to the `load_all_as` command, but it tries to match the system
from the core file name.

# The stream command

```
mbc stream
```

It will open the standard input, and will execute each line as a single command.

# TODO : document other commands !

