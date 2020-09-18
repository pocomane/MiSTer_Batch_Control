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

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE without any space.
  char *core;    // Path prefix to the core; searched in the internal DB
  char *romdir;  // Must be give explicitely at the command line. It must be LOWERCASE .
  char *romext;  // Valid extension for rom filename; searched in the internal DB
  char *menuseq; // Sequence of input for the rom selection; searched in the internal DB (NULL -> default_menuseq will be used)
} system_t;

static char* default_menuseq = "EEMOFO";
static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  { "ATARI2600"    , "/media/fat/_Console/Atari2600_"    , "/media/fat/games/Astrocade"    , "rom" , NULL      , } ,
  { "GAMEBOY"      , "/media/fat/_Console/Gameboy_"      , "/media/fat/games/GameBoy"      , "gbc" , NULL      , } ,
  { "GBA"          , "/media/fat/_Console/GBA_"          , "/media/fat/games/GBA"          , "gba" , NULL      , } ,
  { "GENESIS"      , "/media/fat/_Console/Genesis_"      , "/media/fat/games/Genesis"      , "gen" , NULL      , } ,
  { "NES"          , "/media/fat/_Console/NES_"          , "/media/fat/games/NES"          , "nes" , "EEMOFO"  , } ,
  { "NES.FDSBIOS"  , "/media/fat/_Console/NES_"          , "/media/fat/games/NES"          , "nes" , "EEMDOFO" , } ,
  { "SMS"          , "/media/fat/_Console/SMS_"          , "/media/fat/games/SMS"          , "sms" , NULL      , } ,
  { "SNES"         , "/media/fat/_Console/SNES_"         , "/media/fat/games/SNES"         , "sfc" , NULL      , } ,
  { "TGFX16.SGX"   , "/media/fat/_Console/TurboGrafx16_" , "/media/fat/games/games/TGFX16" , "sgx" , "EEMDOFO" , } ,
  { "TURBOGRAFX16" , "/media/fat/_Console/TurboGrafx16_" , "/media/fat/games/games/TGFX16" , "pce" , NULL      , } ,

  // { "ACUARIUS.CAQ"   , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "AO486.C"        , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "AO486.D"        , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "ARCHIE.1"       , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "ATARI800.CART"  , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "ATARI800.D2"    , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "AMSTRAD.B"      , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "C16.CART"       , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "C16.DISK"       , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "C16.PRG"        , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "C16.PLAY"       , "/core" , "/mnt" , "rom" , "EEMDDDOFO"   , } ,
  // { "C16.TAPE"       , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "C64.CART"       , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "C64.PLAY"       , "/core" , "/mnt" , "rom" , "EEMDDDDOFO"  , } ,
  // { "C64.TAPE"       , "/core" , "/mnt" , "rom" , "EEMDDDOFO"   , } ,
  // { "COCO_3.1"       , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "COCO_3.2"       , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "COCO_3.3"       , "/core" , "/mnt" , "rom" , "EEMDDDOFO"   , } ,
  // { "COLECO.SG"      , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "MACPLUS.2"      , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "MACPLUS.VHD"    , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "MEGACD.BIOS"    , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "NK0011M.A"      , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "NK0011M.B"      , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "NK0011M.H"      , "/core" , "/mnt" , "rom" , "EEMDDDOFO"   , } ,
  // { "SAMCOUPE.2"     , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "SPMX.DDI"       , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "SPECTRUM.TAPE"  , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "TI-00_4A.D"     , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "TI-00_4A.G"     , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "VECTOR06.A"     , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "VECTOR06.B"     , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "VIC20.CT"       , "/core" , "/mnt" , "rom" , "EEMDDOFO"    , } ,
  // { "VIC20.CART"     , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,
  // { "VIC20.DISK"     , "/core" , "/mnt" , "rom" , "EEMDDDOFO"   , } ,
  // { "VIC20.PLAY"     , "/core" , "/mnt" , "rom" , "EEMDDDDDOFO" , } ,
  // { "VIC20.TAPE"     , "/core" , "/mnt" , "rom" , "EEMDDDDOFO"  , } ,
  // { "ZSPECTRUM.TAPE" , "/core" , "/mnt" , "rom" , "EEMDOFO"     , } ,

  // unsupported
  //{ "Altair8800"     , 0 , 0, 0, 0, },
  //{ "MultiComp"      , 0 , 0, 0, 0, },
  //{ "X68000"         , 0 , 0, 0, 0, },

  //{ "ZZZZTESTING", "/media/data/temp/aaa_", "/media/data/temp/zzzztesting", "rom", "EEMOFO", }, // Testing purpose
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

  // Wait that userspace detects the new device
  sleep(1);

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

  sleep(3);

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
    
  return run_command(argc-1, argv+1);
}

