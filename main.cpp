#include "main.h"

static const char *uinput_file[] = {
	"/dev/uinput",
	"/dev/input/uinput",
	"/dev/misc/uinput",
	0
};

const size_t uinput_cnt = sizeof(uinput_file) / sizeof(uinput_file[0]);

unsigned char input_bits[1+EV_MAX/8];

int read_device(const char *name);
int spawn_device()
{
	int e;
	int fd;
	size_t i;

	for (i = 0; i < uinput_cnt; ++i) {
		fd = open(uinput_file[i], O_WRONLY | O_NDELAY);
		if (fd >= 0)
			break;
	}

	if (i >= uinput_cnt) {
		cerr << "Failed to open uinput device file. Please specify." << endl;
		return 1;
	}

	struct uinput_user_dev dev;
	struct input_event ev;

	memset(&dev, 0, sizeof(dev));
	cin.read((char*)dev.name, sizeof(dev.name));
	cin.read((char*)&dev.id, sizeof(dev.id));
	
	cin.read((char*)input_bits, sizeof(input_bits));
	for (i = 0; i < EV_MAX; ++i) {
		if (!testbit(input_bits, i))
			continue;
		if (ioctl(fd, UI_SET_EVBIT, i) == -1) {
			cErr << "Failed to set evbit " << i << ": " << err << endl;
			goto error;
		}
	}

	close(fd);

	goto end;
error:
	e = 1;
end:
	close(fd);
	
	return e;
}

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
