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
#include <getopt.h>
#include <stdarg.h>
#include <map>
using std::map;

#include "main.h"

const char*
Exception::what() const noexcept {
	return msg_;
}

DeviceException::DeviceException(const char *msg)
	: Exception(msg)
{}

MsgException::MsgException(MsgException&& o)
	: Exception(msgbuf_) // we have to copy instead of moving
{
	::strncpy(msgbuf_, o.msgbuf_, sizeof(msgbuf_));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
MsgException::MsgException(const char *msg, ...)
	: Exception(msgbuf_)
{
	va_list ap;
	va_start(ap, msg);
	::vsnprintf(msgbuf_, sizeof(msgbuf_), msg, ap);
	va_end(ap);
}

ErrnoException::ErrnoException(ErrnoException&& o)
	: Exception(msgbuf_) // we have to copy instead of moving
{
	::strncpy(msgbuf_, o.msgbuf_, sizeof(msgbuf_));
}

ErrnoException::ErrnoException(const char *msg, ...)
	: Exception(msgbuf_)
{
	int errcode = errno;
	va_list ap;
	va_start(ap, msg);
	int end = ::vsnprintf(msgbuf_, sizeof(msgbuf_), msg, ap);
	va_end(ap);
	if (end < 0)
		end = 0;
	::snprintf(msgbuf_ + end,
	           sizeof(msgbuf_)-size_t(end),
	           ": %s", ::strerror(errcode));
	msgbuf_[sizeof(msgbuf_)-1] = 0;
}
#pragma clang diagnostic pop

static void
usage [[noreturn]] (FILE *out, int exit_status)
{
	::fprintf(out,
"usage: netevent <command>\n"
"commands:\n"
"  show DEVICE [COUNT]     show up to COUNT input events of DEVICE\n"
"  cat [OPTIONS] DEVICE    dump in netevent 1 comaptible way\n"
"  create [OPTIONS]        create a device\n"
"  daemon [OPTIONS] SOCK   run a device daemon\n"
"  command SOCK <command>  send a runtime command to a daemon\n"
);
	::exit(exit_status);
}

// ::strtoul() doesn't support a maxlen and I want to be able to use this in
// the middle of command parsing... or stuff.
static bool
parseLongParse(long *out, const char *s, size_t maxlen)
{
	*out = 0;
	if (s[0] == '0' && maxlen > 1 && s[1] == 'x') {
		s += 2;
		maxlen -= 2;
		while (*s && maxlen) {
			if (*s >= '0' && *s <= '9') {
				*out = 0x10 * *out + (*s - '0');
				++s;
				--maxlen;
			} else if (*s >= 'a' && *s <= 'f') {
				*out = 0x10 * *out + (*s - 'a' + 0xa);
				++s;
				--maxlen;
			} else if (*s >= 'A' && *s <= 'F') {
				*out = 0x10 * *out + (*s - 'A' + 0xA);
				++s;
				--maxlen;
			} else
				return false;
		}
	} else if (s[0] == '0' && maxlen > 1) {
		++s;
		--maxlen;
		while (*s && maxlen) {
			if (*s >= '0' && *s <= '7') {
				*out = 8 * *out + (*s - '0');
				++s;
				--maxlen;
			} else
				return false;
		}
	} else {
		while (*s && maxlen) {
			if (*s >= '0' && *s <= '9') {
				*out = 10 * *out + (*s - '0');
				++s;
				--maxlen;
			} else
				return false;
		}
	}
	return true;
}

static bool
parseLongDo(long *out, const char *s, size_t maxlen, bool allowSigned)
{
#if 0
	char *end = nullptr;

	errno = 0;
	// no support for non-zero-terminated strings :-(
	*out = ::strtoul(s, &end, 0);
	return !(errno || !end || *end);
#endif

	if (!maxlen || !*s)
		return false;

	while (maxlen && (*s == ' ' || *s == '\t')) {
		++s;
		--maxlen;
	}

	bool negative = false;
	if (*s == '+' || (allowSigned && *s == '-')) {
		negative = *s == '-';
		++s;
		--maxlen;
	}

	while (!maxlen || !(*s >= '0' && *s <= '9'))
		return false;

	if (!parseLongParse(out, s, maxlen))
		return false;
	if (negative)
		*out = -*out;
	return true;
}

bool
parseLong(long *out, const char *s, size_t maxlen)
{
	return parseLongDo(out, s, maxlen, true);
}

bool
parseULong(unsigned long *out, const char *s, size_t maxlen)
{
	long o;
	if (parseLongDo(&o, s, maxlen, false)) {
		*out = static_cast<unsigned long>(o);
		return true;
	}
	return false;
}

unsigned int
String2EV(const char* text, size_t length)
{
	if (length > 3 && ::strncasecmp(text, "EV_", 3) == 0) {
		text += 3;
		length -= 3;
	}
	for (const auto& i: kEVMap) {
		if (::strncasecmp(text, i.name, length) == 0)
			return i.num;
	}
	unsigned long num;
	if (parseULong(&num, text, length))
		return unsigned(num);
	return unsigned(-1);
}

static void
usage_show [[noreturn]] (FILE *out, int exit_status)
{
	::fprintf(out,
"usage: netevent show [options] DEVICE [COUNT]\n"
"options:\n"
"  -h, --help             show this help message\n"
"  -g, --grab             grab the device\n"
"  -G, --no-grab          do not grab the device (default)\n"
);
	::exit(exit_status);
}

static int
cmd_show(int argc, char **argv)
{
	static struct option longopts[] = {
		{ "help",      no_argument, nullptr, 'h' },
		{ "grab",      no_argument, nullptr, 'g' },
		{ "no-grab",   no_argument, nullptr, 'G' },
		{ nullptr, 0, nullptr, 0 }
	};

	bool optGrab = false;
	int c, optindex = 0;
	opterr = 1;
	while (true) {
		c = ::getopt_long(argc, argv, "hgG", longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
		 case 'h':
			usage_show(stdout, EXIT_SUCCESS);

		 case 'g': optGrab = true; break;
		 case 'G': optGrab = false; break;

		 case '?':
			break;
		 default:
			::fprintf(stderr, "getopt error\n");
			return -1;
		}
	}

	if (::optind+1 != argc && ::optind+2 != argc)
		usage_show(stderr, EXIT_FAILURE);

	unsigned long maxcount = 10;
	if (::optind+2 == argc &&
	    !parseULong(&maxcount, argv[::optind+1], size_t(-1)))
	{
		::fprintf(stderr, "bad count: %s\n", argv[::optind+1]);
		return 2;
	}

	InDevice dev {argv[::optind]};
	if (optGrab)
		dev.grab(true);
	InputEvent ev;
	unsigned long count = 0;
	while (count++ < maxcount && dev.read(&ev)) {
		::printf("%s:%u:%i\n",
			 EV2String(ev.type), ev.code, ev.value);
	}

	return 0;
}

static void
usage_cat [[noreturn]] (FILE *out, int exit_status)
{
	::fprintf(out,
"usage: netevent cat [options] DEVICE\n"
"options:\n"
"  -h, --help             show this help message\n"
"  -l, --legacy           run in netevent 1 compatible mode\n"
"  --no-legacy            run in netevent 2 mode (default)\n"
"  -g, --grab             grab the device (default)\n"
"  -G, --no-grab          do not grab the device\n"
);
	::exit(exit_status);
}

static int
cmd_cat(int argc, char **argv)
{
	static struct option longopts[] = {
		{ "help",      no_argument, nullptr, 'h' },
		{ "legacy",    no_argument, nullptr, 'l' },
		{ "no-legacy", no_argument, nullptr, 0x1000 },
		{ "grab",      no_argument, nullptr, 'g' },
		{ "no-grab",   no_argument, nullptr, 'G' },
		{ nullptr, 0, nullptr, 0 }
	};

	bool optLegacyMode = false;
	bool optGrab = true;

	int c, optindex = 0;
	opterr = 1;
	while (true) {
		c = ::getopt_long(argc, argv, "hlgG", longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
		 case 'h':
			usage_cat(stdout, EXIT_SUCCESS);
			// break; usage is [[noreturn]]
			//
		 case 'l':    optLegacyMode = true; break;
		 case 0x1000: optLegacyMode = false; break;

		 case 'g': optGrab = true; break;
		 case 'G': optGrab = false; break;

		 case '?':
			break;
		 default:
			::fprintf(stderr, "getopt error\n");
			return -1;
		}
	}

	if (::optind >= argc) {
		::fprintf(stderr, "missing device name\n");
		usage_cat(stderr, EXIT_FAILURE);
	}

	// Worth adding support for multiple devices via NE2 protocol?
	// Just use the daemon?
	if (::optind+1 != argc) {
		::fprintf(stderr, "too many parameters\n");
		usage_cat(stderr, EXIT_FAILURE);
	}

	InDevice dev {argv[::optind]};
	if (optGrab)
		dev.grab(true);
	if (optLegacyMode) {
		dev.writeNeteventHeader(1);
		InputEvent ev;
		while (dev.read(&ev)) {
			if (!mustWrite(1, &ev, sizeof(ev))) {
				::fprintf(stderr, "write failed: %s\n",
					  ::strerror(errno));
				return 2;
			}
		}
	} else {
		dev.writeNE2AddDevice(1, 0);
		NE2Packet pkt = {};
		pkt.cmd = htobe16(uint16_t(NE2Command::DeviceEvent));
		pkt.event.id = htobe16(0);
		while (dev.read(&pkt.event.event)) {
			pkt.event.event.toNet();
			if (!mustWrite(1, &pkt, sizeof(pkt))) {
				::fprintf(stderr, "write failed: %s\n",
					  ::strerror(errno));
				return 2;
			}
		}
	}
	return 0;
}

static void
usage_create [[noreturn]] (FILE *out, int exit_status)
{
	::fprintf(out,
"usage: netevent create [options]\n"
"options:\n"
"  -h, --help             show this help message\n"
"  -l, --legacy           run in netevent 1 compatible mode\n"
"  --no-legacy            run in netevent 2 mode (default)\n"
"  --duplicates=MODE      how to deal with duplicate devices\n"
"duplicate device modes:\n"
"  reject                 treat duplicates as errors and exit (default)\n"
"  resume                 assume the devices are equivalent and resume them\n"
"  replace                remove the previous device and create a new one\n"
);
	::exit(exit_status);
}

static int
cmd_create_legacy()
{
	// FIXME: if anybody needs it, the --hotkey, --toggler etc. stuff
	// could be added here ...
	auto out = OutDevice::newFromNeteventStream(0);
	InputEvent ev;
	while (true) {
		if (!mustRead(0, &ev, sizeof(ev))) {
			if (!errno)
				break;
			throw ErrnoException("read error");
		}
		out->write(ev);
	}
	return 0;
}

static int
cmd_create(int argc, char **argv)
{
	static struct option longopts[] = {
		{ "help",           no_argument,       nullptr, 'h' },
		{ "legacy",         no_argument,       nullptr, 'l' },
		{ "no-legacy",      no_argument,       nullptr, 0x1000 },
		{ "duplicates",     required_argument, nullptr, 'd' },
		{ nullptr, 0, nullptr, 0 }
	};

	bool no_legacy = false;
	bool optLegacyMode = false;
	enum class DuplicateMode { Reject, Resume, Replace }
	optDuplicates = DuplicateMode::Reject;

	int c, optindex = 0;
	opterr = 1;
	while (true) {
		c = ::getopt_long(argc, argv, "hld:", longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
		 case 'h':
			usage_create(stdout, EXIT_SUCCESS);
			// break; usage is [[noreturn]]
		 case 'l':    optLegacyMode = true; break;
		 case 0x2000: optLegacyMode = false; break;
		 case 'd':
			no_legacy = true;
			if (!::strcasecmp(optarg, "reject"))
				optDuplicates = DuplicateMode::Reject;
			else if (!::strcasecmp(optarg, "resume"))
				optDuplicates = DuplicateMode::Resume;
			else if (!::strcasecmp(optarg, "replace"))
				optDuplicates = DuplicateMode::Replace;
			else {
				::fprintf(stderr,
				"invalid mode for duplicate devices\n"
				"should be 'reject', 'resume' or 'replace'\n");
				usage_create(stderr, EXIT_FAILURE);
			}
			break;
		 case '?':
			break;
		 default:
			::fprintf(stderr, "getopt error\n");
			return -1;
		}
	}

	if (optLegacyMode && no_legacy) {
		::fprintf(stderr,
		    "legacy mode does not support the provided parameters\n");
		return 2;
	}

	if (::optind != argc) {
		::fprintf(stderr, "too many arguments\n");
		usage_create(stderr, EXIT_FAILURE);
	}

	if (optLegacyMode)
		return cmd_create_legacy();

	map<uint16_t, uniq<OutDevice>> devices;

	NE2Packet pkt = {};
	while (mustRead(0, &pkt, sizeof(pkt))) {
		pkt.cmd = be16toh(pkt.cmd);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
		switch (static_cast<NE2Command>(pkt.cmd)) {
		 case NE2Command::KeepAlive:
			break;
		 case NE2Command::AddDevice:
		 {
			pkt.add_device.id = be16toh(pkt.add_device.id);
			pkt.add_device.dev_info_size =
			    be16toh(pkt.add_device.dev_info_size);
			pkt.add_device.dev_name_size =
			    be16toh(pkt.add_device.dev_name_size);

			if (optDuplicates == DuplicateMode::Replace) {
				auto dev =
				    OutDevice::newFromNE2AddCommand(0, pkt);
				devices[pkt.add_device.id] = move(dev);
				break;
			}

			auto old = devices.find(pkt.add_device.id);
			if (old == devices.end()) {
				auto dev =
				    OutDevice::newFromNE2AddCommand(0, pkt);
				devices[pkt.add_device.id] = move(dev);
				break;
			}

			if (optDuplicates == DuplicateMode::Reject)
				throw MsgException(
				    "protocol error: duplicate device %u",
				    pkt.add_device.id);

			if (optDuplicates == DuplicateMode::Resume) {
				OutDevice::skipNE2AddCommand(0, pkt);
				break;
			}

			throw Exception("unhandled --duplicates mode");
		 }
		 case NE2Command::RemoveDevice:
		 {
			auto id = be16toh(pkt.remove_device.id);
			auto iter = devices.find(id);
			if (iter == devices.end())
				throw MsgException(
				    "protocol error: missing device %u", id);
			devices.erase(iter);
			break;
		 }
		 case NE2Command::DeviceEvent:
		 {
			auto id = be16toh(pkt.event.id);
		 	auto iter = devices.find(id);
			if (iter == devices.end())
				throw MsgException(
				    "protocol error: missing device %u", id);
		 	pkt.event.event.toHost();
		 	iter->second->write(pkt.event.event);
			break;
		 }
		 default:
			throw MsgException(
			    "protocol error: unknown packet type %u",
			    pkt.cmd);
		}
#pragma clang diagnostic pop
	}
	if (errno)
		throw ErrnoException("read error");
	return 0;
}

static int
cmd_command(int argc, char **argv)
{
	if (argc < 3) {
		::fprintf(stderr,
		          "usage: netevent command SOCKETNAME COMMAND\n");
		return 2;
	}

	const char *sockname = argv[1];
	if (!sockname[0]) {
		::fprintf(stderr, "bad socket name\n");
		return 3;
	}

	string command = join(' ', argv+2, argv+argc);
	if (!command.length()) // okay
		return 0;

	Socket sock;
	if (sockname[0] == '@')
		sock.connectUnix<true>(&sockname[1]);
	else {
		(void)::unlink(sockname);
		sock.connectUnix<false>(sockname);
	}

	if (!mustWrite(sock.fd(), command.c_str(), command.length())) {
		::fprintf(stderr, "failed to send command: %s\n",
		          ::strerror(errno));
	}
	sock.shutdown(false);
	char buf[1024];
	ssize_t got;
	while ((got = ::read(sock.fd(), buf, sizeof(buf))) > 0) {
		if (!mustWrite(1, buf, size_t(got)))
			return -1; // means we lost our output, screw that
	}
	if (got < 0) {
		::fprintf(stderr, "failed to read response: %s\n",
		          ::strerror(errno));
		return 2;
	}
	return 0;
}

unsigned long kUISetBitIOC[EV_MAX] = {0};
int
main(int argc, char **argv)
{
	// C++ doesn't have designated initializers so...
	kUISetBitIOC[EV_KEY] = UI_SET_KEYBIT;
	kUISetBitIOC[EV_REL] = UI_SET_RELBIT;
	kUISetBitIOC[EV_ABS] = UI_SET_ABSBIT;
	kUISetBitIOC[EV_MSC] = UI_SET_MSCBIT;
	kUISetBitIOC[EV_LED] = UI_SET_LEDBIT;
	kUISetBitIOC[EV_SND] = UI_SET_SNDBIT;
	kUISetBitIOC[EV_FF ] = UI_SET_FFBIT;
	kUISetBitIOC[EV_SW ] = UI_SET_SWBIT;

	if (argc < 2)
		usage(stderr, EXIT_FAILURE);

	if (!::strcmp(argv[1], "-h") ||
	    !::strcmp(argv[1], "-?") ||
	    !::strcmp(argv[1], "--help"))
		usage(stdout, EXIT_SUCCESS);

	::optind = 1;
	try {
		if (!::strcmp(argv[1], "show"))
			return cmd_show(argc-1, argv+1);
		if (!::strcmp(argv[1], "cat"))
			return cmd_cat(argc-1, argv+1);
		if (!::strcmp(argv[1], "create"))
			return cmd_create(argc-1, argv+1);
		if (!::strcmp(argv[1], "daemon"))
			return cmd_daemon(argc-1, argv+1);
		if (!::strcmp(argv[1], "command"))
			return cmd_command(argc-1, argv+1);
	} catch (Exception& ex) {
		::fprintf(stderr, "error: %s\n", ex.what());
		return 2;
	} catch (std::exception& ex) {
		::fprintf(stderr, "unhandled error: %s\n", ex.what());
		return 2;
	}

	fprintf(stderr, "unknown command: %s\n", argv[1]);
	usage(stderr, EXIT_FAILURE);
}
