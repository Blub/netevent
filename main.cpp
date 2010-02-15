#include "main.h"

unsigned char input_bits[1+EV_MAX/8];
const char *toggle_file = 0;
const char *toggle_cmd = 0;
bool no_grab = false;
bool count_syn = false;

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
	cerr << std::endl;
	cerr << "-write does not accept any parameters" << endl;
	cerr << "a count of 0 in -showevents means keep going forever" << endl;
	cerr << std::endl;
	cerr << "example hotkey: -hotkey EV_KEY:161:0 "" -hotkey EV_KEY:161:1 \"play sound.wav\"" << endl;
	cerr << "  will ignore the eject key-down event, but play sound.wav when releasing the key." << endl;
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
		else {
			cerr << "invalid parameter: " << argv[1] << endl;
			usage(arg0);
		}
	}
	return 1;
}
