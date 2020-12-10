
# MiSTer Batch control

This is a simple command line application to control the [MiSTer
fpga](https://github.com/MiSTer-devel)). It main purpose is to load ROM files
since this features is not supported by the MiSTer out of the box.

# Core supports

Each core must be specifically supported. If a core you are interested in is
not already supported, please open an issue with the following information:

- The key sequence needed to select the last rom from the menu, e.g. F12,
  Left, Enter, End, Enter
- Extension of the rom files, e.g. "nes"
- Path of the default rom directory, e.g. "/media/fat/games/NES"
- Suffix of the path of the core file, e.g. "/media/fat/_Console/NES_"
- An easy-to-remember name, e.g. "NES"

Please note that these informations could change from one release of the MiSTer
to another, so please, make sure you are refering to the last release. If a core
stops to work after an update, open a issue with the same information: probably
I just did not update them yet.

If you want to make the change by yourself (e.g. to support an old release of
the MiSTer), a single line of code for each core must be added in the
system_list variable definition. It just contains the same informationi
previosly described.

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

# Usage

Running the `mbc` without arguments will give minimal help. To actually perform som
action on the MiSTer, `mbc` must be run like:

```
mbc COMMAND [ARG1 [ARG2 [...]]]
```

Please refer to the documentation of the single commands.

## Command list_core

```
mbc list_core
```

This will list all the supported systems. The list will contain the `SYSTEM` id
and the path of the default core.

Note - Only the following systems are supported for now: ATARI2600, GAMEBOY, GBA,
GENESIS, NES (cart and disk), SMS, SNES, TURBOGRAFX (and SGX).

## Command list_content

```
mbc list_content
```

It will list all the rom in the default MiSTer directories. Each rome is
preceded by the system name and a space. The system name is upper case and has
no spaces.

## Command load_rom

```
mbc load_rom SYSTEM /PATH/TO/ROM
```

This will load the default core associated to the `SYSTEM`, then it will load
the rom passed as argument. The supported systems can be retrieved with the
`list_core` command.

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
mbc raw_seq MDDO
```

sends F12, down, dowm, enter, so it selects the third item of the menu.

## Command stream

```
mbc stream
```

It will open the standard input, and will execute each line as a single command.

## TODO : document other commands !

