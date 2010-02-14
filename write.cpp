#include "main.h"

static const char *uinput_file[] = {
	"/dev/uinput",
	"/dev/input/uinput",
	"/dev/misc/uinput",
	0
};
static const size_t uinput_cnt = sizeof(uinput_file) / sizeof(uinput_file[0]);

int spawn_device()
{
	int e;
	int fd;
	size_t i;
	ssize_t si;

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

#define RecieveBitsFor(REL, rel, REL_MAX, RELBIT) \
	do { \
	if (testbit(input_bits, EV_##REL)) { \
		unsigned char bits##rel[1+REL_MAX/8]; \
		cin.read((char*)bits##rel, sizeof(bits##rel)); \
		for (i = 0; i < REL_MAX; ++i) { \
			if (!testbit(bits##rel, i)) continue; \
			if (ioctl(fd, UI_SET_##RELBIT, i) == -1) { \
				cErr << "Failed to set " #rel "-bit: " << i << ": " << err << endl; \
				goto error; \
			} \
		} \
	} \
	} while(0)

	RecieveBitsFor(KEY, key, KEY_MAX, KEYBIT);
	RecieveBitsFor(ABS, abs, ABS_MAX, ABSBIT);
	RecieveBitsFor(REL, rel, REL_MAX, RELBIT);
	RecieveBitsFor(MSC, msc, MSC_MAX, MSCBIT);
	RecieveBitsFor(SW, sw, SW_MAX, SWBIT);
	RecieveBitsFor(LED, led, LED_MAX, LEDBIT);

#define RecieveDataFor(KEY, key, KEY_MAX, KEYBIT) \
	do { \
	if (testbit(input_bits, EV_##KEY)) { \
		unsigned char bits##key[1+KEY_MAX/8]; \
		cin.read((char*)bits##key, sizeof(bits##key)); \
		for (i = 0; i < KEY_MAX; ++i) { \
			if (!testbit(bits##key, i)) continue; \
			if (ioctl(fd, UI_SET_##KEYBIT, i) == -1) { \
				cErr << "Failed to activate " #key "-bit: " << i << ": " << err << endl; \
				goto error; \
			} \
		} \
	} \
	} while(0)

	RecieveDataFor(KEY, key, KEY_MAX, KEYBIT);
	RecieveDataFor(LED, led, LED_MAX, LEDBIT);
	RecieveDataFor(SW, sw, SW_MAX, SWBIT);

	if (testbit(input_bits, EV_ABS)) {
		struct input_absinfo ai;
		for (i = 0; i < ABS_MAX; ++i) {
			cin.read((char*)&ai, sizeof(ai));
			dev.absmin[i] = ai.minimum;
			dev.absmax[i] = ai.maximum;
		}
	}

	si = write(fd, &dev, sizeof(dev));
	if (si < (ssize_t)sizeof(dev)) {
		cErr << "Failed to write initial data to device: " << err << endl;
		goto error;
	}

	if (ioctl(fd, UI_DEV_CREATE) == -1) {
		cErr << "Failed to create device: " << err << endl;
		goto error;
	}

	while (true) {
		if (!cin.read((char*)&ev, sizeof(ev))) {
			cerr << "End of data" << endl;
			break;
		}

	}

	goto end;
error:
	e = 1;
end:
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	
	return e;
}
