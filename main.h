#ifndef MAIN_H__
#define MAIN_H__

#include <iostream>

using std::cout;
using std::cerr;
using std::endl;
using std::cin;

#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define gerr() std::string err(strerror(errno))
#define cErr gerr(); cerr
#define testbit(in, bit) (!!( ((in)[bit/8]) & (1<<(bit&7)) ))

extern unsigned char input_bits[1+EV_MAX/8];
extern const char *toggle_file;
extern const char *toggle_cmd;
extern bool no_grab;
extern bool count_syn;
extern bool be_quiet;

typedef struct {
	int type;
	int code;
	int value;
	std::string command;
} hotkey_t;

struct input_event_t {
	uint64_t tv_sec;
	uint32_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

const char *evname(unsigned int e);
int evid(const char *name);

#include <vector>
extern std::vector<hotkey_t> hotkeys;
typedef std::vector<hotkey_t>::const_iterator hotkey_iterator;
extern bool add_hotkey(const char *keydef, const char *command);
extern bool hotkey_hook(int type, int code, int value);
extern bool on;

#endif
