/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdarg.h>
#include "main.h"

static const char*const (gDevicePaths[]) = {
	"/dev/uinput",
	"/dev/input/uinput",
	"/dev/misc/uinput",
};

OutDevice::~OutDevice()
{
	(void)::ioctl(fd_, UI_DEV_DESTROY);
	::close(fd_);
}

void
OutDevice::assertNotCreated(const char *errmsg) const
{
	if (created_)
		throw DeviceException(errmsg);
}

OutDevice::OutDevice(const string& name, struct input_id id)
{
	if (name.length() >= sizeof(user_dev_.name))
		throw DeviceException("device name too long");

	for (const char *path : gDevicePaths) {
		fd_ = ::open(path, O_WRONLY | O_NDELAY);
		if (fd_ < 0) {
			if (errno != ENOENT)
				throw ErrnoException(
				    "error opening uinput device");
			continue;
		}
		break;
	}
	if (fd_ < 0)
		throw DeviceException("cannot find uinput device node");

	::memset(&user_dev_, 0, sizeof(user_dev_));
	::memcpy(user_dev_.name, name.c_str(), name.length());
	::memcpy(&user_dev_.id, &id, sizeof(id));

	// Being explicit here: we currently don't support force feedback.
	// (I have no way to test it, and don't want to)
	user_dev_.ff_effects_max = 0;
#ifdef HAS_UI_DEV_SETUP
	struct uinput_setup setup;
	::memcpy(&setup.id, &user_dev_.id, sizeof(user_dev_.id));
	::memcpy(setup.name, name.c_str(), name.length());
	setup.ff_effects_max = user_dev_.ff_effects_max;
	ctl(UI_DEV_SETUP, &setup, "failed to setup uinput device");
#endif
}

void
OutDevice::setEventBit(uint16_t type)
{
	assertNotCreated("trying to enable event type");
	ctl(UI_SET_EVBIT, type,
	   "failed to enable input bit %u", static_cast<unsigned>(type));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
template<typename T>
void
OutDevice::ctl(unsigned long req, T&& data, const char *errmsg, ...) const
{
	if (::ioctl(fd_, req, data) == -1) {
		char buf[1024];
		va_list ap;
		va_start(ap, errmsg);
		::vsnprintf(buf, sizeof(buf), errmsg, ap);
		va_end(ap);
		throw ErrnoException(errmsg);
	}
}

void
OutDevice::setGenericBit(unsigned long what, uint16_t bit,
                         const char *errmsg, ...)
{
	if (created_) {
		char buf[1024];
		va_list ap;
		va_start(ap, errmsg);
		::vsnprintf(buf, sizeof(buf), errmsg, ap);
		va_end(ap);
		throw ErrnoException("device already created", errmsg);
	}
	if (::ioctl(fd_, what, bit) == -1) {
		char buf[1024];
		va_list ap;
		va_start(ap, errmsg);
		::vsnprintf(buf, sizeof(buf), errmsg, ap);
		va_end(ap);
		throw ErrnoException(errmsg);
	}
}
#pragma clang diagnostic pop

void
OutDevice::setupAbsoluteAxis(uint16_t code, const struct input_absinfo& info)
{
	assertNotCreated("trying to set absolute axis");
#ifdef HAS_UI_DEV_SETUP
	struct uinput_abs_setup data = { code, info };
	ctl(UI_ABS_SETUP, &data, "failed to setup device axis information");
#else
	user_dev_.absmax[code] = info.maximum;
	user_dev_.absmin[code] = info.minimum;
	user_dev_.absfuzz[code] = info.fuzz;
	user_dev_.absflat[code] = info.flat;
#endif
}

void
OutDevice::create()
{
#ifdef HAS_UI_DEV_SETUP
	// nothing to do here
#else
	if (::write(fd_, &user_dev_, sizeof(user_dev_)) != sizeof(user_dev_))
		throw ErrnoException("failed to upload device info");
#endif
	if (::ioctl(fd_, UI_DEV_CREATE) == -1)
		throw ErrnoException("failed to create device");
	created_ = true;
}

uniq<OutDevice>
OutDevice::newFromNeteventStream(int fd)
{
	// old netevent protocol:
	uint16_t size;
	struct uinput_user_dev userdev;
	if (!mustRead(fd, &size, sizeof(size)))
		throw ErrnoException("i/o error");

	if (size != sizeof(userdev))
		throw DeviceException(
		    "protocol error: struct uinput_user_dev size mismatch");

	::memset(&userdev, 0, sizeof(userdev));
	if (!mustRead(fd, &userdev.name, sizeof(userdev.name)))
		throw ErrnoException("error reading device name");
	if (!mustRead(fd, &userdev.id, sizeof(userdev.id)))
		throw ErrnoException("error reading device id");

	uniq<OutDevice> dev {
		new OutDevice(string(userdev.name,
		              ::strnlen(userdev.name, sizeof(userdev.name))),
		              userdev.id)
	};

	// NOTE: netevent1 used 1+max/8 everywhere for array sizes
	Bits bits;
	bits.resizeNE1Compat(EV_MAX);
	if (!mustRead(fd, bits.data(), bits.byte_size()))
		throw ErrnoException("error reading event bits");
	bits.shrinkTo(EV_MAX);
	for (auto b : bits)
		if (b)
			dev->setEventBit(uint16_t(b.index()));

	// netevent protocol 1 contained (only) key, abs, rel, msc, sw, led
	// data here

	static
	struct {
		uint16_t code;
		uint32_t max;
		const char *what;
		unsigned long ioc;
	}
	const kEntryTypes[] = { // order matters
		{ EV_KEY, KEY_MAX, "key", UI_SET_KEYBIT },
		{ EV_ABS, ABS_MAX, "abs", UI_SET_ABSBIT },
		{ EV_REL, REL_MAX, "rel", UI_SET_RELBIT },
		{ EV_MSC, MSC_MAX, "msc", UI_SET_MSCBIT },
		{ EV_SW,  SW_MAX,  "sw",  UI_SET_SWBIT  },
		{ EV_LED, LED_MAX, "led", UI_SET_LEDBIT },
	};

	Bits entrybits;
	Bits absbits;
	for (const auto& type : kEntryTypes) {
		// NOTE: netevent used an array of [1 + MAX/8] for each bit
		// field
		if (!bits[type.code])
			continue;
		entrybits.resizeNE1Compat(type.max);
		if (!mustRead(fd, entrybits.data(), entrybits.byte_size()))
			throw ErrnoException("error reading %s bits",
			                     type.what);
		entrybits.shrinkTo(type.max);
		for (auto b : entrybits) {
			if (b)
				dev->setGenericBit(
				    type.ioc, uint16_t(b.index()),
				    "failed to set %s bit", type.what);
		}
		// remember the absolute bits:
		if (type.code == EV_ABS)
			absbits = move(entrybits);
	}

	// netevent 1 dumps the key state, LED state and SW state at this point
	// note that netevent 1 didn't actually apply it, but rather enabled
	// the corresponding codes, we simply skip this step
	static
	struct {
		uint16_t code;
		uint32_t max;
		const char *what;
	}
	const kStateTypes[] = { // order matters
		{ EV_KEY, KEY_MAX, "key" },
		{ EV_LED, LED_MAX, "led" },
		{ EV_SW,  SW_MAX,  "sw" },
	};
	for (const auto& type : kStateTypes) {
		// NOTE: netevent used an array of [1 + MAX/8] for each bit
		// field
		if (!bits[type.code])
			continue;
		entrybits.resizeNE1Compat(type.max);
		if (!mustRead(fd, entrybits.data(), entrybits.byte_size()))
			throw ErrnoException("error reading %s state",
			                     type.what);
		// we discard this data
	}

	if (bits[EV_ABS]) {
		struct input_absinfo ai;
		for (size_t i = 0; i != ABS_MAX; ++i) {
			if (!mustRead(fd, &ai, sizeof(ai)))
				throw ErrnoException(
				    "failed to read absolute axis %zu", i);
			if (absbits[i])
				dev->setupAbsoluteAxis(uint16_t(i), ai);
		}
	}

	dev->create();

	return dev;
}

uniq<OutDevice>
OutDevice::newFromNE2AddCommand(int fd, NE2Packet& pkt)
{
	if (pkt.cmd != static_cast<int>(NE2Command::AddDevice))
		throw Exception("internal error: wrong packet");

	struct uinput_user_dev userdev;
	if (pkt.add_device.dev_info_size != sizeof(userdev))
		throw DeviceException(
		    "protocol error: struct user_input_dev size mismatch");
	if (pkt.add_device.dev_name_size != sizeof(userdev.name))
		throw DeviceException(
		    "protocol error: struct input device name size mismatch");

	::memset(&userdev, 0, sizeof(userdev));
	if (!mustRead(fd, &userdev.name, sizeof(userdev.name)))
		throw ErrnoException("error reading device name");
	struct {
		uint16_t bustype;
		uint16_t vendor;
		uint16_t product;
		uint16_t version;
	} dev_id;
	if (!mustRead(fd, &dev_id, sizeof(dev_id)))
		throw ErrnoException("error reading device id");
	userdev.id.bustype = be16toh(dev_id.bustype);
	userdev.id.vendor  = be16toh(dev_id.vendor);
	userdev.id.product = be16toh(dev_id.product);
	userdev.id.version = be16toh(dev_id.version);

	uniq<OutDevice> dev {
		new OutDevice(string(userdev.name,
		              ::strnlen(userdev.name, sizeof(userdev.name))),
		              userdev.id)
	};

	uint16_t evbitsize = 0;
	if (!mustRead(fd, &evbitsize, sizeof(evbitsize)))
		throw ErrnoException("failed to read type bitfield size");
	evbitsize = be16toh(evbitsize);
	if (evbitsize != EV_MAX)
		throw MsgException(
		    "protocol error: event type count mismatch, got %u != %u",
		    evbitsize, EV_MAX);

	Bits evbits;
	evbits.resize(EV_MAX);
	if (!mustRead(fd, evbits.data(), evbits.byte_size()))
		throw ErrnoException("error reading event bits");
	for (auto bit : evbits)
		if (bit)
			dev->setEventBit(uint16_t(bit.index()));

	Bits entrybits;
	Bits absbits;
	for (auto ev : evbits) {
		if (!ev || !kUISetBitIOC[ev.index()])
			continue;
		uint16_t count;
		if (!mustRead(fd, &count, sizeof(count)))
			throw ErrnoException(
			    "failed to read type %zu bit count",
			    ev.index());
		count = be16toh(count);
		entrybits.resize(count);
		if (!mustRead(fd, entrybits.data(), entrybits.byte_size()))
			throw ErrnoException(
			    "failed to read type %zu bit field",
			    ev.index());

		auto ioc = kUISetBitIOC[ev.index()];
		for (auto b : entrybits) {
			if (b)
				dev->setGenericBit(ioc, uint16_t(b.index()),
				    "failed to set %zu bit %zu",
				    ev.index(), b.index());
		}

		if (ev.index() == EV_ABS)
			absbits = move(entrybits);
	}

	struct {
		int32_t value;
		int32_t minimum;
		int32_t maximum;
		int32_t fuzz;
		int32_t flat;
		int32_t resolution;
	} ai;
	for (auto abs : absbits) {
		if (!abs)
			continue;
		if (!mustRead(fd, &ai, sizeof(ai)))
			throw ErrnoException(
			    "failed to read absolute axis %zu", abs.index());
		struct input_absinfo hostai;
		hostai.value      = be16toh(ai.value);
		hostai.minimum    = be16toh(ai.minimum);
		hostai.maximum    = be16toh(ai.maximum);
		hostai.fuzz       = be16toh(ai.fuzz);
		hostai.flat       = be16toh(ai.flat);
		hostai.resolution = be16toh(ai.resolution);
		dev->setupAbsoluteAxis(uint16_t(abs.index()), hostai);
	}

	// Skip the state:
	Bits statebits {EV_MAX};
	if (!mustRead(fd, statebits.data(), statebits.byte_size()))
		throw ErrnoException("failed to read state bitfield");
	for (auto i : statebits) {
		if (i) {
			::fprintf(stderr, "got unexpected state bits\n");
			break;
		}
	}

	dev->create();

	return dev;
}

void
OutDevice::write(const InputEvent& ie)
{
	struct input_event ev;
	ev.time.tv_sec = time_t(ie.tv_sec);
	ev.time.tv_usec = ie.tv_usec;
	ev.type = ie.type;
	ev.code = ie.code;
	ev.value = ie.value;
	if (!mustWrite(fd_, &ev, sizeof(ev)))
		throw ErrnoException("failed to write event");
}
