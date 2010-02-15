#include "main.h"

#include <signal.h>

static bool running = true;
bool on = true;
static int fd = 0;
static bool tog_on = false;

static pthread_t tog_thread;

#if defined( WITH_INOTIFY )
# include <sys/inotify.h>
static int inf_fd;
static int watch_fd;
#endif

static void toggle_hook();
void tog_signal(int sig)
{
	if (sig == SIGUSR2)
		tog_on = false;
	else if (sig == SIGUSR1) {
		on = !on;
		toggle_hook();
	}
}

static void *tog_func(void *ign)
{
	int tfd;
	char dat[8];
	tog_on = true;
	signal(SIGUSR2, tog_signal);

#if !defined( WITH_INOTIFY )
	struct stat st;
	if (lstat(toggle_file, &st) != 0) {
		cErr << "stat failed on " << toggle_file << ": " << err << endl;
		tog_on = false;
	}
	else
	{
		if (!S_ISFIFO(st.st_mode)) {
			cerr << "The toggle file is not a fifo, and inotify support has not been compiled in." << endl;
			cerr << "This is evil, please compile with inotify support." << endl;
			tog_on = false;
		}
	}
#else
	inf_fd = inotify_init();
	if (inf_fd == -1) {
		cErr << "inotify_init failed: " << err << endl;
		tog_on = false;
	} else {
		watch_fd = inotify_add_watch(inf_fd, toggle_file, IN_CLOSE_WRITE | IN_CREATE);
		if (watch_fd == -1) {
			cErr << "inotify_add_watch failed: " << err << endl;
			tog_on = false;
		}
	}
#endif

	while (tog_on) {
#if defined( WITH_INOTIFY )
		inotify_event iev;
		if (read(inf_fd, &iev, sizeof(iev)) != (ssize_t)sizeof(iev)) {
			cErr << "Failed to read from inotify watch: " << err << endl;
			break;
		}
		if (iev.wd != watch_fd) {
			cerr << "Inotify sent is bogus information..." << endl;
			continue;
		}
#endif
		tfd = open(toggle_file, O_RDONLY);
		if (tfd < 0) {
			cErr << "Failed to open '" << toggle_file << "': " << err << endl;
			break;
		}
		read(tfd, dat, sizeof(dat));
		close(tfd);
		dat[sizeof(dat)-1] = 0;
		bool r = !!atoi(dat);
		if (on != r) {
			on = r;
			toggle_hook();
		}
	}

	tog_on = running = false;
	return 0;
}

static void toggle_hook()
{
	if (ioctl(fd, EVIOCGRAB, (void*)(int)on) == -1) {
		cErr << "Grab failed: " << err << endl;
	}
	setenv("GRAB", (on ? "1" : "0"), -1);
       	if (toggle_cmd) {
		if (!fork()) {
			execlp("sh", "sh", "-c", toggle_cmd, NULL);
       			cErr << "Failed to run command: " << err << endl;
       			exit(1);
		}

       	}
}

int read_device(const char *devfile)
{
	struct input_event ev;
	size_t i;
	ssize_t s;
	int e = 0;
	on = !no_grab;

	signal(SIGUSR1, tog_signal);
	fd = open(devfile, O_RDONLY);

	if (fd < 0) {
		std::string err(strerror(errno));
		cerr << "Failed to open device '" << devfile << "': " << err << endl;
		return 1;
	}

	if (on) {
		if (ioctl(fd, EVIOCGRAB, (void*)1) == -1) {
			std::string err(strerror(errno));
			cerr << "Failed to grab device: " << err << endl;
		}
		setenv("GRAB", "1", -1);
	}
	else
		setenv("GRAB", "0", -1);

	struct uinput_user_dev dev;
	memset(&dev, 0, sizeof(dev));

	if (ioctl(fd, EVIOCGNAME(sizeof(dev.name)), dev.name) == -1) {
		cErr << "Failed to get device name: " << err << endl;
		goto error;
	}

	if (ioctl(fd, EVIOCGID, &dev.id) == -1) {
		cErr << "Failed to get device id: " << err << endl;
		goto error;
	}
	
	cerr << " Device: " << dev.name << endl;
	cerr << "     Id: " << dev.id.version << endl;
	cerr << "BusType: " << dev.id.bustype << endl;
	
	cout.write(dev.name, sizeof(dev.name));
	cout.write((const char*)&dev.id, sizeof(dev.id));
	
	cerr << "Getting input bits." << endl;
	if (ioctl(fd, EVIOCGBIT(0, sizeof(input_bits)), &input_bits) == -1) {
		cErr << "Failed to get input-event bits: " << err << endl;
		goto error;
	}
	cout.write((const char*)input_bits, sizeof(input_bits));

#define TransferBitsFor(REL, rel, REL_MAX) \
	do { \
	if (testbit(input_bits, EV_##REL)) { \
		unsigned char bits##rel[1+REL_MAX/8]; \
		cerr << "Getting " #rel "-bits." << endl; \
		if (ioctl(fd, EVIOCGBIT(EV_##REL, sizeof(bits##rel)), bits##rel) == -1) { \
			cErr << "Failed to get " #rel " bits: " << err << endl; \
			goto error; \
		} \
		cout.write((const char*)&bits##rel, sizeof(bits##rel)); \
	} \
	} while(0)
	
	TransferBitsFor(KEY, key, KEY_MAX);
	TransferBitsFor(ABS, abs, ABS_MAX);
	TransferBitsFor(REL, rel, REL_MAX);
	TransferBitsFor(MSC, msc, MSC_MAX);
	TransferBitsFor(SW, sw, SW_MAX);
	TransferBitsFor(LED, led, LED_MAX);

#define TransferDataFor(KEY, key, KEY_MAX) \
	do { \
	if (testbit(input_bits, EV_##KEY)) { \
		cerr << "Getting " #key "-state." << endl; \
		unsigned char bits##key[1+KEY_MAX/8]; \
		if (ioctl(fd, EVIOCG##KEY(sizeof(bits##key)), bits##key) == -1) { \
			cErr << "Failed to get " #key " state: " << err << endl; \
			goto error; \
		} \
		cout.write((const char*)bits##key, sizeof(bits##key)); \
	} \
	} while(0)
	
	TransferDataFor(KEY, key, KEY_MAX);
	TransferDataFor(LED, led, LED_MAX);
	TransferDataFor(SW, sw, SW_MAX);

	if (testbit(input_bits, EV_ABS)) {
		struct input_absinfo ai;
		cerr << "Getting abs-info." << endl;
		for (i = 0; i < ABS_MAX; ++i) {
			if (ioctl(fd, EVIOCGABS(i), &ai) == -1) {
				cErr << "Failed to get device id: " << err << endl;
				goto error;
			}
			cout.write((const char*)&ai, sizeof(ai));
		}
	}

	cout.flush();

	if (toggle_file) {
		if (pthread_create(&tog_thread, 0, &tog_func, 0) != 0) {
			cErr << "Failed to create toggling-thread: " << err << endl;
			goto error;
		}
        }

	cerr << "Transferring input events." << endl;
	while (running) {
		s = read(fd, &ev, sizeof(ev));
		if (!s) {
			cerr << "EOF" << endl;
			break;
		}
		else if (s < 0) {
			cErr << "When reading from device: " << err << endl;
			goto error;
		}

		/*
		if (ev.type == EV_KEY && ev.code == 161) {
			if (ev.value == 0) {
				on = !on;
				toggle_hook();
			}
		} else if (on) {
			cout.write((const char*)&ev, sizeof(ev));
			cout.flush();
		}
		*/
		bool old_on = on;
		if (!hotkey_hook(ev.type, ev.code, ev.value)) {
			cout.write((const char*)&ev, sizeof(ev));
			cout.flush();
		} else if (old_on != on) {
			toggle_hook();
		}
	}

	goto end;
error:
	e = 1;
end:
	if (tog_on)
		pthread_cancel(tog_thread);
	ioctl(fd, EVIOCGRAB, (void*)0);
	close(fd);

	return 0;
}
