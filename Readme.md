
# MiSTer Batch control

This is a simple command line utility to control the [MiSTer
fpga](https://github.com/MiSTer-devel)). It main purpose is to load ROM files
since this features is not supported by the MiSTer out of the box.

# Installation

A simple script to install this utility, among with others, is in
[MiSTer_Misc](https://github.com/pocomane/MiSTer_misc/) repository. Alternatively
you can find pre-compiled binaries in the [release
page](https://github.com/pocomane/MiSTer_Batch_Control/releases/latest).

If you make some changes to the code, you can use the `build.sh` script to
download the [gcc/musl crosscompile toolchain](http://musl.cc), and to compile
a static version of the application. However, also a simple command like:

```
arm-linux-gnueabihf-gcc -std=c99 -static -D_XOPEN_SOURCE=700 -o mbc mbc.c
```

should be able to compile the utility, since it does not have any dependency
(except standard C library and linux interface).

The default utility works by creating symbolic links to the target rom files.
Another system is avaiable, using mount/bind points. To enable this feature, the
source must be compiled defining `USE_MOUNT_POINTS`, e.g. :

```
arm-linux-gnueabihf-gcc -std=c99 -static -DUSE_MOUNT_POINT -D_XOPEN_SOURCE=700 -o mbc mbc.c
```

# Support

Some functionalities need specific support for each system.  To handle
not-supported or non-standard cases, the `CUSTOM` system is provided. It let
you, for example, to freely set the game directory, the symulated key sequence,
etc.  It is configured with the environment variables specified in the
`list_core` command section.  If you make a system work in this way, please
open a github issue specifying the value of the variables, so its support can
be easly added.

Please note that these informations could change from one release of the MiSTer
to another, so please, make sure you are referring to the last release. If the
utility stops to work after an update for some specific core, you can open an
issue with the same information: probably I just did not update them yet.

If you want to make the change by yourself (e.g. to support an old release of
the MiSTer), a single line of code for each core must be added/changed in the
definition of the `system_list` variable. It just contains the same
informations described in `list_core`.

# Usage

Running the `mbc` without arguments will give minimal help. To actually perform
some action on the MiSTer, `mbc` must be run like:

```
mbc COMMAND [ARG1 [ARG2 [...]]]
```

The main commands are `raw_seq` and `load_rom`, but other usefull ones are
provided too. Please refer to the documentation of the single commands in this
Readme.

Some commands need specific support for each cores. A special system `CUSTOM`
is provided to try to launch unsupported core. See documentation of the `list_core`
for more details.


## Command raw_seq

```
mbc raw_seq KEY_SEQUENCE
```

This command will emulate the key press and release as described in the
`KEY_SEQUENCE`. It must be an uppercase string containig just the following
characters:

- U - Up arrow
- D - Down arrow
- L - Left arrow
- R - Right arrow
- O - Enter (Open)
- E - ESC
- H - Home
- F - End (Finish)
- M - F12 (Menu)

For example, the command

```
mbc raw_seq EEMDDO
```

sends Esc, Esc, F12, down, dowm, enter, so it selects the third item of the
menu. The timing can be configured by means of the following environment
variables:

- `MBC_CORE_WAIT` sets the number of milliseconds to wait for the core to be loaded;
  the default is 3000.

- `MBC_KEY_WAIT` sets the number of milliseconds to wait between each key-press;
  the default is 40.

- `MBC_SEQUENCE_WAIT` sets the number of milliseconds to wait before and after a key
  sequence; the default is 1000.


## Command load_rom

```
mbc load_rom SYSTEM /PATH/TO/ROM
```

This will load the default core associated to the `SYSTEM`, then it will load
the rom passed as argument. The supported systems can be retrieved with the
`list_core` command.

The games can be loaded from any directory, but the default one MUST exist
(e.g. /media/fat/games/NES). It can be empty.


## Command list_core

```
mbc list_core
```

The `list_core` command lists the systems. The list will contain the `SYSTEM` id and
the path of the default core. The special system `CUSTOM` can be by mean of the
following environment variables:

- `MBC_CUSTOM_SEQUENCE` is the sequence of keys needed to open the rom
  selection menu; it must be specified in the same format of the `raw_seq`
  command.  e.g. `MBC_CUSTOM_SEQUENCE=EEMO`

- `MBC_CUSTOM_CORE` is the fixed suffix of the path of the core file, e.g.
  `MBC_CUSTOM_CORE=/media/fat/_Console/NES_`

- `MBC_CUSTOM_FOLDER` is the name of the folders that are related to the system,
   e.g. `MBC_CUSTOM_ROM_PATH=NES`

- `MBC_CUSTOM_ROM_EXT` is the extension of the rom files, e.g.
  `MBC_CUSTOM_ROM_EXT=nes`


## Command list_content

```
mbc list_content
```

It will list all the games in the default directory. Each game is preceded by
the system name and a space. The system name is upper case and has no spaces.

Please note that the directory is not recursivelly searched; linux has better
tools for such purpose. This command is meant just to give a quick feedback
of the `mbc` functionalities.


## Command load_all_as

```
mbc load_all_as SYSTEM /PATH/TO/CORE /PATH/TO/ROM
```

This is similar to `load_rom` but it will use the core provaided as argument
instead of the default one.


## Command load_all

```
mbc load_all /PATH/TO/CORE /PATH/TO/ROM
```

This is similar to the `load_all_as` command, but it tries to match the system
from the core file name.


## Command stream

```
mbc stream
```

It will open the standard input, and will execute each line as a single command.


## TODO : document other commands !

