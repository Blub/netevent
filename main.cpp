#include "main.h"

#include <unistd.h>

unsigned char input_bits[1+EV_MAX/8];
const char *toggle_file = 0;
const char *toggle_cmd = 0;
bool no_grab = false;
bool count_syn = false;
bool be_quiet = false;

int read_device(const char *devname);
int spawn_device();
int show_events(int count, const char *devname);

static void usage(const char *arg0)
{
	size_t len = strlen(arg0);
	cerr << "usage: " << arg0                  << " [options] -read <device>" << endl;
	cerr << "       " << std::string(len, ' ') << "           -write" << endl;
	cerr << "       " << std::string(len, ' ') << " [options] -showevents <count> <device>" << endl;
	cerr << "options are:" << endl;
	cerr << "  -ontoggle <command>     Command to execute when grabbing is toggled." << endl;
	cerr << "  -toggler <fifo>         Fifo to keep opening and reading the on-status." << endl;
	cerr << "  -nograb                 Do not grab the device at startup." << endl;
	cerr << "  -countsyn               Also count SYN events in showevents." << endl;
	cerr << "  -hotkey t:c:v <command> Run a command on the Type:Code:Value event." << endl;
	cerr << "  -quiet                  Shut up -showevents." << endl;
	cerr << std::endl;
	cerr << "-write does not accept any parameters" << endl;
	cerr << "a count of 0 in -showevents means keep going forever" << endl;
	cerr << std::endl;
	cerr << "example hotkey: -hotkey EV_KEY:161:0 \"\" -hotkey EV_KEY:161:1 \"play sound.wav\"" << endl;
	cerr << "  will ignore the eject key-down event, but play sound.wav when releasing the key." << endl;
	cerr << "The special hotkey command '@toggle' toggles device grabbing." << endl;
	cerr << "You can use '@toggle-on' and '@toggle-off' to specifically enable or disable grabbing." << endl;
	exit(1);
}

int main(int argc, char **argv)
{
	const char *arg0 = argv[0];
	if (argc < 2)
		usage(arg0);

	memset(input_bits, 0, sizeof(input_bits));

	while (1) {
		if (argc < 2)
			usage(arg0);
		std::string command(argv[1]);
		if (command == "-h" || command == "--help" || command == "-help") {
			usage(arg0);
		}
		else if (command == "-read") {
        		if (argc < 3)
				usage(arg0);
			return read_device(argv[2]);
		}
		else if (command == "-write") {
			return spawn_device();
		}
		else if (command == "-toggler") {
			if (argc < 3)
				usage(arg0);
			toggle_file = argv[2];
			argv += 2;
			argc -= 2;
		}
		else if (command == "-ontoggle") {
			if (argc < 3)
				usage(arg0);
			toggle_cmd = argv[2];
			argv += 2;
			argc -= 2;
		}
		else if (command == "-nograb") {
			no_grab = true;
			++argv;
			--argc;
		}
		else if (command == "-showevents") {
			if (argc < 4)
				usage(arg0);
			return show_events(atoi(argv[2]), argv[3]);
		}
		else if (command == "-countsyn") {
			count_syn = true;
			++argv;
			--argc;
		}
		else if (command == "-quiet") {
			be_quiet = true;
			++argv;
			--argc;
		}
		else if (command == "-hotkey") {
			if (argc < 4)
				usage(arg0);
			if (!add_hotkey(argv[2], argv[3]))
				usage(arg0);
			argv += 3;
			argc -= 3;
		}
		else {
			cerr << "invalid parameter: " << argv[1] << endl;
			usage(arg0);
		}
	}
	return 1;
}

std::vector<hotkey_t> hotkeys;

bool add_hotkey(const char *keydef, const char *command)
{
	std::string spart[3];
	size_t part = 0;
	const char *p;

	for (p = keydef; *p && part < 3; ++p) {
		if (*p == ':')
			++part;
		else
			spart[part].append(1, *p);
	}

	if (!spart[0].length() || !spart[1].length() || !spart[2].length()) {
		cerr << "Invalid hotkey parameter, need to be of the format <type>:<code>:<value>." << endl;
		cerr << "For example: EV_KEY:161:1" << endl;
		return false;
	}

	if (!isdigit(spart[1][0])) {
		cerr << "Invalid code parameter: " << spart[1] << endl;
		return false;
	}

	if (!isdigit(spart[2][0])) {
		cerr << "Invalid value parameter: " << spart[2] << endl;
		return false;
	}

	hotkey_t hk;
	if (isdigit(spart[0][0]))
		hk.type = atoi(spart[0].c_str());
	else
		hk.type = evid(spart[0].c_str());
	if (hk.type < 0 || hk.type >= EV_MAX) {
		cerr << "Invalid type: " << spart[0] << endl;
		return false;
	}

	hk.code = atoi(spart[1].c_str());
	hk.value = atoi(spart[2].c_str());
	hk.command = command;

	if (!be_quiet) {
		cerr << "Adding hotkey: " << hk.type << ':' << hk.code << ':' << hk.value << " = " << hk.command << endl;
	}
	hotkeys.push_back(hk);
	return true;
}

#include <signal.h>
void tog_signal(int sig);
bool hotkey_hook(int type, int code, int value)
{
	hotkey_iterator hi;
	for (hi = hotkeys.begin(); hi != hotkeys.end(); ++hi) {
		if (hi->type == type && hi->code == code && hi->value == value) {
			if (!hi->command.length())
				return true;
			if (hi->command == "@toggle") {
				on = !on;
				return true;
			}
			if (hi->command == "@toggle-on") {
				on = true;
				return true;
			}
			if (hi->command == "@toggle-off") {
				on = false;
				return true;
			}
       			if (!fork()) {
       				execlp("sh", "sh", "-c", hi->command.c_str(), NULL);
       				cErr << "Failed to run command: " << err << endl;
       				exit(1);
       			}
			return true;
		}
	}
	return false;
}
