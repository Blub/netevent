#include "main.h"

unsigned char input_bits[1+EV_MAX/8];

int read_device(const char *name);
int spawn_device();

static void usage(const char *arg0)
{
	size_t len = strlen(arg0);
	cerr << "usage: " << arg0                  << " -read <device>" << endl;
	cerr << "       " << std::string(len, ' ') << " -write" << endl;
	exit(1);
}

int main(int argc, char **argv)
{
	if (argc < 2)
		usage(argv[0]);

	memset(input_bits, 0, sizeof(input_bits));

	std::string command(argv[1]);
	if (command == "-read") {
        	if (argc != 3)
			usage(argv[0]);
		return read_device(argv[2]);
	}
	else if (command == "-write") {
		return spawn_device();
	}
}
