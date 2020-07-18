#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <linux/uinput.h>

#define DEVICE_NAME "Fake device"
#define DEVICE_PATH "/dev/uinput"
#define MISTER_COMMAND_DEVICE "/dev/MiSTer_cmd"

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
  struct input_event ie;
  ie.type = type;
  ie.code = code;
  ie.value = val;
  gettimeofday(&ie.time, NULL);
  write(fd, &ie, sizeof(ie));
}

static int ev_close(int fd) {
  ioctl(fd, UI_DEV_DESTROY);
  return close(fd);
}

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE .
  char *rompath; // Must be give explicitely at the command line
  char *menuseq; // Internal DB
} system_t;

static char* default_menuseq = "EEMOFO";
static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  // ( NOTE : When menuseq == NULL, default_menuseq will be used )
  
  { "ATARI2600"    , "/media/fat/games/Astrocade/~~~.ROM" , NULL        , } ,
  { "GAMEBOY"      , "/media/fat/GameBoy/~~~.gbc"         , NULL        , } ,
  { "GBA"          , "/media/fat/GBA/~~~.gba"             , NULL        , } ,
  { "GENESIS"      , "/media/fat/Genesis/~~~.gen"         , NULL        , } ,
  { "NES"          , "/media/fat/NES/~~~.nes"             , "EEMOFO"    , } ,
  { "NES.FDSBIOS"  , "/media/fat/NES/~~~.nes"             , "EEMDOFO" } ,
  { "SMS"          , "/media/fat/SMS/~~~.sms"             , NULL        , } ,
  { "SNES"         , "/media/fat/SNES/~~~.sfc"            , NULL        , } ,
  { "TGFX16.SGX"   , "/media/fat/games/TGFX16/~~~.sgx"    , "EEMDOFO" } ,
  { "TURBOGRAFX16" , "/media/fat/games/TGFX16/~~~.pce"    , NULL        , } ,

  // { "ACUARIUS.CAQ"   , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "AO486.C"        , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "AO486.D"        , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "ARCHIE.1"       , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "ATARI800.CART"  , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "ATARI800.D2"    , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "AMSTRAD.B"      , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "C16.CART"       , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "C16.DISK"       , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "C16.PRG"        , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "C16.PLAY"       , "/mnt/the.rom"                   , "EEMDDDOFO" }   ,
  // { "C16.TAPE"       , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "C64.CART"       , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "C64.PLAY"       , "/mnt/the.rom"                   , "EEMDDDDOFO" }  ,
  // { "C64.TAPE"       , "/mnt/the.rom"                   , "EEMDDDOFO" }   ,
  // { "COCO_3.1"       , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "COCO_3.2"       , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "COCO_3.3"       , "/mnt/the.rom"                   , "EEMDDDOFO" }   ,
  // { "COLECO.SG"      , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "MACPLUS.2"      , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "MACPLUS.VHD"    , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "MEGACD.BIOS"    , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "NK0011M.A"      , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "NK0011M.B"      , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "NK0011M.H"      , "/mnt/the.rom"                   , "EEMDDDOFO" }   ,
  // { "SAMCOUPE.2"     , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "SPMX.DDI"       , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "SPECTRUM.TAPE"  , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "TI-00_4A.D"     , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "TI-00_4A.G"     , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "VECTOR06.A"     , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "VECTOR06.B"     , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "VIC20.CT"       , "/mnt/the.rom"                   , "EEMDDOFO" }    ,
  // { "VIC20.CART"     , "/mnt/the.rom"                   , "EEMDOFO" }     ,
  // { "VIC20.DISK"     , "/mnt/the.rom"                   , "EEMDDDOFO" }   ,
  // { "VIC20.PLAY"     , "/mnt/the.rom"                   , "EEMDDDDDOFO" } ,
  // { "VIC20.TAPE"     , "/mnt/the.rom"                   , "EEMDDDDOFO" }  ,
  // { "ZSPECTRUM.TAPE" , "/mnt/the.rom"                   , "EEMDOFO" }     ,

  // unsupported
  //{ "Altair8800"     , 0, 0, },
  //{ "MultiComp"      , 0, 0, },
  //{ "X68000"         , 0, 0, },

  //{ "ZZZZTESTING", "/media/data/temp/zzzztesting/~~~.rom", "EEMOFO", }, // Testing purpose
};

static void emulate_key(int fd, int key) {
  msleep(50);
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

  /* Wait that userspace detects the new device */
  sleep(1);

  for (int i=0; i<strlen(seq); i++) {
    if (0 != i) msleep(50);

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

  /* Wait that userspace detects all the events */
  sleep(1);

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

static int rom_unlink(system_t* sys) {
  return unlink(sys->rompath);
}

static int rom_link(system_t* sys, char* path) {

  rom_unlink(sys); // It is expected that this fails in some cases

  int ret = symlink(path, sys->rompath);
  if (0 != ret) {
    PRINTERR("Can not link %s -> %s\n", sys->rompath, path);
    return -1;
  }

  return 0;
}

static int emulate_system_sequence(system_t* sys) {
  if (NULL == sys) {
    return -1;
  }
  return emulate_sequence(sys->menuseq ? sys->menuseq : default_menuseq);
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

static int load_core_and_rom(system_t* sys, char* corepath, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  int ret = load_core(sys, corepath);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not load the core");
    return -1;
  }

  sleep(3);

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

static int stream_mode();

// command list
static void cmd_exit(int argc, char** argv)         { exit(0); }
static void cmd_stream_mode(int argc, char** argv)  { stream_mode(); }
static void cmd_load_core(int argc, char** argv)    { if(checkarg(1,argc))load_core(get_system(argv[1],NULL),argv[1]); }
static void cmd_load_core_as(int argc, char** argv) { if(checkarg(2,argc))load_core(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_rom(int argc, char** argv)     { if(checkarg(2,argc))load_rom(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_all(int argc, char** argv)     { if(checkarg(2,argc))load_core_and_rom(get_system(argv[1],NULL),argv[1],argv[2]); }
static void cmd_load_all_as(int argc, char** argv)  { if(checkarg(3,argc))load_core_and_rom(get_system(NULL,argv[1]),argv[2],argv[3]); }
static void cmd_rom_link(int argc, char** argv)     { if(checkarg(2,argc))rom_link(get_system(NULL,argv[1]),argv[2]); }
static void cmd_raw_seq(int argc, char** argv)      { if(checkarg(1,argc))emulate_sequence(argv[1]); }
static void cmd_select_seq(int argc, char** argv)   { if(checkarg(1,argc))emulate_system_sequence(get_system(NULL,argv[1])); }
static void cmd_rom_unlink(int argc, char** argv)   { if(checkarg(1,argc))rom_unlink(get_system(NULL,argv[1])); }
//
struct cmdentry cmdlist[] = {
  //
  // The "name" field can not contain ' ' or '\0'.
  // The array must be lexicographically sorted wrt "name" field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
  //
  {"done"         , cmd_exit         , } ,
  {"load_all"     , cmd_load_all     , } ,
  {"load_all_as"  , cmd_load_all_as  , } ,
  {"load_core"    , cmd_load_core    , } ,
  {"load_core_as" , cmd_load_core_as , } ,
  {"load_rom"     , cmd_load_rom     , } ,
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
    PRINTERR("%s", "unknown command");
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

static void print_help(char* name) {
  printf("Usage:\n");
  printf("  %s COMMAND [ARGS]\n", name);
  printf("\n");
  printf("Some command have argumentd, e.g.:\n");
  printf("  %s load_all SYSTEM CORE_PATH ROM_PATH\n", name);
  printf("\n");
  printf("CORE_PATH is the absolute path to the core fpga file (.rbf)\n");
  printf("ROM_PATH is the absolute path to the rom file\n");
  printf("\n");
  printf("Supported SYSTEM:");
  for (int i=0; i<ARRSIZ(system_list); i++){
    if (0 != i) printf(",");
    printf(" %s", system_list[i].id);
  }
  printf("\n");
  printf("Supported COMMAND:");
  for (int i=0; i<ARRSIZ(cmdlist); i++){
    if (0 != i) printf(",");
    printf(" %s", cmdlist[i].name);
  }
  printf("\n");
}

int main(int argc, char* argv[]) {

  if (2 > argc) {
    print_help(argv[0]);
    return 0;
  }
    
  return run_command(argc-1, argv+1);
}

