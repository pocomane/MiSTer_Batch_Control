#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <linux/uinput.h>

#define DEVICE_NAME "Fake device"
#define DEVICE_PATH "/dev/uinput"
#define MISTER_COMMAND_DEVICE "/dev/MiSTer_cmd"
#define AUX_ROM_NAME "~~~"
#define CORE_EXT "rbf"

#define ARRSIZ(A) (sizeof(A)/sizeof(A[0]))
#define LOG(F,...) printf("%d - " F, __LINE__, __VA_ARGS__ )
#define PRINTERR(F,...) LOG("error - %s - " F, strerror(errno ? errno : EPERM), __VA_ARGS__ )
#define SBSEARCH(T, SA, C)	(bsearch(T, SA, sizeof(SA)/sizeof(SA[0]), sizeof(SA[0]), (C)))

static void msleep(long ms){
  struct timespec w;
  w.tv_sec = ms / 1000;
  w.tv_nsec = (ms % 1000 ) * 1000000;
  while (nanosleep(&w, &w));
}

static int ev_open() {

  int fd = open(DEVICE_PATH, O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    PRINTERR("%s", DEVICE_PATH);
    return fd;
  }

  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (int i=KEY_ESC; i<KEY_MAX; i++){
    ioctl(fd, UI_SET_KEYBIT, i);
  }

#if UINPUT_VERSION < 5
  struct uinput_user_dev uud;
  memset(&uud, 0, sizeof(uud));
  snprintf(uud.name, UINPUT_MAX_NAME_SIZE, DEVICE_NAME);
  write(fd, &uud, sizeof(uud));
#error MiSTer needs UINPUT 5 or above
#else
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234; /* fake vendor */
  usetup.id.product = 0x5678; /* fake product */
  strcpy(usetup.name, DEVICE_NAME);
  ioctl(fd, UI_DEV_SETUP, &usetup);
  ioctl(fd, UI_DEV_CREATE);
#endif

  return fd;
}

static void ev_emit(int fd, int type, int code, int val) {
  struct input_event ie = {0,};
  ie.type = type;
  ie.code = code;
  ie.value = val;
  //gettimeofday(&ie.time, NULL);
  write(fd, &ie, sizeof(ie));
}

static int ev_close(int fd) {
  ioctl(fd, UI_DEV_DESTROY);
  return close(fd);
}

static int mkparent(char *path, mode_t mode) {
  int depth = 0;
  char *curr = path, *found = 0;

  while (0 != (found = strchr(curr, '/'))) {
    if (found != curr) { // skip root or double slashes in path
      depth += 1;

      *found = '\0';
      int created = !mkdir(path, mode);
      int already_exist = (errno == EEXIST);
      *found = '/';

      // no error if a directory already exist
      if (!created && !already_exist) {
        return -depth;
      } else {
        if (already_exist) {
          struct stat st;
          *found = '\0';
          int stat_error = stat(path, &st);
          *found = '/';
          if (stat_error) return -depth;
          if (!S_ISDIR(st.st_mode)) {
            return -depth;
          }
        }
      }

    }
    curr = found + 1;
  }
  return 0;
}

static int mkparent_dup(const char *path, mode_t mode) {
  char *pathdup = strdup(path);
  if (!pathdup) return -1;
  int status = mkparent(pathdup, mode);
  free(pathdup);
  return status;
}

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE without any space.
  char *menuseq; // Sequence of input for the rom selection; searched in the internal DB
  char *core;    // Path prefix to the core; searched in the internal DB
  char *romdir;  // Must be give explicitely at the command line. It must be LOWERCASE .
  char *romext;  // Valid extension for rom filename; searched in the internal DB
} system_t;

static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  { "ALICEMC10",      "EEMDDDOFO",     "/media/fat/_Computer/AliceMC10_",         "/media/fat/games/AliceMC10",    "c10", },
  { "AMSTRAD",        "EEMOFO",        "/media/fat/_Computer/Amstrad_",           "/media/fat/games/Amstrad",      "dsk", },
  { "AMSTRAD-PCW",    "EEMOFO",        "/media/fat/_Computer/Amstrad-PCW_",       "/media/fat/games/Amstrad PCW",  "dsk", },
  { "AMSTRAD-PCW.B",  "EEMDOFO",       "/media/fat/_Computer/Amstrad-PCW_",       "/media/fat/games/Amstrad PCW",  "dsk", },
  { "AMSTRAD.B",      "EEMDOFO",       "/media/fat/_Computer/Amstrad_",           "/media/fat/games/Amstrad",      "dsk", },
  { "AMSTRAD.TAP",    "EEMDDDOFO",     "/media/fat/_Computer/Amstrad_",           "/media/fat/games/Amstrad",      "cdt", },
  { "AO486",          "EEMOFO",        "/media/fat/_Computer/ao486_",             "/media/fat/games/AO486",        "img", },
  { "AO486.B",        "EEMDOFO",       "/media/fat/_Computer/ao486_",             "/media/fat/games/AO486",        "img", },
  { "AO486.C",        "EEMDDOFO",      "/media/fat/_Computer/ao486_",             "/media/fat/games/AO486",        "vhd", },
  { "AO486.D",        "EEMDDDOFO",     "/media/fat/_Computer/ao486_",             "/media/fat/games/AO486",        "vhd", },
  { "APOGEE",         "EEMOFO",        "/media/fat/_Computer/Apogee_",            "/media/fat/games/APOGEE",       "rka", },
  { "APPLE-I",        "EEMOFO",        "/media/fat/_Computer/Apple-I_",           "/media/fat/games/Apple-I",      "txt", },
  { "APPLE-II",       "EEMOFO",        "/media/fat/_Computer/Apple-II_",          "/media/fat/games/Apple-II",     "dsk", },
  { "AQUARIUS.BIN",   "EEMOFO",        "/media/fat/_Computer/Aquarius_",          "/media/fat/games/AQUARIUS",     "bin", },
  { "AQUARIUS.CAQ",   "EEMDOFO",       "/media/fat/_Computer/Aquarius_",          "/media/fat/games/AQUARIUS",     "caq", },
  { "ARCADE",         "OFO",           "/media/fat/menu",                         "/media/fat/_Arcade",            "mra", },
  { "ARCHIE.D1",      "EEMDDDOFO",     "/media/fat/_Computer/Archie_",            "/media/fat/games/ARCHIE",       "vhd", },
  { "ARCHIE.F0",      "EEMOFO",        "/media/fat/_Computer/Archie_",            "/media/fat/games/ARCHIE",       "img", },
  { "ARCHIE.F1",      "EEMDOFO",       "/media/fat/_Computer/Archie_",            "/media/fat/games/ARCHIE",       "img", },
  { "ASTROCADE",      "EEMOFO",        "/media/fat/_Console/Astrocade_",          "/media/fat/games/Astrocade",    "bin", },
  { "ATARI2600",      "EEMOFO",        "/media/fat/_Console/Atari2600_",          "/media/fat/games/ATARI2600",    "rom", },
  { "ATARI2600",      "EEMOFO",        "/media/fat/_Console/Atari2600_",          "/media/fat/games/Astrocade",    "rom", },
  { "ATARI5200",      "EEMOFO",        "/media/fat/_Console/Atari5200_",          "/media/fat/games/ATARI5200",    "rom", },
  { "ATARI800.CART",  "EEMDDOFO",      "/media/fat/_Computer/Atari800_",          "/media/fat/games/ATARI800",     "car", },
  { "ATARI800.D1",    "EEMOFO",        "/media/fat/_Computer/Atari800_",          "/media/fat/games/ATARI800",     "atr", },
  { "ATARI800.D2",    "EEMDOFO",       "/media/fat/_Computer/Atari800_",          "/media/fat/games/ATARI800",     "atr", },
  { "BBCMICRO",       "EEMOFO",        "/media/fat/_Computer/BBCMicro_",          "/media/fat/games/BBCMicro",     "vhd", },
  { "BK0011M",        "EEMOFO",        "/media/fat/_Computer/BK0011M_",           "/media/fat/games/BK0011M",      "bin", },
  { "BK0011M.A",      "EEMDOFO",       "/media/fat/_Computer/BK0011M_",           "/media/fat/games/BK0011M",      "dsk", },
  { "BK0011M.B",      "EEMDDOFO",      "/media/fat/_Computer/BK0011M_",           "/media/fat/games/BK0011M",      "dsk", },
  { "BK0011M.HD",     "EEMDDDOFO",     "/media/fat/_Computer/BK0011M_",           "/media/fat/games/BK0011M",      "vhd", },
  { "C16.CART",       "EEMDOFO",       "/media/fat/_Computer/C16_",               "/media/fat/games/C16",          "bin", },
  { "C16.DISK",       "EEMDDOFO",      "/media/fat/_Computer/C16_",               "/media/fat/games/C16",          "d64", },
  { "C16.TAPE",       "EEMDDDOFO",     "/media/fat/_Computer/C16_",               "/media/fat/games/C16",          "tap", },
  { "C64.CART",       "EEMDDDDOFO",    "/media/fat/_Computer/C64_",               "/media/fat/games/C65",          "crt", },
  { "C64.DISK",       "EEMOFO",        "/media/fat/_Computer/C64_",               "/media/fat/games/C64",          "rom", },
  { "C64.PRG",        "EEMDDDOFO",     "/media/fat/_Computer/C64_",               "/media/fat/games/C64",          "prg", },
  { "C64.TAPE",       "EEMOFO",        "/media/fat/_Computer/C64_",               "/media/fat/games/C64",          "rom", },
  { "COCO_2",         "EEMDDDDDDOFO",  "/media/fat/_Computer/CoCo2_",             "/media/fat/games/CoCo2",        "rom", },
  { "COCO_2.CAS",     "EEMDDDDDDDOFO", "/media/fat/_Computer/CoCo2_",             "/media/fat/games/CoCo2",        "cas", },
  { "COCO_2.CCC",     "EEMDDDDDDOFO",  "/media/fat/_Computer/CoCo2_",             "/media/fat/games/CoCo2",        "ccc", },
  { "COLECO",         "EEMOFO",        "/media/fat/_Console/ColecoVision_",       "/media/fat/games/Coleco",       "col", },
  { "COLECO.SG",      "EEMDOFO",       "/media/fat/_Console/ColecoVision_",       "/media/fat/games/Coleco",       "sg",  },
  { "CUSTOM",         "EEMOFO",        "/media/fat/_Console/NES_",                "/media/fat/games/NES",          "nes", },
  { "EDSAC",          "EEMOFO",        "/media/fat/_Computer/EDSAC_",             "/media/fat/games/EDSAC",        "tap", },
  { "GALAKSIJA",      "EEMOFO",        "/media/fat/_Computer/Galaksija_",         "/media/fat/games/Galaksija",    "tap", },
  { "GAMEBOY",        "EEMOFO",        "/media/fat/_Console/Gameboy_",            "/media/fat/games/GameBoy",      "gb",  },
  { "GAMEBOY.COL",    "EEMOFO",        "/media/fat/_Console/Gameboy_",            "/media/fat/games/GameBoy",      "gbc", },
  { "GBA",            "EEMOFO",        "/media/fat/_Console/GBA_",                "/media/fat/games/GBA",          "gba", },
  { "GENESIS",        "EEMOFO",        "/media/fat/_Console/Genesis_",            "/media/fat/games/Genesis",      "gen", },
  { "JUPITER",        "EEMOFO",        "/media/fat/_Computer/Jupiter_",           "/media/fat/games/Jupiter_",     "ace", },
  { "LASER310",       "EEMOFO",        "/media/fat/_Computer/Laser310_",          "/media/fat/games/Laser310_",    "vz",  },
  { "MACPLUS.2",      "EEMOFO",        "/media/fat/_Computer/MacPlus_",           "/media/fat/games/MACPLUS",      "dsk", },
  { "MACPLUS.VHD",    "EEMDOFO",       "/media/fat/_Computer/MacPlus_",           "/media/fat/games/MACPLUS",      "dsk", },
  { "MEGACD",         "EEMOFO",        "/media/fat/_Console/MegaCD_",             "/media/fat/games/MegaCD",       "cue", },
  { "MSX",            "EEMOFO",        "/media/fat/_Computer/MSX_",               "/media/fat/games/MSX",          "vhd", },
  { "NEOGEO",         "EEMOFO",        "/media/fat/_Console/NeoGeo_",             "/media/fat/games/NeoGeo",       "neo", },
  { "NES",            "EEMOFO",        "/media/fat/_Console/NES_",                "/media/fat/games/NES",          "nes", },
  { "NES.FDSBIOS",    "EEMDOFO",       "/media/fat/_Console/NES_",                "/media/fat/games/NES",          "nes", },
  { "ODYSSEY2",       "EEMOFO",        "/media/fat/_Console/Odyssey2_",           "/media/fat/games/ODYSSEY2",     "bin", },
  { "ORAO",           "EEMOFO",        "/media/fat/_Computer/ORAO_",              "/media/fat/games/ORAO",         "tap", },
  { "ORIC",           "EEMOFO",        "/media/fat/_Computer/Oric_",              "/media/fat/games/Oric_",        "dsk", },
  { "PDP1",           "EEMOFO",        "/media/fat/_Computer/PDP1_",              "/media/fat/games/PDP1",         "bin", },
  { "PET2001",        "EEMOFO",        "/media/fat/_Computer/PET2001_",           "/media/fat/games/PET2001",      "prg", },
  { "PET2001.TAP",    "EEMOFO",        "/media/fat/_Computer/PET2001_",           "/media/fat/games/PET2001",      "tap", },
  { "QL",             "EEMOFO",        "/media/fat/_Computer/QL_",                "/media/fat/games/QL_",          "mdv", },
  { "SAMCOUPE.1",     "EEMOFO",        "/media/fat/_Computer/SAMCoupe_",          "/media/fat/games/SAMCOUPE",     "img", },
  { "SAMCOUPE.2",     "EEMDOFO",       "/media/fat/_Computer/SAMCoupe_",          "/media/fat/games/SAMCOUPE",     "img", },
  { "SMS",            "EEMOFO",        "/media/fat/_Console/SMS_",                "/media/fat/games/SMS",          "sms", },
  { "SNES",           "EEMOFO",        "/media/fat/_Console/SNES_",               "/media/fat/games/SNES",         "sfc", },
  { "SPECIALIST",     "EEMOFO",        "/media/fat/_Computer/Specialist_",        "/media/fat/games/Specialist_",  "rsk", },
  { "SPECIALIST.ODI", "EEMDOFO",       "/media/fat/_Computer/Specialist_",        "/media/fat/games/Specialist_",  "odi", },
  { "SPECTRUM",       "EEMDOFO",       "/media/fat/_Computer/ZX-Spectrum_",       "/media/fat/games/Spectrum",     "tap", },
  { "SPECTRUM.DSK",   "EEMOFO",        "/media/fat/_Computer/ZX-Spectrum_",       "/media/fat/games/Spectrum",     "dsk", },
  { "SPECTRUM.SNAP",  "EEMDDOFO",      "/media/fat/_Computer/ZX-Spectrum_",       "/media/fat/games/Spectrum",     "z80", },
  { "TGFX16.SGX",     "EEMDOFO",       "/media/fat/_Console/TurboGrafx16_",       "/media/fat/games/TGFX16",       "sgx", },
  { "TI-99_4A",       "EEMDOFO",       "/media/fat/_Computer/Ti994a_",            "/media/fat/games/TI-99_4A",     "bin", },
  { "TI-99_4A.D",     "EEMDDOFO",      "/media/fat/_Computer/Ti994a_",            "/media/fat/games/TI-99_4A",     "bin", },
  { "TI-99_4A.G",     "EEMDDOFO",      "/media/fat/_Computer/Ti994a_",            "/media/fat/games/TI-99_4A",     "bin", },
  { "TRS-80",         "EEMOFO",        "/media/fat/_Computer/TRS-80_",            "/media/fat/games/TRS-80_",      "dsk", },
  { "TRS-80.1",       "EEMDOFO",       "/media/fat/_Computer/TRS-80_",            "/media/fat/games/TRS-80_",      "dsk", },
  { "TSCONF",         "EEMOFO",        "/media/fat/_Computer/TSConf_",            "/media/fat/games/TSConf_",      "vhd", },
  { "TURBOGRAFX16",   "EEMOFO",        "/media/fat/_Console/TurboGrafx16_",       "/media/fat/games/TGFX16",       "pce", },
  { "VECTOR06",       "EEMOFO",        "/media/fat/_Computer/Vector-06C_""/core", "/media/fat/games/VECTOR06",     "rom", },
  { "VECTOR06.A",     "EEMDOFO",       "/media/fat/_Computer/Vector-06C_""/core", "/media/fat/games/VECTOR06",     "fdd", },
  { "VECTOR06.B",     "EEMDDOFO",      "/media/fat/_Computer/Vector-06C_""/core", "/media/fat/games/VECTOR06",     "fdd", },
  { "VECTREX",        "EEMOFO",        "/media/fat/_Console/Vectrex_",            "/media/fat/games/VECTREX",      "vec", },
  { "VECTREX.OVR",    "EEMDOFO",       "/media/fat/_Console/Vectrex_",            "/media/fat/games/VECTREX",      "ovr", },
  { "VIC20",          "EEMOFO",        "/media/fat/_Computer/VIC20_",             "/media/fat/games/VIC20",        "prg", },
  { "VIC20.CART",     "EEMDOFO",       "/media/fat/_Computer/VIC20_",             "/media/fat/games/VIC20",        "crt", },
  { "VIC20.CT",       "EEMDDOFO",      "/media/fat/_Computer/VIC20_",             "/media/fat/games/VIC20",        "ct",  },
  { "VIC20.DISK",     "EEMDDDOFO",     "/media/fat/_Computer/VIC20_",             "/media/fat/games/VIC20",        "d64", },
  { "ZX81",           "EEMOFO",        "/media/fat/_Computer/ZX81_",              "/media/fat/games/ZX81",         "0",   },
  { "ZX81.P",         "EEMOFO",        "/media/fat/_Computer/ZX81_",              "/media/fat/games/ZX81",         "p",   },

  // unsupported
  // { "AMIGA",       "EEMOFO",            "/media/fat/_Computer/Minimig_",           "/media/fat/games/Amiga",     0, },
  // { "ATARIST",       "EEMOFO",            "/media/fat/_Computer/AtariST_",           "/media/fat/games/AtariST",     0, },
  // { "AY-3-8500",     0,            "/media/fat/_Console/AY-3-8500_",          "/media/fat/games/AY-3-8500",    0, },
  // { "Altair8800"     , 0 , 0, 0, 0, },
  // { "MULTICOMP",     0,            "/media/fat/_Computer/MultiComp_",         "/media/fat/games/MultiComp",   0, },
  // { "MultiComp"      , 0 , 0, 0, 0, },
  // { "SHARPMZ",       "EEMOFO",            "/media/fat/_Computer/SharpMZ_",           "/media/fat/games/SharpMZ_",     0, },
  // { "X68000"         , 0 , 0, 0, 0, },
};

int core_wait = 3000; // ms
int inter_key_wait = 50; // ms
int sequence_wait = 1000; // ms

static void emulate_key(int fd, int key) {
  msleep(inter_key_wait);
  ev_emit(fd, EV_KEY, key, 1);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
  msleep(50);
  ev_emit(fd, EV_KEY, key, 0);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static int emulate_sequence(char* seq) {

  int fd = ev_open();
  if (fd < 0) {
    return -1;
  }

  // Wait that userspace detects the new device
  msleep(sequence_wait);

  // Emulate sequence
  for (int i=0; i<strlen(seq); i++) {
    switch (seq[i]) {
      default:
        close(fd);
        return -1;

      break;case 'U': emulate_key(fd, KEY_UP);    // 103 0x67 up
      break;case 'D': emulate_key(fd, KEY_DOWN);  // 108 0x6c down
      break;case 'L': emulate_key(fd, KEY_LEFT);  // 105 0x69 left
      break;case 'R': emulate_key(fd, KEY_RIGHT); // 106 0x6a right
      break;case 'O': emulate_key(fd, KEY_ENTER); //  28 0x1c enter (Open)
      break;case 'E': emulate_key(fd, KEY_ESC);   //   1 0x01 esc
      break;case 'H': emulate_key(fd, KEY_HOME);  // 102 0x66 home
      break;case 'F': emulate_key(fd, KEY_END);   // 107 0x6b end (Finish)
      break;case 'M': emulate_key(fd, KEY_F12);   //  88 0x58 f12 (Menu)
    }
  }

  // Wait that userspace detects all the events
  msleep(sequence_wait);

  ev_close(fd);

  return 0;
}

static char* after_string(char* str, char delim) {
  for (char* curr = str; '\0' != *curr; curr += 1){
    if (delim == *curr) {
      str = curr+1;
    }
  }
  return str;
}

static void  get_core_name(char* corepath, char* out, int size) {

  char* start = after_string(corepath, '/');
  if (NULL == start) {
    return;
  }

  char* end = after_string(start, '_');
  end -= 1;
  int len = end - start;
  if (len <= 0) {
    return;
  }

  size -= 1;
  if (size > len) size = len;
  strncpy(out, start, size);
  out[size] = '\0';

  for (int i = 0; i < len; i++){
    out[i] = toupper(out[i]);
  }
}

static int cmp_char_ptr_field(const void * a, const void * b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

static system_t* get_system(char* corepath, char * name) {

  char system[64] = {0, };

  if (NULL == name) {
    get_core_name(corepath, system, ARRSIZ(system));
    if ('\0' == system[0]) {
      return NULL;
    }
    name = system;
  }

  system_t target = {name, 0};
  return (system_t*) SBSEARCH(&target, system_list, cmp_char_ptr_field);
}

static int load_core(system_t* sys, char* corepath) {

  if (NULL == sys) {
    PRINTERR("%s\n", "invalid system");
    return -1;
  }
  
  FILE* f = fopen(MISTER_COMMAND_DEVICE, "wb");
  if (0 == f) {
    PRINTERR("%s\n", MISTER_COMMAND_DEVICE);
    return -1;
  }
 
  // TODO : check that a file exists at corepath ? 
  
  int ret = fprintf(f, "load_core %s\n", corepath);
  if (0 > ret){
    return -1;
  }

  fclose(f);
  return 0;
}

static int rom_unlink_by_path(system_t* sys, char* rompath) {
  return unlink(rompath);
}

static int get_aux_rom_path(system_t* sys, char* out, size_t len){
  return snprintf(out, len, "%s/%s.%s", sys->romdir, AUX_ROM_NAME, sys->romext);
}

static int rom_unlink(system_t* sys) {
  char rompath[4096] = {0};
  get_aux_rom_path(sys, rompath, sizeof(rompath));
  return unlink(rompath);
}

static int rom_link(system_t* sys, char* path) {

  // if (mkparent_dup(path, 0777) < 0) return -1;

  char rompath[4096] = {0};
  get_aux_rom_path(sys, rompath, sizeof(rompath));

  rom_unlink_by_path(sys, rompath); // It is expected that this fails in some cases
  
  int ret = symlink(path, rompath);
  if (0 != ret) {
    PRINTERR("Can not link %s -> %s\n", rompath, path);
    return -1;
  }

  return 0;
}

static int emulate_system_sequence(system_t* sys) {
  if (NULL == sys) {
    return -1;
  }
  return emulate_sequence(sys->menuseq);
}

static int load_rom(system_t* sys, char* rom) {

  int ret = rom_link(sys, rom);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not bind the rom");
    rom_unlink(sys);
    return -1;
  }

  ret = emulate_system_sequence(sys);
  if (0 > ret) {
    PRINTERR("%s\n", "Error during key emulation");
    rom_unlink(sys);
    return -1;
  }

  ret = rom_unlink(sys);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not unbind the rom");
    return -1;
  }

  return 0;
}

static int has_ext(char* name, char* ext){
  char* name_ext = after_string(name, '.');
  if (name_ext == name) return 0;
  while (1) {
    if (tolower(*ext) != tolower(*name_ext)) return 0;
    if (*name_ext != '\0') break;
    ext += 1;
    name_ext += 1;
  }
  return 1;
}

static int resolve_core_path(char* path, char* out, int len){
  strncpy(out, path, len);
  char* name = after_string(out, '/');
  if (*name == '\0' || name - out < 3) {
    return -1;
  }
  name[-1] = '\0';
  int matched = 0;
  struct dirent* ep = NULL;
  DIR* dp = opendir(out);
  if (dp != NULL) {
    int nlen = strlen(name);
    while (0 != (ep = readdir (dp))){
      if (!strncmp(name, ep->d_name, nlen) && has_ext(ep->d_name, CORE_EXT)){
        matched = 1;
        snprintf(out+strlen(out), len-strlen(out)-1, "/%s", ep->d_name);
        break;
      }
    }
  }
  if (!matched) {
    return -1;
  }
  return 0;
}

static int list_core(){
  for (int i=0; i<ARRSIZ(system_list); i++){
    int plen = 64 + strlen(system_list[i].core);
    char corepath[plen];
    if (0 != resolve_core_path(system_list[i].core, corepath, plen)){
      printf("#%s can not be found with prefix %s\n", system_list[i].id, system_list[i].core);
    } else {
      printf("%s %s\n", system_list[i].id, corepath);
    }
  }
}

static int load_rom_autocore(system_t* sys, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  int plen = 64 + strlen(sys->core);
  char corepath[plen];

  if (resolve_core_path(sys->core, corepath, plen)){
    PRINTERR("Can not find the core at %s\n", sys->core);
    return -1;
  }

  int ret = load_core(sys, corepath);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not load the core");
    return -1;
  }

  msleep(core_wait);

  return load_rom(sys, rom);
}

static int load_core_and_rom(system_t* sys, char* corepath, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  int ret = load_core(sys, corepath);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not load the core");
    return -1;
  }

  msleep(core_wait);

  return load_rom(sys, rom);
}

struct cmdentry {
  const char * name;
  void (*cmd)(int argc, char** argv);
};

int checkarg(int min, int val){
  if (val >= min+1) return 1;
  PRINTERR("At least %d arguments are needed\n", min);
  return 0;
}

int list_content_for(system_t* sys){
  DIR *dp;
  struct dirent *ep;
  int something_found = 0;

  int elen = strlen(sys->romext);
  dp = opendir(sys->romdir);
  if (dp != NULL) {
    while (0 != (ep = readdir (dp))){

      if (has_ext(ep->d_name, sys->romext)){
        something_found = 1;
        printf("%s %s/%s\n", sys->id, sys->romdir, ep->d_name);
      }
    }
    closedir(dp);
  }
  if (!something_found) {
    //printf("#%s no '.%s' files found in %s\n", sys->id, sys->romext, sys->romdir);
  }
  return something_found;
}

int list_content(){
  for (int i=0; i<ARRSIZ(system_list); i++){
    list_content_for(system_list+i);
  }
  return 0;
}

static int stream_mode();

// command list
static void cmd_exit(int argc, char** argv)         { exit(0); }
static void cmd_stream_mode(int argc, char** argv)  { stream_mode(); }
static void cmd_load_core(int argc, char** argv)    { if(checkarg(1,argc))load_core(get_system(argv[1],NULL),argv[1]); }
static void cmd_load_core_as(int argc, char** argv) { if(checkarg(2,argc))load_core(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_rom(int argc, char** argv)     { if(checkarg(2,argc))load_rom(get_system(NULL,argv[1]),argv[2]); }
static void cmd_list_core(int argc, char** argv)    { list_core(); }
static void cmd_rom_autocore(int argc, char** argv) { if(checkarg(2,argc))load_rom_autocore(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_all(int argc, char** argv)     { if(checkarg(2,argc))load_core_and_rom(get_system(argv[1],NULL),argv[1],argv[2]); }
static void cmd_load_all_as(int argc, char** argv)  { if(checkarg(3,argc))load_core_and_rom(get_system(NULL,argv[1]),argv[2],argv[3]); }
static void cmd_rom_link(int argc, char** argv)     { if(checkarg(2,argc))rom_link(get_system(NULL,argv[1]),argv[2]); }
static void cmd_raw_seq(int argc, char** argv)      { if(checkarg(1,argc))emulate_sequence(argv[1]); }
static void cmd_select_seq(int argc, char** argv)   { if(checkarg(1,argc))emulate_system_sequence(get_system(NULL,argv[1])); }
static void cmd_rom_unlink(int argc, char** argv)   { if(checkarg(1,argc))rom_unlink(get_system(NULL,argv[1])); }
static void cmd_list_content(int argc, char** argv) { list_content(); }
static void cmd_list_rom_for(int argc, char** argv) { if(checkarg(1,argc))list_content_for(get_system(NULL,argv[1])); }
//
struct cmdentry cmdlist[] = {
  //
  // The "name" field can not contain ' ' or '\0'.
  // The array must be lexicographically sorted wrt "name" field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
  //
  {"done"         , cmd_exit         , } ,
  {"list_content" , cmd_list_content , } ,
  {"list_core"    , cmd_list_core    , } ,
  {"list_rom_for" , cmd_list_rom_for , } ,
  {"load_all"     , cmd_load_all     , } ,
  {"load_all_as"  , cmd_load_all_as  , } ,
  {"load_core"    , cmd_load_core    , } ,
  {"load_core_as" , cmd_load_core_as , } ,
  {"load_rom"     , cmd_rom_autocore , } ,
  {"load_rom_only", cmd_load_rom     , } ,
  {"raw_seq"      , cmd_raw_seq      , } ,
  {"rom_link"     , cmd_rom_link     , } ,
  {"rom_unlink"   , cmd_rom_unlink   , } ,
  {"select_seq"   , cmd_select_seq   , } ,
  {"stream"       , cmd_stream_mode  , } ,
};

static int run_command(int narg, char** args) {

  // DEBUG
  // printf("%d arguments command\n", narg);
  // for (int a=0; a<narg; a+=1){
  //   printf("arg %d] %s\n", a, args[a]);
  // }
 
  // match the command
  struct cmdentry target = {args[0], 0};
  struct cmdentry * command = (struct cmdentry*)
    SBSEARCH(&target, cmdlist, cmp_char_ptr_field);

  // call the command
  if (NULL == command || NULL == command->cmd) {
    PRINTERR("%s\n", "unknown command");
    return -1;
  }
  command->cmd(narg, args);
  return 0;
}

static int stream_mode() {
  char* line = NULL;
  size_t size = 0;
  while (1) {

    // read line
    int len = getline(&line, &size, stdin);
    if (-1 == len) {
      break;
    }
    len = len-1;
    line[len] = '\0';

    // Split command in arguments
    int narg = 0;
    char *args[5] = {0,};
    int matchnew = 1;
    for (int i=0; i<size; i++){
      if ('\0' == line[i] || '\n' == line[i]) {
        line[i] = '\0';
        break;
      }
      if (' ' == line[i] || '\t' == line[i]) {
        line[i] = '\0';
        matchnew = 1;
      } else if (matchnew) {
        args[narg] = line+i;
        narg += 1;
        matchnew = 0;
      }
    }
    if (0 == narg) {
      continue;
    }

    // do it
    run_command(narg, args);
  }
  if (line) free(line);
  return 0;
}

static void read_options(int argc, char* argv[]) {

  char* val;

  system_t* custom_system = get_system(NULL, "CUSTOM");
  if (NULL == custom_system){
    printf("no CUSTOM system record: CUSTOM can not be used\n");
  } else {

    // Note: no need to free the duplicated string since they last until the end of the run
    val = getenv("MBC_CUSTOM_CORE");
    if (NULL != val && val[0] != '\0') custom_system->core = strdup(val);
    val = getenv("MBC_CUSTOM_ROM_PATH");
    if (NULL != val && val[0] != '\0') custom_system->romdir = strdup(val);
    val = getenv("MBC_CUSTOM_ROM_EXT");
    if (NULL != val && val[0] != '\0') custom_system->romext = strdup(val);
    val = getenv("MBC_CUSTOM_SEQUENCE");
    if (NULL != val && val[0] != '\0') custom_system->menuseq = strdup(val);
  }

  val = getenv("MBC_CORE_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      core_wait = i;
    } else {
      printf("invalid core wait option from environment; fallling back to %d ms\n", core_wait);
    }
  }

  val = getenv("MBC_KEY_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      inter_key_wait = i;
    } else {
      printf("invalid key wait option from environment; fallling back to %d ms\n", inter_key_wait);
    }
  }

  val = getenv("MBC_SEQUENCE_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      sequence_wait = i;
    } else {
      printf("invalid sequence wait option from environment; fallling back to %d ms\n", sequence_wait);
    }
  }
}

static void print_help(char* name) {
  printf("Usage:\n");
  printf("  %s COMMAND [ARGS]\n", name);
  printf("\n");
  printf("E.g.:\n");
  printf("  %s load_rom NES /media/fat/NES/*.nes\n", name);
  printf("\n");
  printf("Supported COMMAND:");
  for (int i=0; i<ARRSIZ(cmdlist); i++){
    if (0 != i) printf(",");
    printf(" %s", cmdlist[i].name);
  }
  printf("\n");
  printf("\n");
  printf("Please refer to the Readme for further infromation: https://github.com/pocomane/MiSTer_Batch_Control\n");
}

int main(int argc, char* argv[]) {

  if (2 > argc) {
    print_help(argv[0]);
    return 0;
  }

  read_options(argc, argv);
    
  return run_command(argc-1, argv+1);
}

