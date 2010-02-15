#include "main.h"
#include <time.h>
#include <iomanip>
#include <sstream>

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

int show_events(int count, const char *devname)
{
	int fd;

	if (count < 1) {
		cerr << "Bogus number specified: cannot print " << count << " events." << endl;
		return 1;
	}
	
	fd = open(devname, O_RDONLY);
	
	if (fd < 0) {
		cErr << "Failed to open device '" << devname << "': " << err << endl;
		return 1;
	}

	if (!no_grab) {
		if (ioctl(fd, EVIOCGRAB, (void*)1) == -1) {
			cErr << "Failed to grab device: " << err << endl;
			close(fd);
			return 1;
		}
	}

	struct input_event ev;
	ssize_t s;
	int c;
	cout << std::hex << std::setfill(' ');
	for (c = 0; c < count; ++c) {
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
	
	return 0;
}
