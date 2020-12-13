
# MiSTer Batch control

This is a simple command line utility to control the [MiSTer
fpga](https://github.com/MiSTer-devel)). It main purpose is to load ROM files
since this features is not supported by the MiSTer out of the box.

# Build

The build should be straightforward:

```
arm-linux-gnueabihf-gcc -O2 -o mbc mbc.c
```

However you can find pre-compiled binaries in the [release
page](https://github.com/pocomane/MiSTer_Batch_Control/releases).

Moreover, the `build.sh` will download the [gcc/musl crosscompile
toolchain](http://musl.cc), and it will compile a static version of the
application.

Some functionalities need specific support for each cores. If you want that a
particular core is directly supported, please open a github issue specifying
the value of variables described int the `list_core` command in this Readme.

Please note that these informations could change from one release of the MiSTer
to another, so please, make sure you are referring to the last release. If the
utility stops to work after an update for some specific core, you can open an
issue with the same information: probably I just did not update them yet.

If you want to make the change by yourself (e.g. to support an old release of
the MiSTer), a single line of code for each core must be added/changed in the
definition of the `system_list` variable. It just contains the same
informations described in `list_core`.

# Usage

Running the `mbc` without arguments will give minimal help. To actually perform som
action on the MiSTer, `mbc` must be run like:

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
  the default is 50.

- `MBC_SEQUENCE_WAIT` sets the number of milliseconds to wait before and after a key
  sequence; the default is 1000.


## Command list_core

```
mbc list_core
```

The `list_core` command lists the systems. The list will contain the `SYSTEM` id and
the path of the default core. The special system `CUSTOM` can be by mean of the
following environment variables:

- `MBC_CUSTOM_SEQUENCE` is the key sequence needed to open the menu and select
  the last rom; it must be specified in the same format of the `raw_seq`
  command. e.g. `MBC_CUSTOM_SEQUENCE=EEMODO`

- `MBC_CUSTOM_CORE` is the fixed suffix of the path of the core file, e.g.
  `MBC_CUSTOM_CORE=/media/fat/_Console/NES_`

- `MBC_CUSTOM_ROM_PATH` is the path of the default rom
  directory, e.g. `MBC_CUSTOM_ROM_PATH=/media/fat/games/NES`

- `MBC_CUSTOM_ROM_EXT` is the extension of the rom files, e.g.
  `MBC_CUSTOM_ROM_EXT=nes`

## Command load_rom

```
mbc load_rom SYSTEM /PATH/TO/ROM
```

This will load the default core associated to the `SYSTEM`, then it will load
the rom passed as argument. The supported systems can be retrieved with the
`list_core` command.


## Command list_content

```
mbc list_content
```

It will list all the rom in the default MiSTer directories. Each rome is
preceded by the system name and a space. The system name is upper case and has
no spaces.

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

