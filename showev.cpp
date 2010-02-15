#include "main.h"
#include <time.h>
#include <iomanip>
#include <sstream>
#include <signal.h>

const char *evname(unsigned int e)
{
	static char buf[128];
	switch (e) {
#define ETOS(x) case x: return #x
		ETOS(EV_SYN);
		ETOS(EV_KEY);
		ETOS(EV_REL);
		ETOS(EV_ABS);
		ETOS(EV_MSC);
		ETOS(EV_SW);
		ETOS(EV_LED);
		ETOS(EV_SND);
		ETOS(EV_REP);
		ETOS(EV_FF);
		ETOS(EV_PWR);
		ETOS(EV_FF_STATUS);
		ETOS(EV_MAX);
		default:
		{
			std::ostringstream val;
			val << e;
			strcpy(buf, val.str().c_str());
			return buf;
		}
	}
	return "<0xDeadC0de>";
}
#undef ETOS

int evid(const char *name)
{
#define ETOS(x) if (!strcmp(name, #x)) return x
       	ETOS(EV_SYN);
       	ETOS(EV_KEY);
       	ETOS(EV_REL);
       	ETOS(EV_ABS);
       	ETOS(EV_MSC);
       	ETOS(EV_SW);
       	ETOS(EV_LED);
       	ETOS(EV_SND);
       	ETOS(EV_REP);
       	ETOS(EV_FF);
       	ETOS(EV_PWR);
       	ETOS(EV_FF_STATUS);
	return EV_MAX;
}
#undef ETOS

static int fd;

static void toggle_hook()
{
	if (ioctl(fd, EVIOCGRAB, (void*)(int)on) == -1) {
		cErr << "Grab failed: " << err << endl;
	}
	setenv("GRAB", (on ? "1" : "0"), 1);
	if (toggle_cmd) {
		if (!fork()) {
			execlp("sh", "sh", "-c", toggle_cmd, NULL);
			cErr << "Failed to run command: " << err << endl;
			exit(1);
		}
	}
}

void ev_toggle(int sig)
{
	if (sig == SIGUSR1) {
		on = !on;
		toggle_hook();
	}
}

int show_events(int count, const char *devname)
{
	if (count < 0) {
		cerr << "Bogus number specified: cannot print " << count << " events." << endl;
		return 1;
	}

	fd = open(devname, O_RDONLY);
	
	if (fd < 0) {
		cErr << "Failed to open device '" << devname << "': " << err << endl;
		return 1;
	}

	signal(SIGUSR1, ev_toggle);

	on = !no_grab;
	if (on) {
		if (ioctl(fd, EVIOCGRAB, (void*)1) == -1) {
			cErr << "Failed to grab device: " << err << endl;
			close(fd);
			return 1;
		}
		setenv("GRAB", "1", -1);
	}
	else
		setenv("GRAB", "0", -1);

	struct input_event ev;
	ssize_t s;
	int c;
	if (!be_quiet)
		cout << std::dec << std::setfill(' ');
	for (c = 0; !count || c < count; ++c) {
		s = read(fd, &ev, sizeof(ev));
		if (s < 0) {
			cErr << "Error while reading from device: " << err << endl;
			close(fd);
			return 1;
		}
		if (s == 0) {
			cerr << "End of data." << endl;
			close(fd);
			return 1;
		}
		if (!be_quiet) {
			time_t curtime = ev.time.tv_sec;
			struct tm *tmp = localtime(&curtime);
			if (ev.type == EV_SYN && !count_syn)
				cout << "   -";
			else
				cout << std::right << std::setfill(' ') << std::setw(4) << c;
			cout << ") Event time: " << asctime(tmp); // asctime contains a newline
			cout << std::left;
			cout << "      Type = " << std::setw(3) << ev.type << evname(ev.type)
			     << "      Code = " << std::setw(6) << ev.code
			     << "  Value = " << std::setw(6) << ev.value
			     << endl;
			if (!count_syn && ev.type == EV_SYN)
				--c;
		}
		bool old_on = on;
		if (hotkey_hook(ev.type, ev.code, ev.value)) {
			if (old_on != on)
				toggle_hook();
		}
	}
	
	return 0;
}
