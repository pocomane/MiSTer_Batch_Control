#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <linux/uinput.h>
#include <sys/inotify.h>
#include <poll.h>

#define DEVICE_NAME "Fake device"
#define DEVICE_PATH "/dev/uinput"
#define MISTER_COMMAND_DEVICE "/dev/MiSTer_cmd"
#define MBC_LINK_NAM "~~~"
#define CORE_EXT "rbf"
#define MBCSEQ "HDOFO"
#define ROMSUBLINK " !MBC"

#define HAS_PREFIX(P, N) (!strncmp(P, N, sizeof(P)-1)) // P must be a string literal

#define ARRSIZ(A) (sizeof(A)/sizeof(A[0]))
#define LOG(F,...) printf("%d - " F, __LINE__, __VA_ARGS__ )
#define PRINTERR(F,...) LOG("error - %s - " F, strerror(errno ? errno : EPERM), __VA_ARGS__ )
#define SBSEARCH(T, SA, C)	(bsearch(T, SA, sizeof(SA)/sizeof(SA[0]), sizeof(SA[0]), (C)))

#ifdef FAKE_OS_OPERATION
#define nanosleep(m,n) (LOG("nanosleep %ld %ld\n", (m)->tv_sec, (m)->tv_nsec), 0)
#define open(p,b)      (LOG("open '%s'\n", p), fake_fd(p))
#define ioctl(f,...)   (0)
#define write(f,d,s)   (LOG("write '%s' <-",f),hex_write(d,s),printf("\n"), 0)
#define close(f)       (LOG("close '%s'\n",fake_fd_name(f)), 0)
#define mkdir(p,m)     (LOG("mkdir '%s'\n",p), 0)
#define poll(p,n,m)    (LOG("%s\n","poll"), 0)
#define read(f,d,s)    (LOG("read '%s'\n",fake_fd_name(f)), 0)
#define inotify_init() (LOG("%s\n","inotifyinit"), 0)
#define inotify_add_watch(f,d,o)   (LOG("inotifyadd '%s'\n",fake_fd_name(f)), 0)
//#define opendir(p)     (LOG("opendir '%s'\n",p), fake_fd(p))
//#define readdir(f)     (LOG("readdir '%s'\n",fake_fd_name(f)), 0)
#define closedir(f)    (LOG("closedir '%s'\n",fake_fd_name(f)), 0)
#define inotify_rm_watch(f,w)   (LOG("inotifyrm '%s'\n",fake_fd_name(f)), 0)
#define fopen(p,o)     (LOG("fopen '%s'\n",p), fake_file(p))
#define fprintf(f,m,d) (LOG("fprintf '%s' <- '%s'\n",fake_file_name(f),m), 0)
#define fclose(f)      (LOG("fclose '%s'\n",fake_file_name(f)), 0)
#define mount(s,t,x,o, y)    (LOG("mount '%s' -> '%s'\n",s,t), 0)
#define umount(p)      (LOG("umount '%s'\n",p), 0)
#define remove(p)      (LOG("remove '%s'\n",p), 0)
#define rmdir(p)       (LOG("rmdir '%s'\n",p), 0)
//#define stat(p,s)      (LOG("stat '%s'\n",p), stat(p,s))
//#define getenv(k)      (LOG("getenv '%s'\n",k),getenv(k))
#undef S_ISREG
#define S_ISREG(s)      (1)
static int   fake_fd(const char* name)    { return (int)(char*)strdup(name); }
static char* fake_fd_name(int fd)         { return (char*)fd; }
static FILE* fake_file(const char* name)  { return (FILE*)(char*)strdup(name); }
static char* fake_file_name(FILE* f)      { return (char*)f; }
static void  hex_write(const char*b,int s){ for(int i=0;i<s;i+=1)printf(" %02x",b[i]); }
#endif // FAKE_OS_OPERATION

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

static void path_parentize(char* path){
  for (int i = strlen(path); i > 0; i--)
    if (path[i] == '/') {
      path[i] = '\0';
      break;
    }
}

static int is_dir(const char* path){
  struct stat st;
  int stat_error = stat(path, &st);
  if (!stat_error && S_ISDIR(st.st_mode))
    return 1;
  return 0;
}

static int mkdir_core(const char* path, mode_t mode){

  int created = !mkdir(path, mode);
  if (created) return 0;

  // no error if a directory already exist
  if (!created && errno == EEXIST && is_dir(path))
    return 0;

  return -1;
}

static int mkparent_core(char *path, mode_t mode) {
  int depth = 0;
  char *curr = path, *found = 0;

  while (0 != (found = strchr(curr, '/'))) {
    if (found != curr) { // skip root or double slashes in path
      depth += 1;

      *found = '\0';
      int err = mkdir_core(path, mode);
      *found = '/';

      if (err) return -depth;
    }
    curr = found + 1;
  }
  return 0;
}

static int mkparent(const char *path, mode_t mode) {
  char *pathdup = strdup(path);
  if (!pathdup) return -1;
  int status = mkparent_core(pathdup, mode);
  free(pathdup);
  return status;
}

static int mkdirpath(const char *path, mode_t mode) {
  if (is_dir(path)) return 0;
  int result = mkparent(path, mode);
  if (result) return result;
  return mkdir_core(path, mode);
}

typedef struct {
  int watcher;
  int file_n;
  struct pollfd pool[99];
} input_monitor;

int user_input_poll(input_monitor*monitor, int ms){
  int result = poll(monitor->pool, monitor->file_n, ms);
  if (0> result) return -4; // Poll error
  return result;
}

int user_input_clear(input_monitor*monitor){
  struct input_event ev;
  int count = 0;
  while (0< user_input_poll(monitor, 1))
    for (int f = 0; f < monitor->file_n; f += 1)
      if (monitor->pool[f].revents & POLLIN)
        if (0< read(monitor->pool[f].fd, &ev, sizeof(ev)))
          count += 1;
  return count;
}

int user_input_open(input_monitor*monitor){
  const char folder[] = "/dev/input";

  monitor->watcher = -1;
  monitor->file_n = 0;

  int inotify_fd = inotify_init();
  if (inotify_fd == -1) return -1; // Inotify error

  monitor->pool[monitor->file_n].fd = inotify_fd;
  monitor->pool[monitor->file_n].events = POLLIN;
  monitor->file_n += 1;

  monitor->watcher = inotify_add_watch( inotify_fd, folder, IN_CREATE | IN_DELETE );
  if (monitor->watcher == -1) return -2; // Watcher error

  struct dirent* dir = NULL;
  DIR* dirinfo = opendir(folder);
  if (dirinfo != NULL) {
    while (0 != (dir = readdir (dirinfo))){
      if (HAS_PREFIX("event", dir->d_name)){

        char out[sizeof(folder)+strlen(dir->d_name)+9];
        snprintf(out, sizeof(out)-1, "%s/%s", folder,dir->d_name);
        struct stat fileinfo;
        if (stat(out, &fileinfo) == 0 && !S_ISDIR(fileinfo.st_mode) && !S_ISREG(fileinfo.st_mode)) {

          if (monitor->file_n >= sizeof(monitor->pool)/sizeof(*(monitor->pool))) break;
          int fd = open(out,O_RDONLY|O_NONBLOCK);
          if (fd <= 0) return -3; // Open error

          monitor->pool[monitor->file_n].fd = fd;
          monitor->pool[monitor->file_n].events = POLLIN;
          monitor->file_n += 1;
        }
      }
    }
    closedir(dirinfo);
  }

  while(0< user_input_clear(monitor)); // drop old events

  return 0;
}

int is_user_input_event(int code){ return (code>0); }
int is_user_input_timeout(int code){ return (code==0); }

void user_input_close(input_monitor*monitor){

  if (monitor->watcher >= 0)
    inotify_rm_watch(monitor->pool[0].fd, monitor->watcher);

  for (int f = 0; f < monitor->file_n; f += 1)
    if (monitor->pool[f].fd > 0) close(monitor->pool[f].fd);
  monitor->file_n = 0;
}

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE without any space.
  char *menuseq; // Sequence of input for the rom selection; searched in the internal DB
  char *core;    // Path prefix to the core; searched in the internal DB
  char *fsid;    // Name used in the filesystem to identify the folders of the system; if it starts with something different than '/', it will identify a subfolder of a default path
  char *romext;  // Valid extension for rom filename; searched in the internal DB
  char *sublink; // If not NULL, the auxiliary rom link will be made in the specified path, instead of default one
} system_t;

static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  { "ALICEMC10",      "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/AliceMC10_",         "AliceMC10",    "c10", },
  { "AMSTRAD",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", },
  { "AMSTRAD-PCW",    "EEMO" MBCSEQ,        "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", },
  { "AMSTRAD-PCW.B",  "EEMDO" MBCSEQ,       "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", },
  { "AMSTRAD.B",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", },
  { "AMSTRAD.TAP",    "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/Amstrad_",           "Amstrad",      "cdt", },
  { "AO486",          "EEMO" MBCSEQ,        "/media/fat/_Computer/ao486_",             "AO486",        "img", },
  { "AO486.B",        "EEMDO" MBCSEQ,       "/media/fat/_Computer/ao486_",             "AO486",        "img", },
  { "AO486.C",        "EEMDDO" MBCSEQ,      "/media/fat/_Computer/ao486_",             "AO486",        "vhd", },
  { "AO486.D",        "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/ao486_",             "AO486",        "vhd", },
  { "APOGEE",         "EEMO" MBCSEQ,        "/media/fat/_Computer/Apogee_",            "APOGEE",       "rka", },
  { "APPLE-I",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Apple-I_",           "Apple-I",      "txt", },
  { "APPLE-II",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Apple-II_",          "Apple-II",     "dsk", },
  { "AQUARIUS.BIN",   "EEMO" MBCSEQ,        "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "bin", },
  { "AQUARIUS.CAQ",   "EEMDO" MBCSEQ,       "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "caq", },
  { "ARCADE",         "O" MBCSEQ,           "/media/fat/menu",                         "/media/fat/_Arcade", "mra", "/media/fat/_Arcade/_ !MBC/~~~.mra"},
  { "ARCHIE.D1",      "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/Archie_",            "ARCHIE",       "vhd", },
  { "ARCHIE.F0",      "EEMO" MBCSEQ,        "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", },
  { "ARCHIE.F1",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", },
  { "ASTROCADE",      "EEMO" MBCSEQ,        "/media/fat/_Console/Astrocade_",          "Astrocade",    "bin", },
  { "ATARI2600",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari2600_",          "ATARI2600",    "rom", },
  { "ATARI2600",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari2600_",          "Astrocade",    "rom", },
  { "ATARI5200",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari5200_",          "ATARI5200",    "rom", },
  { "ATARI800.CART",  "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Atari800_",          "ATARI800",     "car", },
  { "ATARI800.D1",    "EEMO" MBCSEQ,        "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", },
  { "ATARI800.D2",    "EEMDO" MBCSEQ,       "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", },
  { "BBCMICRO",       "EEMO" MBCSEQ,        "/media/fat/_Computer/BBCMicro_",          "BBCMicro",     "vhd", },
  { "BK0011M",        "EEMO" MBCSEQ,        "/media/fat/_Computer/BK0011M_",           "BK0011M",      "bin", },
  { "BK0011M.A",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", },
  { "BK0011M.B",      "EEMDDO" MBCSEQ,      "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", },
  { "BK0011M.HD",     "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/BK0011M_",           "BK0011M",      "vhd", },
  { "C16.CART",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/C16_",               "C16",          "bin", },
  { "C16.DISK",       "EEMDDO" MBCSEQ,      "/media/fat/_Computer/C16_",               "C16",          "d64", },
  { "C16.TAPE",       "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/C16_",               "C16",          "tap", },
  { "C64.CART",       "EEMDDDDO" MBCSEQ,    "/media/fat/_Computer/C64_",               "C65",          "crt", },
  { "C64.DISK",       "EEMO" MBCSEQ,        "/media/fat/_Computer/C64_",               "C64",          "rom", },
  { "C64.PRG",        "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/C64_",               "C64",          "prg", },
  { "C64.TAPE",       "EEMO" MBCSEQ,        "/media/fat/_Computer/C64_",               "C64",          "rom", },
  { "COCO_2",         "EEMDDDDDDO" MBCSEQ,  "/media/fat/_Computer/CoCo2_",             "CoCo2",        "rom", },
  { "COCO_2.CAS",     "EEMDDDDDDDO" MBCSEQ, "/media/fat/_Computer/CoCo2_",             "CoCo2",        "cas", },
  { "COCO_2.CCC",     "EEMDDDDDDO" MBCSEQ,  "/media/fat/_Computer/CoCo2_",             "CoCo2",        "ccc", },
  { "COLECO",         "EEMO" MBCSEQ,        "/media/fat/_Console/ColecoVision_",       "Coleco",       "col", },
  { "COLECO.SG",      "EEMDO" MBCSEQ,       "/media/fat/_Console/ColecoVision_",       "Coleco",       "sg",  },
  { "CUSTOM",         "EEMO" MBCSEQ,        "/media/fat/_Console/NES_",                "NES",          "nes", },
  { "EDSAC",          "EEMO" MBCSEQ,        "/media/fat/_Computer/EDSAC_",             "EDSAC",        "tap", },
  { "GALAKSIJA",      "EEMO" MBCSEQ,        "/media/fat/_Computer/Galaksija_",         "Galaksija",    "tap", },
  { "GAMEBOY",        "EEMO" MBCSEQ,        "/media/fat/_Console/Gameboy_",            "GameBoy",      "gb",  },
  { "GAMEBOY.COL",    "EEMO" MBCSEQ,        "/media/fat/_Console/Gameboy_",            "GameBoy",      "gbc", },
  { "GBA",            "EEMO" MBCSEQ,        "/media/fat/_Console/GBA_",                "GBA",          "gba", },
  { "GENESIS",        "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "gen",  },
  { "JUPITER",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Jupiter_",           "Jupiter_",     "ace", },
  { "LASER310",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Laser310_",          "Laser310_",    "vz",  },
  { "MACPLUS.2",      "EEMO" MBCSEQ,        "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", },
  { "MACPLUS.VHD",    "EEMDO" MBCSEQ,       "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", },
  { "MEGACD",         "EEMO" MBCSEQ,        "/media/fat/_Console/MegaCD_",             "MegaCD",       "chd", },
  //{ "MEGACD.CUE",     "EEMO" MBCSEQ,        "/media/fat/_Console/MegaCD_",             "MegaCD",       "cue", },
  { "MEGADRIVE",      "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "md",  },
  { "MEGADRIVE.BIN",  "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "bin",  },
  { "MSX",            "EEMO" MBCSEQ,        "/media/fat/_Computer/MSX_",               "MSX",          "vhd", },
  { "NEOGEO",         "EEMO" MBCSEQ,        "/media/fat/_Console/NeoGeo_",             "NeoGeo",       "neo", },
  { "NES",            "EEMO" MBCSEQ,        "/media/fat/_Console/NES_",                "NES",          "nes", },
  { "NES.FDS",        "EEMDO" MBCSEQ,       "/media/fat/_Console/NES_",                "NES",          "nes", },
  { "ODYSSEY2",       "EEMO" MBCSEQ,        "/media/fat/_Console/Odyssey2_",           "ODYSSEY2",     "bin", },
  { "ORAO",           "EEMO" MBCSEQ,        "/media/fat/_Computer/ORAO_",              "ORAO",         "tap", },
  { "ORIC",           "EEMO" MBCSEQ,        "/media/fat/_Computer/Oric_",              "Oric_",        "dsk", },
  { "PDP1",           "EEMO" MBCSEQ,        "/media/fat/_Computer/PDP1_",              "PDP1",         "bin", },
  { "PET2001",        "EEMO" MBCSEQ,        "/media/fat/_Computer/PET2001_",           "PET2001",      "prg", },
  { "PET2001.TAP",    "EEMO" MBCSEQ,        "/media/fat/_Computer/PET2001_",           "PET2001",      "tap", },
  { "QL",             "EEMO" MBCSEQ,        "/media/fat/_Computer/QL_",                "QL_",          "mdv", },
  { "SAMCOUPE.1",     "EEMO" MBCSEQ,        "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", },
  { "SAMCOUPE.2",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", },
  { "SCRIPT",         "EDDDODOHDO",         "/media/fat/menu",                         "/media/fat/_Scripts", "sh", "/media/fat/Scripts/~~~.sh" },
  { "SMS",            "EEMO" MBCSEQ,        "/media/fat/_Console/SMS_",                "SMS",          "sms", },
  { "SNES",           "EEMO" MBCSEQ,        "/media/fat/_Console/SNES_",               "SNES",         "sfc", },
  { "SPECIALIST",     "EEMO" MBCSEQ,        "/media/fat/_Computer/Specialist_",        "Specialist_",  "rsk", },
  { "SPECIALIST.ODI", "EEMDO" MBCSEQ,       "/media/fat/_Computer/Specialist_",        "Specialist_",  "odi", },
  { "SPECTRUM",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "tap", },
  { "SPECTRUM.DSK",   "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "dsk", },
  { "SPECTRUM.SNAP",  "EEMDDO" MBCSEQ,      "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "z80", },
  { "SUPERGRAFX",     "EEMDO" MBCSEQ,       "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "sgx", },
  { "TGFX16",         "EEMO" MBCSEQ,        "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "pce", },
  { "TGFX16-CD",      "EEMDDO" MBCSEQ,      "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "chd", },
  //{ "TGFX16-CD.CUE",  "EEMDDO" MBCSEQ,      "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "cue", },
  { "TI-99_4A",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TI-99_4A.D",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TI-99_4A.G",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TRS-80",         "EEMO" MBCSEQ,        "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", },
  { "TRS-80.1",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", },
  { "TSCONF",         "EEMO" MBCSEQ,        "/media/fat/_Computer/TSConf_",            "TSConf_",      "vhd", },
  { "VECTOR06",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "rom", },
  { "VECTOR06.A",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", },
  { "VECTOR06.B",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", },
  { "VECTREX",        "EEMO" MBCSEQ,        "/media/fat/_Console/Vectrex_",            "VECTREX",      "vec", },
  { "VECTREX.OVR",    "EEMDO" MBCSEQ,       "/media/fat/_Console/Vectrex_",            "VECTREX",      "ovr", },
  { "VIC20",          "EEMO" MBCSEQ,        "/media/fat/_Computer/VIC20_",             "VIC20",        "prg", },
  { "VIC20.CART",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/VIC20_",             "VIC20",        "crt", },
  { "VIC20.CT",       "EEMDDO" MBCSEQ,      "/media/fat/_Computer/VIC20_",             "VIC20",        "ct",  },
  { "VIC20.DISK",     "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/VIC20_",             "VIC20",        "d64", },
  { "ZX81",           "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX81_",              "ZX81",         "0",   },
  { "ZX81.P",         "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX81_",              "ZX81",         "p",   },

  // unsupported
  //{ "AMIGA",          "EEMO" MBCSEQ,        "/media/fat/_Computer/Minimig_",           "/media/fat/games/Amiga",     0, },
  //{ "ATARIST",        "EEMO" MBCSEQ,        "/media/fat/_Computer/AtariST_",           "/media/fat/games/AtariST",     0, },
  //{ "AY-3-8500",      0,                    "/media/fat/_Console/AY-3-8500_",          "/media/fat/games/AY-3-8500",    0, },
  //{ "Altair8800"      , 0 , 0, 0, 0, },
  //{ "MULTICOMP",      0,                    "/media/fat/_Computer/MultiComp_",         "/media/fat/games/MultiComp",   0, },
  //{ "MultiComp"       , 0 , 0, 0, 0, },
  //{ "SHARPMZ",        "EEMO" MBCSEQ,        "/media/fat/_Computer/SharpMZ_",           "/media/fat/games/SharpMZ_",     0, },
  //{ "X68000"          , 0 , 0, 0, 0, },
};

int core_wait = 3000; // ms
int inter_key_wait = 40; // ms
int sequence_wait = 1000; // ms

static void emulate_key_press(int fd, int key) {
  msleep(inter_key_wait);
  ev_emit(fd, EV_KEY, key, 1);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void emulate_key_release(int fd, int key) {
  msleep(inter_key_wait);
  ev_emit(fd, EV_KEY, key, 0);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void emulate_key(int fd, int key) {
  emulate_key_press(fd, key);
  emulate_key_release(fd, key);
}

#define TAG_KEY_PRESS   '{'
#define TAG_KEY_RELEASE '}'
#define TAG_KEY_FULL    ':'

static char* parse_hex_byte(char* seq, int* code, int* tag){
  int c, n;
  if (0> sscanf(seq, "%2x%n", &c, &n) || 2!= n) return 0;
  if (code) *code = c;
  if (tag) *tag = TAG_KEY_FULL;
  return seq+n;
}

static char* parse_tagged_byte(char* seq, int* code, int* tag){
  if (tag) *tag = *seq; // any single char tag is valid (action support is checked by the caller)
  if (seq[1] == '\0') return 0;
  return parse_hex_byte(seq+1, code, 0);
}

static char* parse_alphanumeric_key(char* seq, int* code, int* tag){
  int i = 0; if (!code) code = &i;
  switch (*seq) {
    default: return 0;

    break;case '0': *code = KEY_0;
    break;case '1': *code = KEY_1;
    break;case '2': *code = KEY_2;
    break;case '3': *code = KEY_3;
    break;case '4': *code = KEY_4;
    break;case '5': *code = KEY_5;
    break;case '6': *code = KEY_6;
    break;case '7': *code = KEY_7;
    break;case '8': *code = KEY_8;
    break;case '9': *code = KEY_9;
    break;case 'a': *code = KEY_A;
    break;case 'b': *code = KEY_B;
    break;case 'c': *code = KEY_C;
    break;case 'd': *code = KEY_D;
    break;case 'e': *code = KEY_E;
    break;case 'f': *code = KEY_F;
    break;case 'g': *code = KEY_G;
    break;case 'h': *code = KEY_H;
    break;case 'i': *code = KEY_I;
    break;case 'j': *code = KEY_J;
    break;case 'k': *code = KEY_K;
    break;case 'l': *code = KEY_L;
    break;case 'm': *code = KEY_M;
    break;case 'n': *code = KEY_N;
    break;case 'o': *code = KEY_O;
    break;case 'p': *code = KEY_P;
    break;case 'q': *code = KEY_Q;
    break;case 'r': *code = KEY_R;
    break;case 's': *code = KEY_S;
    break;case 't': *code = KEY_T;
    break;case 'u': *code = KEY_U;
    break;case 'v': *code = KEY_V;
    break;case 'w': *code = KEY_W;
    break;case 'x': *code = KEY_X;
    break;case 'y': *code = KEY_Y;
    break;case 'z': *code = KEY_Z;
  }
  if (tag) *tag = TAG_KEY_FULL;
  return seq+1;
}

static char* parse_special_key(char* seq, int* code, int* tag){
  int i = 0; if (!code) code = &i;
  switch (*seq) {
    default: return 0;

    break;case 'U': *code = KEY_UP;    // 103 0x67 up
    break;case 'D': *code = KEY_DOWN;  // 108 0x6c down
    break;case 'L': *code = KEY_LEFT;  // 105 0x69 left
    break;case 'R': *code = KEY_RIGHT; // 106 0x6a right
    break;case 'O': *code = KEY_ENTER; //  28 0x1c enter (Open)
    break;case 'E': *code = KEY_ESC;   //   1 0x01 esc
    break;case 'H': *code = KEY_HOME;  // 102 0x66 home
    break;case 'F': *code = KEY_END;   // 107 0x6b end (Finish)
    break;case 'M': *code = KEY_F12;   //  88 0x58 f12 (Menu)
  }
  if (tag) *tag = TAG_KEY_FULL;
  return seq+1;
}

static char* parse_key_sequence(char* seq, int* code, int* tag){
  char *next;
  if (0!=( next = parse_alphanumeric_key(seq, code, tag) )) return next;
  if (0!=( next = parse_special_key(seq, code, tag) )) return next;
  if (0!=( next = parse_tagged_byte(seq, code, tag) )) return next;
  return 0;
}

static int emulate_sequence(char* seq) {

  int fd = ev_open();
  if (fd < 0) {
    return -1;
  }

  // Wait that userspace detects the new device
  msleep(sequence_wait);

  while (seq && '\0' != *seq) {
    int code = 0, tag = 0;

    // Parse the sequence
    char* newseq = parse_key_sequence(seq, &code, &tag);
    if (0 == newseq || seq == newseq) goto err; // can not parse
    seq = newseq;

    // Emulate the keyboard event
    switch (tag) {
      default: goto err; // unsupported action

      break;case TAG_KEY_FULL: emulate_key(fd, code);
      break;case TAG_KEY_PRESS: emulate_key_press(fd, code);
      break;case TAG_KEY_RELEASE: emulate_key_release(fd, code);
    }
  }

  // Wait that userspace detects all the events
  msleep(sequence_wait);

  ev_close(fd);
  return 0;

err:
  ev_close(fd);
  return -1;
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
  
  while (1) {
    int tmp = open(MISTER_COMMAND_DEVICE, O_WRONLY|O_NONBLOCK);
    if (0<= tmp) {
      close(tmp);
      break;
    }
    LOG("%s\n", "can not access the MiSTer command fifo; retrying");
    msleep(1000);
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

// This MUST BE KEPT IN SYNC with the findPrefixDir function of the Main_MiSTer (file_io.cpp)
//
int findPrefixDirAux(const char* path, char *dir, size_t dir_len){
  char temp_dir[dir_len+1];
	if (0> snprintf(temp_dir, dir_len, "%s/%s", path, dir)) return 0;
  struct stat sb;
  if (stat(temp_dir, &sb)) return 0;
  if (!S_ISDIR(sb.st_mode)) return 0;
	if (0> snprintf(dir, dir_len, "%s", temp_dir)) return 0;
  return 1;
}
int findPrefixDir(char *dir, size_t dir_len){

  if (findPrefixDirAux("/media/usb0", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb0/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb1", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb1/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb2", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb2/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb3", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb3/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb4", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb4/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb5", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb5/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb6", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb6/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/cifs", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/cifs/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/games", dir, dir_len)) return 1;

	return 0;
}

static void get_base_path(system_t* sys, char* out, int size) {
  if ('/' == sys->fsid[0]) {
    snprintf(out, size-1, "%s", sys->fsid);
  } else {
    snprintf(out, size-1, "%s", sys->fsid);
    if (!findPrefixDir(out, size))
      snprintf(out, size-1, "/media/fat/games/%s", sys->fsid); // fallback
  }
}

static void get_link_path(system_t* sys, char* out, int size) {
  if (NULL != sys->sublink) {
    snprintf(out, size-1, "%s", sys->sublink);
  } else {
    get_base_path(sys, out, size);
    int dl = strlen(out);
    snprintf(out+dl, size-dl-1, "/%s/%s.%s", ROMSUBLINK, MBC_LINK_NAM, sys->romext);
  }
}

static int create_file(char* path){
  int result = mkparent(path, 0777);
  if (result) return result;
  FILE* f = fopen(path, "ab");
  if (!f) return errno;
  return fclose(f);
}

static int filesystem_bind(const char* source, const char* target) {
  int err = 0;
  for(int r = 0; r < 20; r += 1){
    if (r > 14) LOG("retrying the binding since the mount point is busy (%s -> %s)\n", source, target);
    if (r != 0) msleep(1000);

    err = mount(source, target, "", MS_BIND | MS_RDONLY | MS_REC, "");

    if (err) err = errno;
    if (EBUSY != err) break;
  }
  return err;
}

static int filesystem_unbind(const char* path) {
  int err = 0;
  for(int r = 0; r < 20; r += 1){
    if (r > 14) LOG("retrying the unbinding since the mount point is busy (%s)\n", path);
    if (r != 0) msleep(1000);

    err = umount(path);

    if (err) err = errno;
    if (EBUSY != err) break;
  }
  return err;
}

static int rom_link(system_t* sys, char* path) {

  // Some cores do not simply list the content of the rom-dir. For example the
  // NeoGeo shows the roms by an internal name, not the file one.  So the best
  // way to handle the rom-dir is to create an temporary sub-dir containing
  // just one file. The dir must have a name that place it as the first item of
  // the list.
  //
  // Moreover the core base directory could be on a filesystem without symbolic
  // links support (e.g. vfat in /media/usb0), so we have to bind-mount a
  // feature-full fs in the temporary directory. The bind-mount removal will
  // fail until the MiSTer main app ends the loading
  //
  // Once bind-mounted, whe have two options:
  //   1) create a link to the desired (we need to use realpath to allow link
  //      created from any working-dir)
  //   2) make another bind-mount to the target file;
  //
  // Here we use the system 2)
  //

  char linkpath[PATH_MAX] = {0};
  get_link_path(sys, linkpath, sizeof(linkpath));

  if (create_file(linkpath)) {
    PRINTERR("Can not create rom link file or folder %s\n", linkpath);
    return -1;
  }

  if (filesystem_bind(path, linkpath)) {
    PRINTERR("Can not bind %s to %s\n", path, linkpath);
    return -1;
  }

  return 0;
}

static int is_empty_file(const char* path){
  struct stat st;
  int stat_error = stat(path, &st);
  if (stat_error) return 0;
  if (!S_ISREG(st.st_mode)) return 0;
  if (0 != st.st_size) return 0;
  return 1;
}

static int rom_unlink(system_t* sys) {
  int result;
  char aux_path[PATH_MAX] = {0};

  get_link_path(sys, aux_path, sizeof(aux_path));
  if (!filesystem_unbind(aux_path) && is_empty_file(aux_path))
    remove(aux_path);

  path_parentize(aux_path);
  rmdir(aux_path); // No issue if error

  return result;
}

static int emulate_system_sequence(system_t* sys) {
  if (NULL == sys) {
    return -1;
  }
  return emulate_sequence(sys->menuseq);
}

static int load_rom(system_t* sys, char* rom) {
  int err;

  err = rom_link(sys, rom);
  if (err) PRINTERR("%s\n", "Can not bind the rom");

  if (!err) {
    err = emulate_system_sequence(sys);
    if (err) PRINTERR("%s\n", "Error during key emulation");
  }

  err = rom_unlink(sys);
  if (err) PRINTERR("%s\n", "Can not unbind the rom");

  return err;
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
  return 0;
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

  char romdir[PATH_MAX] = {0};
  get_base_path(sys, romdir, sizeof(romdir));

  dp = opendir(romdir);
  if (dp != NULL) {
    while (0 != (ep = readdir (dp))){

      if (has_ext(ep->d_name, sys->romext)){
        something_found = 1;
        printf("%s %s/%s\n", sys->id, romdir, ep->d_name);
      }
    }
    closedir(dp);
  }
  if (!something_found) {
    //printf("#%s no '.%s' files found in %s\n", sys->id, sys->romext, romdir);
  }
  return something_found;
}

int list_content(){
  for (int i=0; i<ARRSIZ(system_list); i++){
    list_content_for(system_list+i);
  }
  return 0;
}

int monitor_user_input(int single, char* ms){

  int timeout;
  if (0> sscanf(ms, "%d", &timeout)){
    printf("error\n");
    return -1;
  }
  input_monitor monitor;
  int result = user_input_open(&monitor);
  if (result) goto end;

  for (int c = 1; 1;){

    result = user_input_poll(&monitor, timeout);
    if (!single) c += user_input_clear(&monitor);

    if (is_user_input_timeout(result)) printf("timeout\n");
    else if (is_user_input_event(result)) printf("event catched %d\n", c);

    if (single) break;
  }
  goto end;

end:
  user_input_close(&monitor);
  if (!is_user_input_timeout(result) && ! is_user_input_event(result))
    PRINTERR("input monitor error %d\n", result);

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
static void cmd_wait_input(int argc, char** argv)   { if(checkarg(1,argc))monitor_user_input(1,argv[1]); }
static void cmd_catch_input(int argc, char** argv)  { if(checkarg(1,argc))monitor_user_input(0,argv[1]); }
//
struct cmdentry cmdlist[] = {
  //
  // The "name" field can not contain ' ' or '\0'.
  // The array must be lexicographically sorted wrt "name" field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
  //
  {"catch_input"  , cmd_catch_input  , } ,
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
  {"wait_input"   , cmd_wait_input   , } ,
};

static int run_command(int narg, char** args) {

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
    val = getenv("MBC_CUSTOM_FOLDER");
    if (NULL != val && val[0] != '\0') custom_system->fsid = strdup(val);
    val = getenv("MBC_CUSTOM_ROM_EXT");
    if (NULL != val && val[0] != '\0') custom_system->romext = strdup(val);

    val = getenv("MBC_CUSTOM_LINK");
    int custom_link = 0;
    if (NULL != val && val[0] != '\0') {
      custom_link = 1;
      custom_system->sublink = strdup(val);
    }

    val = getenv("MBC_CUSTOM_SEQUENCE");
    if (NULL != val && val[0] != '\0') {
      int siz = strlen(val);
      char* seq = malloc(siz + strlen(MBCSEQ) +1);
      if (seq) {
        strcpy(seq, val);
        if (!custom_link)
          strcpy(seq + siz, MBCSEQ);
      }
      custom_system->menuseq = seq;
    }
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

