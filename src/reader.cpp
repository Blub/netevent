/*
 * netevent - low-level event-device sharing
 *
 * Copyright (C) 2017-2021 Wolfgang Bumiller <wry.git@bumiller.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>

#include "main.h"

InDevice::InDevice(InDevice&& o)
	: fd_(o.fd_)
	, eof_(o.eof_)
	, grabbing_(o.grabbing_)
	, user_dev_(o.user_dev_)
	, name_(move(o.name_))
	, evbits_(move(o.evbits_))
{
	o.fd_ = -1;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
template<typename T>
int
InDevice::ctl(unsigned long req, T&& data, const char *errmsg, ...) const
{
	int rc = ::ioctl(fd_, req, data);
	if (rc == -1) {
		char buf[1024];
		va_list ap;
		va_start(ap, errmsg);
		::vsnprintf(buf, sizeof(buf), errmsg, ap);
		va_end(ap);
		throw ErrnoException("%s", buf);
	}
	return rc;
}
#pragma clang diagnostic pop

InDevice::InDevice(const string& path)
	: evbits_(EV_MAX)
{
	::memset(&user_dev_, 0, sizeof(user_dev_));

	fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd_ < 0)
		throw ErrnoException("open(%s)", path.c_str());

	ctl(EVIOCGNAME(sizeof(user_dev_.name)), user_dev_.name,
	    "failed to query device name");
	name_.assign(user_dev_.name,
	             ::strnlen(user_dev_.name, sizeof(user_dev_.name)));

	ctl(EVIOCGID, &user_dev_.id, "failed to query device id");

	ctl(EVIOCGBIT(EV_SYN, evbits_.byte_size()), evbits_.data(),
	    "failed to query event device capabilities");
}

InDevice::~InDevice()
{
	if (fd_ != -1)
		::close(fd_);
}

void
InDevice::grab(bool on)
{
	int data = on ? 1 : 0;
	int rc = ::ioctl(fd_, EVIOCGRAB, data);
	if (rc == 0) {
		grabbing_ = on;
		return;
	}
	if (on == grabbing_) {
		// we wouldn't have caused a change, so we expect errors:
		if ( (on  && errno == EBUSY) ||
		     (!on && errno == EINVAL) )
		{
			return;
		}
	}
	throw ErrnoException(
	    on ? "failed to grab input device"
	       : "failed to release input device");
}

bool
InDevice::read(InputEvent *out)
{
	struct input_event ev;
	if (!mustRead(fd_, &ev, sizeof(ev))) {
		if (!errno) {
			eof_ = true;
			throw Exception("unexpected EOF");
		}
		throw ErrnoException("failed to read from device");
	}

	out->tv_sec = uint64_t(ev.time.tv_sec);
	out->tv_usec = uint32_t(ev.time.tv_usec);
	out->type = ev.type;
	out->code = ev.code;
	out->value = ev.value;
	return !eof_;
}

void
InDevice::setName(const string& name)
{
	if (name.length() >= sizeof(user_dev_.name))
		throw MsgException("name too long (%zu > %zu)",
		                   name.length(),
		                   sizeof(user_dev_.name)-1);
	::memcpy(user_dev_.name, name.c_str(), name.length());
	::memset(&user_dev_.name[name.length()], 0,
	         sizeof(user_dev_.name) - name.length());
}

void
InDevice::resetName()
{
	setName(name_);
}

void
InDevice::writeNeteventHeader(int fd)
{
	uint16_t strsz = sizeof(user_dev_);
	struct iovec iov[4];
	iov[0].iov_base = &strsz;
	iov[0].iov_len = sizeof(strsz);
	iov[1].iov_base = user_dev_.name;
	iov[1].iov_len = sizeof(user_dev_.name);
	iov[2].iov_base = &user_dev_.id;
	iov[2].iov_len = sizeof(user_dev_.id);
	iov[3].iov_base = evbits_.data();
	iov[3].iov_len = evbits_.byte_size();
	ssize_t len = 0;
	for (const auto& i: iov)
		len += i.iov_len;

	if (::writev(fd, iov, sizeof(iov)/sizeof(iov[0])) != len)
		throw ErrnoException("failed to write device header");

	static
	struct {
		uint16_t code;
		uint32_t max;
		const char *what;
	}
	const kEntryTypes[] = { // order matters
		{ EV_KEY, KEY_MAX, "key" },
		{ EV_ABS, ABS_MAX, "abs" },
		{ EV_REL, REL_MAX, "rel" },
		{ EV_MSC, MSC_MAX, "msc" },
		{ EV_SW,  SW_MAX,  "sw"  },
		{ EV_LED, LED_MAX, "led" },
	};

	Bits entrybits;
	for (const auto& type : kEntryTypes) {
		if (!evbits_[type.code])
			continue;
		entrybits.resizeNE1Compat(type.max);
		::memset(entrybits.data(), 0, entrybits.byte_size());
		ctl(EVIOCGBIT(type.code, entrybits.byte_size()),
		    entrybits.data(),
		    "failed to query %s event bits", type.what);
		if (!mustWrite(fd, entrybits.data(), entrybits.byte_size()))
			throw ErrnoException("failed to write %s bits",
			                     type.what);
	}

	static
	struct {
		uint16_t code;
		uint32_t max;
		const char *what;
		unsigned long ioc;
	}
	const kStateTypes[] = { // order matters
		// NOTE: netevent 1 compatible lengths!
		{ EV_KEY, KEY_MAX, "key", EVIOCGKEY(1+KEY_MAX/8) },
		{ EV_LED, LED_MAX, "led", EVIOCGLED(1+LED_MAX/8) },
		{ EV_SW,  SW_MAX,  "sw",  EVIOCGSW (1+SW_MAX /8) },
	};
	for (const auto& type : kStateTypes) {
		if (!evbits_[type.code])
			continue;
		entrybits.resizeNE1Compat(type.max);
		::memset(entrybits.data(), 0, entrybits.byte_size());
		ctl(type.ioc, entrybits.data(),
		    "failed to query %s state bits", type.what);
		if (!mustWrite(fd, entrybits.data(), entrybits.byte_size()))
			throw ErrnoException("failed to write %s state",
			                     type.what);
	}

	if (evbits_[EV_ABS]) {
		struct input_absinfo ai;
		for (size_t i = 0; i != ABS_MAX; ++i) {
			ctl(EVIOCGABS(i), &ai,
			    "failed to get abs axis %zu info", i);
			if (!mustWrite(fd, &ai, sizeof(ai)))
				throw ErrnoException(
				    "failed to write absolute axis %zu",
				    i);
		}
	}
}

void
InDevice::writeNE2AddDevice(int fd, uint16_t id)
{
	struct iovec iov[5];

	NE2Packet pkt = {};
	::memset(reinterpret_cast<void*>(&pkt), 0, sizeof(pkt));

	pkt.cmd = htobe16(uint16_t(NE2Command::AddDevice));
	pkt.add_device.id = htobe16(id);
	pkt.add_device.dev_info_size = htobe16(sizeof(user_dev_));
	pkt.add_device.dev_name_size = htobe16(sizeof(user_dev_.name));

	iov[0].iov_base = &pkt;
	iov[0].iov_len = sizeof(pkt);

	iov[1].iov_base = user_dev_.name;
	iov[1].iov_len = sizeof(user_dev_.name);

	struct {
		uint16_t bustype;
		uint16_t vendor;
		uint16_t product;
		uint16_t version;
	} dev_id = {
		htobe16(user_dev_.id.bustype),
		htobe16(user_dev_.id.vendor),
		htobe16(user_dev_.id.product),
		htobe16(user_dev_.id.version),
	};
	iov[2].iov_base = &dev_id;
	iov[2].iov_len = sizeof(dev_id);

	uint16_t evbitsize = htobe16(evbits_.size());
	iov[3].iov_base = &evbitsize;
	iov[3].iov_len = sizeof(evbitsize);

	iov[4].iov_base = evbits_.data();
	iov[4].iov_len = evbits_.byte_size();

	ssize_t len = 0;
	for (const auto& i: iov)
		len += i.iov_len;

	if (::writev(fd, iov, sizeof(iov)/sizeof(iov[0])) != len)
		throw ErrnoException("failed to write device header");

	// NOTE: must not be resized, we use setBitCount here
	Bits entrybits { 0xFFFF };

	// remember available abs axis bits
	Bits absbits;

	uint16_t netbitcount;
	iov[0].iov_base = &netbitcount;
	iov[0].iov_len = sizeof(netbitcount);
	iov[1].iov_base = entrybits.data();
	for (auto ev: evbits_) {
		// Only transfer bits which matter:
		if (!ev || !kUISetBitIOC[ev.index()])
			continue;
		auto count = kBitLength[ev.index()] * LONG_BITS;
		entrybits.setBitCount(size_t(count));
		ctl(EVIOCGBIT(ev.index(), entrybits.byte_size()),
		    entrybits.data(),
		    "failed to query bits for event type %zu",
		    ev.index());
		netbitcount = htobe16(uint16_t(count));
		iov[1].iov_len = entrybits.byte_size();
		len = ssize_t(iov[0].iov_len + iov[1].iov_len);
		if (::writev(fd, iov, 2) != len) {
			throw ErrnoException(
			    "failed to write bits for event type %zu",
			    ev.index());
		}
		if (ev.index() == EV_ABS)
			absbits = entrybits.dup();
	}

	// absolute axis information:
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
		struct input_absinfo hostai;
		ctl(EVIOCGABS(abs.index()), &hostai,
		    "failed to query abs axis %zu info", abs.index());
		ai.value      = int32_t(htobe32(hostai.value));
		ai.minimum    = int32_t(htobe32(hostai.minimum));
		ai.maximum    = int32_t(htobe32(hostai.maximum));
		ai.fuzz       = int32_t(htobe32(hostai.fuzz));
		ai.flat       = int32_t(htobe32(hostai.flat));
		ai.resolution = int32_t(htobe32(hostai.resolution));
		if (!mustWrite(fd, &ai, sizeof(ai)))
			throw ErrnoException(
			    "failed to write absolute axis %zu", abs.index());
	}

	// The next thing in the protocol will be the state, but we currently
	// don't bother, and don't know for how many things we want the state
	// in the future, so we send a bitfield for the types we will send the
	// state for, which we zero out for now:
	Bits statebits {evbits_.size()};
	if (!mustWrite(fd, statebits.data(), statebits.byte_size()))
		throw ErrnoException("failed to write empty state bitfield");
}
