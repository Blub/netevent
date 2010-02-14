#include "main.h"

unsigned char input_bits[1+EV_MAX/8];
const char *toggle_file = 0;
const char *toggle_cmd = 0;

int read_device(const char *name);
int spawn_device();

static void usage(const char *arg0)
{
	size_t len = strlen(arg0);
	cerr << "usage: " << arg0                  << " [options] -read <device>" << endl;
	cerr << "       " << std::string(len, ' ') << " [options] -write" << endl;
	cerr << "options are:" << endl;
	cerr << "  -ontoggle <command>     Command to execute when grabbing is toggled." << endl;
	cerr << "  -toggler <fifo>         Fifo to keep opening and reading the on-status." << endl;
	exit(1);
}

int main(int argc, char **argv)
{
	const char *arg0 = argv[0];
	if (argc < 2)
		usage(arg0);
	
	memset(input_bits, 0, sizeof(input_bits));

	std::string command(argv[1]);
	while (1) {
		if (command == "-read") {
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
	}
}
