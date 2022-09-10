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

#define MBC_BUILD_REVISION 34

#define DEVICE_NAME "Fake device"
#define DEVICE_PATH "/dev/uinput"
#define MISTER_COMMAND_DEVICE "/dev/MiSTer_cmd"
#define MGL_PATH "/run/mbc.mgl"
#define CORE_EXT "rbf"

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
#define fread(d,s,n,o) (LOG("fread '%s'\n",fake_file_name(o)), 0)
#define fwrite(d,s,n,o) (LOG("fwrite '%s'\n",fake_file_name(o)), 0)
#define fprintf(f,m,d) (LOG("fprintf '%s' <- '%s'\n",fake_file_name(f),m), 0)
#define fclose(f)      (LOG("fclose '%s'\n",fake_file_name(f)), 0)
#define mount(s,t,x,o, y)    (LOG("mount '%s' -> '%s'\n",s,t), 0)
#define umount(p)      (LOG("umount '%s'\n",p), 0)
#define umount2(p,...) (LOG("umount2 '%s'\n",p), 0)
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

static size_t updatehash( size_t hash, char c){
  return hash ^( c + (hash<<5) + (hash>>2));
}

size_t contenthash( const char* path){
  FILE *mnt  = fopen( path, "r");
  if( !mnt) return 0;
  int c;
  size_t hash = 0;
  while( EOF != ( c = fgetc( mnt)))
    hash = updatehash( hash, (char) c);
  fclose(mnt);
  if( 0 == hash) hash = 1; // 0 is considered invalid hash, so it can be used as error or initialization value
  return hash;
}

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE without any space.
  char *core;    // Path prefix to the core; searched in the internal DB
  char *fsid;    // Name used in the filesystem to identify the folders of the system; if it starts with something different than '/', it will identify a subfolder of a default path
  char *romext;  // Valid extension for rom filename; searched in the internal DB
  char *loadmode;// Load mode for the MGL, more detail a thttps://mister-devel.github.io/MkDocs_MiSTer/advanced/mgl/#mgl-format
  char *delay;   // Delay to be used during the MGL load, more detail at https://mister-devel.github.io/MkDocs_MiSTer/advanced/mgl/#mgl-format
} system_t;

#define ROM_IS_THE_CORE "!direct"

static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  { "ALICEMC10",      "/media/fat/_Computer/AliceMC10_",         "AliceMC10",    "c10", "f0", "1", },
  { "AMIGA.ADF",      "/media/fat/_Computer/Minimig_",           "Amiga",        "adf", "f0", "1", },
  { "AMIGA.HDF",      "Amiga",                                   "Amiga",        "hdf", "f0", "1", },
  { "AMSTRAD",        "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", "f0", "1", },
  { "AMSTRAD-PCW",    "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", "f0", "1", },
  { "AMSTRAD-PCW.B",  "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", "f0", "1", },
  { "AMSTRAD.B",      "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", "f0", "1", },
  { "AMSTRAD.TAP",    "/media/fat/_Computer/Amstrad_",           "Amstrad",      "cdt", "f0", "1", },
  { "AO486",          "/media/fat/_Computer/ao486_",             "AO486",        "img", "f0", "1", },
  { "AO486.B",        "/media/fat/_Computer/ao486_",             "AO486",        "img", "f0", "1", },
  { "AO486.C",        "/media/fat/_Computer/ao486_",             "AO486",        "vhd", "f0", "1", },
  { "AO486.D",        "/media/fat/_Computer/ao486_",             "AO486",        "vhd", "f0", "1", },
  { "APOGEE",         "/media/fat/_Computer/Apogee_",            "APOGEE",       "rka", "f0", "1", },
  { "APPLE-I",        "/media/fat/_Computer/Apple-I_",           "Apple-I",      "txt", "f0", "1", },
  { "APPLE-II",       "/media/fat/_Computer/Apple-II_",          "Apple-II",     "dsk", "f0", "1", },
  { "AQUARIUS.BIN",   "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "bin", "f0", "1", },
  { "AQUARIUS.CAQ",   "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "caq", "f0", "1", },
  { "ARCADE",         ROM_IS_THE_CORE,                     "/media/fat/_Arcade", "mra", NULL, "0", },
  { "ARCHIE.D1",      "/media/fat/_Computer/Archie_",            "ARCHIE",       "vhd", "f0", "1", },
  { "ARCHIE.F0",      "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", "f0", "1", },
  { "ARCHIE.F1",      "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", "f0", "1", },
  { "ASTROCADE",      "/media/fat/_Console/Astrocade_",          "Astrocade",    "bin", "f0", "1", },
  { "ATARI2600",      "/media/fat/_Console/Atari2600_",          "ATARI2600",    "rom", "f0", "1", },
  { "ASTROCADE",      "/media/fat/_Console/Astrocade_",          "Astrocade",    "rom", "f0", "1", },
  { "ATARI5200",      "/media/fat/_Console/Atari5200_",          "ATARI5200",    "rom", "f1", "1", },
  { "ATARI7800",      "/media/fat/_Console/Atari7800_",          "ATARI7800",    "a78", "f1", "1", },
  { "ATARI800.CART",  "/media/fat/_Computer/Atari800_",          "ATARI800",     "car", "f0", "1", },
  { "ATARI800.D1",    "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", "f0", "1", },
  { "ATARI800.D2",    "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", "f0", "1", },
  { "ATARILYNX",      "/media/fat/_Console/AtariLynx_",          "AtariLynx",    "lnx", "f1", "1", },
  { "BBCMICRO",       "/media/fat/_Computer/BBCMicro_",          "BBCMicro",     "vhd", "f0", "1", },
  { "BK0011M",        "/media/fat/_Computer/BK0011M_",           "BK0011M",      "bin", "f0", "1", },
  { "BK0011M.A",      "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", "f0", "1", },
  { "BK0011M.B",      "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", "f0", "1", },
  { "BK0011M.HD",     "/media/fat/_Computer/BK0011M_",           "BK0011M",      "vhd", "f0", "1", },
  { "C16.CART",       "/media/fat/_Computer/C16_",               "C16",          "bin", "f0", "1", },
  { "C16.DISK",       "/media/fat/_Computer/C16_",               "C16",          "d64", "f0", "1", },
  { "C16.TAPE",       "/media/fat/_Computer/C16_",               "C16",          "tap", "f0", "1", },
  { "C64.CART",       "/media/fat/_Computer/C64_",               "C65",          "crt", "f1", "1", },
  { "C64.DISK",       "/media/fat/_Computer/C64_",               "C64",          "rom", "f1", "1", },
  { "C64.PRG",        "/media/fat/_Computer/C64_",               "C64",          "prg", "f1", "1", },
  { "C64.TAPE",       "/media/fat/_Computer/C64_",               "C64",          "rom", "f1", "1", },
  { "COCO_2",         "/media/fat/_Computer/CoCo2_",             "CoCo2",        "rom", "f0", "1", },
  { "COCO_2.CAS",     "/media/fat/_Computer/CoCo2_",             "CoCo2",        "cas", "f0", "1", },
  { "COCO_2.CCC",     "/media/fat/_Computer/CoCo2_",             "CoCo2",        "ccc", "f0", "1", },
  { "COLECO",         "/media/fat/_Console/ColecoVision_",       "Coleco",       "col", "f0", "1", },
  { "COLECO.SG",      "/media/fat/_Console/ColecoVision_",       "Coleco",       "sg",  "f0", "1", },
  { "CUSTOM",         "/media/fat/_Console/NES_",                "NES",          "nes", "f0", "1", },
  { "EDSAC",          "/media/fat/_Computer/EDSAC_",             "EDSAC",        "tap", "f0", "1", },
  { "GALAKSIJA",      "/media/fat/_Computer/Galaksija_",         "Galaksija",    "tap", "f0", "1", },
  { "GAMEBOY",        "/media/fat/_Console/Gameboy_",            "GameBoy",      "gb",  "f0", "2", },
  { "GAMEBOY.COL",    "/media/fat/_Console/Gameboy_",            "GameBoy",      "gbc", "f0", "2", },
  { "GBA",            "/media/fat/_Console/GBA_",                "GBA",          "gba", "f0", "2", },
  { "GENESIS",        "/media/fat/_Console/Genesis_",            "Genesis",      "gen", "f0", "1", },
  { "INTELLIVISION",  "/media/fat/_Console/Intellivision_",      "Intellivision","bin", "f0", "1", },
  { "JUPITER",        "/media/fat/_Computer/Jupiter_",           "Jupiter_",     "ace", "f0", "1", },
  { "LASER310",       "/media/fat/_Computer/Laser310_",          "Laser310_",    "vz",  "f0", "1", },
  { "MACPLUS.2",      "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", "f0", "1", },
  { "MACPLUS.VHD",    "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", "f0", "1", },
  { "MEGACD",         "/media/fat/_Console/MegaCD_",             "MegaCD",       "chd", "s0", "1", },
  { "MEGACD.CUE",     "/media/fat/_Console/MegaCD_",             "MegaCD",       "cue", "s0", "1", },
  { "MEGADRIVE",      "/media/fat/_Console/Genesis_",            "Genesis",      "md",  "f0", "1", },
  { "MEGADRIVE.BIN",  "/media/fat/_Console/Genesis_",            "Genesis",      "bin", "f0", "1", },
  { "MSX",            "/media/fat/_Computer/MSX_",               "MSX",          "vhd", "f0", "1", },
  { "NEOGEO",         "/media/fat/_Console/NeoGeo_",             "NeoGeo",       "neo", "f1", "1", },
  { "NES",            "/media/fat/_Console/NES_",                "NES",          "nes", "f0", "2", },
  { "NES.FDS",        "/media/fat/_Console/NES_",                "NES",          "fds", "f0", "2", },
  { "ODYSSEY2",       "/media/fat/_Console/Odyssey2_",           "ODYSSEY2",     "bin", "f0", "1", },
  { "ORAO",           "/media/fat/_Computer/ORAO_",              "ORAO",         "tap", "f0", "1", },
  { "ORIC",           "/media/fat/_Computer/Oric_",              "Oric_",        "dsk", "f0", "1", },
  { "PDP1",           "/media/fat/_Computer/PDP1_",              "PDP1",         "bin", "f0", "1", },
  { "PET2001",        "/media/fat/_Computer/PET2001_",           "PET2001",      "prg", "f0", "1", },
  { "PET2001.TAP",    "/media/fat/_Computer/PET2001_",           "PET2001",      "tap", "f0", "1", },
  { "PSX",            "/media/fat/_Console/PSX_",                "PSX",          "cue", "s1", "1", },
  { "QL",             "/media/fat/_Computer/QL_",                "QL_",          "mdv", "f0", "1", },
  { "SAMCOUPE.1",     "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", "f0", "1", },
  { "SAMCOUPE.2",     "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", "f0", "1", },
  { "SMS",            "/media/fat/_Console/SMS_",                "SMS",          "sms", "f1", "1", },
  { "SMS.GG",         "/media/fat/_Console/SMS_",                "SMS",          "gg",  "f2", "1", },
  { "SNES",           "/media/fat/_Console/SNES_",               "SNES",         "sfc", "f0", "2", },
  { "SPECIALIST",     "/media/fat/_Computer/Specialist_",        "Specialist_",  "rsk", "f0", "1", },
  { "SPECIALIST.ODI", "/media/fat/_Computer/Specialist_",        "Specialist_",  "odi", "f0", "1", },
  { "SPECTRUM",       "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "tap", "f0", "1", },
  { "SPECTRUM.DSK",   "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "dsk", "f0", "1", },
  { "SPECTRUM.SNAP",  "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "z80", "f0", "1", },
  { "SUPERGRAFX",     "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "sgx", "f0", "1", },
  { "TGFX16",         "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "pce", "f0", "1", },
  { "TGFX16-CD",      "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "chd", "s0", "1", },
  { "TGFX16-CD.CUE",  "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "cue", "s0", "1", },
  { "TI-99_4A",       "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", "f0", "1", },
  { "TI-99_4A.D",     "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", "f0", "1", },
  { "TI-99_4A.G",     "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", "f0", "1", },
  { "TRS-80",         "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", "f0", "1", },
  { "TRS-80.1",       "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", "f0", "1", },
  { "TSCONF",         "/media/fat/_Computer/TSConf_",            "TSConf_",      "vhd", "f0", "1", },
  { "VC4000",         "/media/fat/_Console/VC4000_",             "VC4000",       "bin", "f0", "1", },
  { "VECTOR06",       "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "rom", "f0", "1", },
  { "VECTOR06.A",     "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", "f0", "1", },
  { "VECTOR06.B",     "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", "f0", "1", },
  { "VECTREX",        "/media/fat/_Console/Vectrex_",            "VECTREX",      "vec", "f0", "1", },
  { "VECTREX.OVR",    "/media/fat/_Console/Vectrex_",            "VECTREX",      "ovr", "f0", "1", },
  { "VIC20",          "/media/fat/_Computer/VIC20_",             "VIC20",        "prg", "f0", "1", },
  { "VIC20.CART",     "/media/fat/_Computer/VIC20_",             "VIC20",        "crt", "f0", "1", },
  { "VIC20.CT",       "/media/fat/_Computer/VIC20_",             "VIC20",        "ct",  "f0", "1", },
  { "VIC20.DISK",     "/media/fat/_Computer/VIC20_",             "VIC20",        "d64", "f0", "1", },
  { "WONDERSWAN",     "/media/fat/_Console/WonderSwan_",         "WonderSwan",   "ws",  "f0", "1", },
  { "WONDERSWAN.COL", "/media/fat/_Console/WonderSwan_",         "WonderSwan",   "wsc", "f0", "1", },
  { "ZX81",           "/media/fat/_Computer/ZX81_",              "ZX81",         "0",   "f0", "1", },
  { "ZX81.P",         "/media/fat/_Computer/ZX81_",              "ZX81",         "p",   "f0", "1", },
  { "ZXNEXT",         "/media/fat/_Computer/ZXNext_",            "ZXNext",       "vhd", "f0", "1", },

  // unsupported
  //{ "AMIGA",          "/media/fat/_Computer/Minimig_",           "/media/fat/games/Amiga",     "", "", ""},
  //{ "ATARIST",        "/media/fat/_Computer/AtariST_",           "/media/fat/games/AtariST",     "", "", ""},
  //{ "AY-3-8500",      "/media/fat/_Console/AY-3-8500_",          "/media/fat/games/AY-3-8500",    "", "", ""},
  //{ "Altair8800"
  //{ "MULTICOMP",      "/media/fat/_Computer/MultiComp_",         "/media/fat/games/MultiComp",   "", "", ""},
  //{ "MultiComp"
  //{ "SHARPMZ",        "/media/fat/_Computer/SharpMZ_",           "/media/fat/games/SharpMZ_",     "", "", ""},
  //{ "X68000"
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

static void key_emulator_wait_mount(){
  static size_t mnthash = 0;
  LOG("%s\n", "waiting for some change in the mount table");
  size_t newhash = mnthash;
  for(int retry = 0; retry < 20; retry += 1){
    newhash = contenthash("/proc/mounts");
    if (newhash != mnthash) break;
    msleep(500);
  }
  if (newhash != mnthash) LOG("%s (%zu)\n", "detected a change in the mounting points", newhash);
  else LOG("%s (%zu)\n", "no changes in the mounting points (timeout)", newhash);
  mnthash = newhash;
}

static void key_emulator_function(int fd, int code){
  switch (code){
    default: return;
    break; case KEY_M: key_emulator_wait_mount();
    break; case KEY_S: msleep(1000);
  }
}

#define TAG_KEY_NOTHING '\0'
#define TAG_KEY_PRESS   '{'
#define TAG_KEY_RELEASE '}'
#define TAG_KEY_FULL    ':'
#define TAG_KEY_FUNCT   '!'

static char* parse_hex_byte(char* seq, int* code, int* tag){
  int c, n;
  if (0> sscanf(seq, "%2x%n", &c, &n) || 2!= n) return 0;
  if (code) *code = c;
  if (tag) *tag = TAG_KEY_NOTHING;
  return seq+n;
}

static char* parse_tagged_byte(char* seq, int* code, int* tag){
  if (seq[1] == '\0') return 0;
  char* result = parse_hex_byte(seq+1, code, 0);
  if( !result) return 0;
  switch( *seq){
    default: return 0;
    // TODO : use different char and tag definition
    break; case TAG_KEY_FULL: if (tag) *tag = TAG_KEY_FULL;
    break; case TAG_KEY_PRESS: if (tag) *tag = TAG_KEY_PRESS;
    break; case TAG_KEY_RELEASE: if (tag) *tag = TAG_KEY_RELEASE;
  }
  return result;
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

static char* parse_tagged_alphanumeric_key(char* seq, int* code, int* tag){
  char* result = parse_alphanumeric_key(seq+1, code, tag);
  if (!result) return 0;
  if (TAG_KEY_FUNCT != *seq) return 0; // TODO : use different char and tag definition
  if (tag) *tag = TAG_KEY_FUNCT;
  return result;
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
  if (0!=( next = parse_tagged_alphanumeric_key(seq, code, tag) )) return next;
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
      break;case TAG_KEY_FUNCT: key_emulator_function(fd, code);
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
  // This accesses the "id" field of the "system_t" struct
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

  if (NULL == sys) { // TODO : remove this check ? sys is  not used !
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

char* search_in_string(const char* pattern_start, const char* data, size_t *size){
  char *pattern, *candidate;

#define MATCH_RESET() do{ \
  pattern = (char*) pattern_start; \
  candidate = NULL; \
}while(0)

  MATCH_RESET();
  while( 1){

    if('\0' == *pattern) goto matched;
    if('\0' == *data) goto not_matched;
    if(pattern_start == pattern) candidate = (char*) data;

//    if('%' != *pattern){ // simple char vs wildcard delimiter
      if(*pattern == *data) pattern += 1; // simple char match
      else MATCH_RESET();  // simple char do not match
//    }else{
//
//      pattern += 1;
//      switch(*pattern){
//        break; default: // wrong wildcard
//          goto not_matched;
//
//        break; case '%': // match a '%' (escaped wildcard delimiter)
//          if('%' == *data) pattern += 1;
//          else MATCH_RESET();
//
//        break; case 'W': // match zero or more whitespace
//          while(' ' == *data || '\t' == *data || '\r' == *data || '\n' == *data)
//            data += 1;
//          data -= 1;
//          pattern += 1;
//      }
//    }
    data += 1;
  }
not_matched:
  candidate = NULL;

matched:
  if(size){
    if(!candidate) *size = 0;
    else *size = data - candidate + 1;
  }
  return candidate;

#undef MATCH_RESET
}


int get_absolute_dir_name(const char* source, char* out, size_t len){
  char dirpath[PATH_MAX];
  realpath(source, dirpath);
  if( -1 == snprintf(out, len, "%s", dirpath))
    return -1;
  return 0;
}

int get_relative_path_to_root(int skip, const char* path, char* out, size_t len){
  char dirpath[PATH_MAX];
  dirpath[0] = '\0';
  get_absolute_dir_name(path, dirpath, sizeof(dirpath));
  char* cur = out;
  snprintf(cur, len, "./");
  len -= 2;
  cur += 2;
  for(int i = 0, count = 0; dirpath[i] != '\0'; i += 1){
    if(dirpath[i] == '/'){
      count += 1;
      if(count > skip){
        snprintf(cur, len, "../");
        len -= 3;
        cur += 3;
      }
    }
  }
  return 0;
}

int stricmp(const char* a, const char* b) {
  for (; tolower(*a) == tolower(*b); a += 1, b += 1)
    if (*a == '\0')
      return 0;
  return tolower(*a) - tolower(*b);
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

static int has_mgl_support(system_t* sys) {
  if (!strcmp(ROM_IS_THE_CORE, sys->core))
    return 0;
  return 1;
}

static int rebase_canon_path(const char* base, const char* path, char* buf, size_t len) {

  size_t blen = strlen(base);

  // Special case: path is inside base
  if (!strncmp(base, path, blen)){
    return 0> snprintf(buf, len, "%s", path+blen+1);
  }

  // Find common prefix
  size_t prefix = 0;
  for (size_t c = 0; base[c] == path[c] && '\0' != base[c] && '\0' != path[c]; c += 1)
    prefix += 1;

  // Expand the right number of ../
  for (int c = 0; c < blen - prefix; c+=1){
    if ('/' == base[prefix + c -1]) {
      if (0> snprintf(buf, len, "../")){
        return -1;
      }
      buf += 3;
      len -= 3;
    }
  }

  // Final path part
  return 0> snprintf(buf, len, "%s", path+prefix);
}

// Note: path and out may point to the same memory.
static int reduce_path(const char* path, char* out, size_t len) {
  size_t d = 0;

  for (size_t s = 0; '\0' != path[s]; s += 1){
    if (d >= len) break;
    out[d] = path[s];
    if (0){

    // simplify '//' occurences
    }else if (0
    ||(d >= 1 && '/' == out[d-1] && '/' == out[d])
    ){
      d -= 1;

    // simplify './' occurences
    }else if (0
    ||(d == 1 && '.' == out[d-1] && '/' == out[d])
    ||(d >  1 && '/' == out[d-2] && '.' == out[d-1] && '/' == out[d])
    ){
      d -= 2;

    // simplify '../' occurences
    }else if (0
    ||(d == 2 && '.' == out[d-2] && '.' == out[d-1] && '/' == out[d])
    ||(d >  2 && '/' == out[d-3] && '.' == out[d-2] && '.' == out[d-1] && '/' == out[d])
    ){
      d -= 4;
      if (d < 0) d = 0;
      for (; d > 0 && '/' != out[d]; d -= 1);
    }

    d += 1;
  }
  out[d] = '\0';
  if (d-1 < len && '/' == out[d-1]) out[d-1] = '\0';
  return 0;
}

static int rebase_path(const char* base, const char* path, char* buf, size_t len) {

  char* pwd = getenv("PWD");
  if (!pwd) return -1;
  size_t pwdl = strlen(pwd);

  size_t basel = strlen(base)+pwdl+2;
  char rbase[basel];
  if (0> snprintf(rbase, basel, "%s%s%s",
                  '/' != base[0] ? pwd : "",
                  '/' != base[0] ? "/" : "",
                  base)
  ) return -1;

  size_t pathl = strlen(path)+pwdl+2;
  char rpath[pathl];
  if (0> snprintf(rpath, pathl, "%s%s%s",
                  '/' != path[0] ? pwd : "",
                  '/' != path[0] ? "/" : "",
                  path)
  ) return -1;

  int res;
  res = reduce_path(rbase, rbase, sizeof(rbase));
  if (res) return res;
  res = reduce_path(rpath, rpath, sizeof(rpath));
  if (res) return res;
  return rebase_canon_path(rbase, rpath, buf, len);
}

static int write_mgl_wrapper(FILE* out, char* corepath, char* rom, char* delay, char typ, char* index) {

  if (0> fprintf( out, "<mistergamedescription>\n <rbf>%s", corepath)){
    PRINTERR("%s", "error while writing mgl file\n");
    return -1;
  }

  if (0> fprintf( out, "</rbf>\n <file path=\"%s", rom)){
    PRINTERR("%s", "error while writing mgl file\n");
    return -1;
  }

  if (0> fprintf( out,
                  "\" delay=\"%s\" type=\"%c\" index=\"%s\" />\n</mistergamedescription>\n",
                  delay, typ, index)){
    PRINTERR("%s", "error while writing mgl file\n");
    return -1;
  }

  return 0;
}

static int generate_mgl(system_t* sys, char* corepath, char* rompath, FILE* out) {
  char base[PATH_MAX];
  char rcore[PATH_MAX];
  char rrom[PATH_MAX];

  if (NULL == sys) {
    PRINTERR("%s\n", "invalid system");
    return -1;
  }

  char *opt = sys->loadmode;
  if (!has_mgl_support(sys)) {
    PRINTERR("can not generate mgl for '%s' system - plese load the core directly\n", sys->id);
    return -1;
  }

  if (NULL == corepath)
    corepath = sys->core;

  if (rebase_path("/media/fat", corepath, rcore, sizeof(rcore))){
    PRINTERR("can not handle folder %s\n", corepath);
    return -1;
  }

  // remove extension from core path
  for (int c = strlen(rcore)-1; c >= 0; c -= 1){
    if ('/' == rcore[c]) break;
    if ('.' == rcore[c]) {
      rcore[c] = '\0';
      break;
    }
  }

  get_base_path(sys, base, sizeof(base));

  if (rebase_path(base, rompath, rrom, sizeof(rrom))){
    PRINTERR("can not handle folder %s\n", rompath);
    return -1;
  }

  return write_mgl_wrapper(out, rcore, rrom,
                           sys->delay ? sys->delay : "0",
                           opt[0],
                           '\0' == opt[0] ? opt : opt+1);
}

static int generate_mgl_in_path(system_t* sys, char* corepath, char* rom, const char* outpath) {

  int result = 0;

  if (mkparent(outpath, 0777)){
    PRINTERR("can not create mgl folder at %s\n", outpath);
    return -1;
  }

  FILE* mgl = fopen(outpath, "wb");
  if (0 != mgl) {
    result = generate_mgl(sys, corepath, rom, mgl);
    fclose(mgl);
  }

  if (result)
    PRINTERR("can not generate mgl file at %s\n", outpath);
  return result;
}

static int load_core_directly(system_t* sys, char* path){
  char rpath[PATH_MAX];
  if (NULL == sys)
    return -1;
  if (rebase_path(sys->core, path, rpath, sizeof(rpath)))
    return -1;
  return load_core(sys, rpath);
}

static int load_core_and_rom(system_t* sys, char* corepath, char* rom) {

  if (!has_mgl_support(sys))
    return load_core_directly(sys, rom);

  if (generate_mgl_in_path(sys, corepath, rom, MGL_PATH))
    return -1;
  return load_core(sys, MGL_PATH);
}

static int load_rom_autocore(system_t* sys, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  if (!has_mgl_support(sys))
    return load_core_directly(sys, rom);

  int plen = 64 + strlen(sys->core);
  char corepath[plen];

  if (resolve_core_path(sys->core, corepath, plen)){
    PRINTERR("Can not find the core at %s\n", sys->core);
    return -1;
  }

  return load_core_and_rom(sys, corepath, rom);
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
static void cmd_list_core(int argc, char** argv)    { list_core(); }
static void cmd_rom_autocore(int argc, char** argv) { if(checkarg(2,argc))load_rom_autocore(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_all(int argc, char** argv)     { if(checkarg(2,argc))load_core_and_rom(get_system(argv[1],NULL),argv[1],argv[2]); }
static void cmd_load_all_as(int argc, char** argv)  { if(checkarg(3,argc))load_core_and_rom(get_system(NULL,argv[1]),argv[2],argv[3]); }
static void cmd_raw_seq(int argc, char** argv)      { if(checkarg(1,argc))emulate_sequence(argv[1]); }
static void cmd_list_content(int argc, char** argv) { list_content(); }
static void cmd_list_rom_for(int argc, char** argv) { if(checkarg(1,argc))list_content_for(get_system(NULL,argv[1])); }
static void cmd_wait_input(int argc, char** argv)   { if(checkarg(1,argc))monitor_user_input(1,argv[1]); }
static void cmd_catch_input(int argc, char** argv)  { if(checkarg(1,argc))monitor_user_input(0,argv[1]); }
static void cmd_mgl_gen(int argc, char** argv)      { if(checkarg(3,argc))generate_mgl(get_system(NULL,argv[1]),argv[2],argv[3],stdout); }
static void cmd_mgl_gen_auto(int argc, char** argv) { if(checkarg(2,argc))generate_mgl(get_system(NULL,argv[1]),NULL,argv[2],stdout); }
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
  {"mgl_gen"      , cmd_mgl_gen      , } ,
  {"mgl_gen_auto" , cmd_mgl_gen_auto , } ,
  {"raw_seq"      , cmd_raw_seq      , } ,
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
    val = getenv("MBC_CUSTOM_DELAY");
    if (NULL != val && val[0] != '\0') custom_system->delay = strdup(val);
    val = getenv("MBC_CUSTOM_MODE");
    if (NULL != val && val[0] != '\0') custom_system->loadmode = strdup(val);
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
  printf("MBC (Mister Batch Control) Revision %d\n", MBC_BUILD_REVISION);
#ifdef MBC_BUILD_DATE
  printf("Build timestamp: %s\n", MBC_BUILD_DATE);
#endif // MBC_BUILD_DATE
#ifdef MBC_BUILD_COMMIT
  printf("Build commit: %s\n", MBC_BUILD_COMMIT);
#endif // MBC_BUILD_COMMIT
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

