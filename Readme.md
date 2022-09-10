
# MiSTer Batch control

This is a simple command line utility to control the [MiSTer
fpga](https://github.com/MiSTer-devel). Its main purpose is to load ROM files
from the command line since this features is not supported by the MiSTer out of
the box.

This software is in the public domain.  IT IS PROVIDED "AS IS", WITHOUT
WARRANTY OF ANY KIND.

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

# Support

Some functionalities need specific support for each system.  To handle
not-supported or non-standard cases, the `CUSTOM` system is provided. It let
you, for example, to freely set the game directory, the startup delay,
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
`KEY_SEQUENCE`. It is a sequence of the following codes with the following
meanings:

- U - press and release up arrow
- D - press and release down arrow
- L - press and release left arrow
- R - press and release right arrow
- O - press and release enter (Open)
- E - press and release esc
- H - press and release home
- F - press and release end (Finish)
- M - press and release F12 (Menu)
- Lowercase letters (a-z) or digits (0-9) - press and release of the corresponding key
- :XX - press and release of the key with the hex code XX
- {XX - press of the key with the hex code XX
- }XX - release of the key with the hex code XX
- !s - wait for 1 second
- !m - wait that some mount point changes (the first time it is called, it does
  not wait since the initial detected mount table is empty; so it must be called
  multiple time in the sequence, e.g. `EEMDD!mO!m`)

The `XX` value is the hex representition of the coded in the
[uapi/input-event-code](https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h)
kernel header.

For example, the commands

```
mbc raw_seq EEMDDO
mbc raw_seq :01:01:58:6C:6C:1C
```

do the same thing: they send Esc, Esc, F12, down, dowm, enter, so they select
the third item of the menu.

The timing can be configured by means of the following environment variables:

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

- `MBC_CUSTOM_CORE` is the fixed suffix of the path of the core file; the special
  value "!direct" can be used to load the rom directly, like in the ARCADE system;
  e.g. `MBC_CUSTOM_CORE=/media/fat/_Console/NES_`

- `MBC_CUSTOM_FOLDER` is the name of the folders that are related to the system,
  e.g. `MBC_CUSTOM_FOLDER=NES`

- `MBC_CUSTOM_ROM_EXT` is the extension of the rom files, e.g.
  `MBC_CUSTOM_ROM_EXT=nes`

- `MBC_CUSTOM_DELAY` number of seconds to wait before load/mount the rom, more
  details at [MGL doc](https://mister-devel.github.io/MkDocs_MiSTer/advanced/mgl/#mgl-format);
  e.g. `MBC_CUSTOM_DELAY=2`

- `MBC_CUSTOM_MODE` type and index for the MGL load, more details at [MGL doc](https://mister-devel.github.io/MkDocs_MiSTer/advanced/mgl/#mgl-format);
  e.g. `MBC_CUSTOM_MODE=f0`

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


## Command catch_input

```
mbc catch_input TIMEOUT_MS
```

Each time a button is pressed a `event catched` will be printed, plus an event
counter. If no event was found within a certain time, `timeout` will be
printed. The timeout time is specified in milliseconds and if it is negative,
no timeout is set and `timeout` will be never printed.

Please note that the `MiSTer` main app opens all the inputs in exclusive mode.
This means that while it is running, `mbc` will not see any event. To use this
command the `MiSTer` main app must be stopped (and then restarted after the exit of
`mbc`). An example is in the `example/demomode.sh` script.

## Command wait_input

```
mbc wait_input TIMEOUT_MS
```

This is similar to `catch_input` command, but it exits after the first event or
timeout.  Same attention of `catch_input` command should be paid when operating
with the `MiSTer` app.

## Command stream

```
mbc stream
```

It will open the standard input, and will execute each line as a single command.

## Command mgl_gen

```
mbc mgl_gen SYSTEM /PATH/TO/CORE /PATH/TO/ROM
```

This will generate an MGL able to run the CORE and load the ROM. The SYSTEM must
be provided in order to generate a compatible MGL script. The output script will
be printed on the standard output.

## Command mgl_gen_auto

```
mbc mgl_gen_auto /PATH/TO/CORE /PATH/TO/ROM
```

This is similar to the `mgl_gen` command, but it tries to match the system
from the core file name.

## Command list_rom_for

TODO : describe

## Command load_core

TODO : describe

## Command load_core_as

TODO : describe

## Command done

TODO : describe

