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
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include <algorithm>
#include <vector>
#include <map>
using std::vector;
using std::map;

#include "main.h"

static void
usage_daemon [[noreturn]] (FILE *out, int exit_status)
{
	::fprintf(out,
"usage: netevent daemon [options] SOCKETNAME\n"
"options:\n"
"  -h, --help             show this help message\n"
"  -s, --source=FILE      run commands from FILE on startup\n"
);
	::exit(exit_status);
}

struct FDCallbacks {
	function<void()> onRead;
	function<void()> onHUP;
	function<void()> onError;
	function<void()> onRemove;
};

struct Command {
	int    client_;
	string command_;
};

struct Input {
	uint16_t id_;
	uniq<InDevice> device_;
};

struct FILEHandle {
	FILE *file_;
	FILEHandle(FILE *file) : file_(file) {}
	FILEHandle(FILEHandle&& o) : file_(o.file_) {
		o.file_ = nullptr;
	}
	FILEHandle(const FILEHandle&) = delete;
	~FILEHandle() {
		if (file_)
			::fclose(file_);
	}
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"

static bool                  gQuit = false;
static vector<int>           gFDRemoveQueue;
static vector<struct pollfd> gFDAddQueue;
static map<int, FDCallbacks> gFDCBs;
static map<int, FILEHandle>  gCommandClients;
static vector<Command>       gCommandQueue;
static vector<uint16_t>      gInputIDFreeList;
static map<string, Input>    gInputs;
static map<string, IOHandle> gOutputs;
static struct {
	int fd = -1;
	string name;
}                            gCurrentOutput;
static bool                  gGrab = false;
using ValueHotkeyMap  = map<int32_t, string>;
using CodeHotkeyMap   = map<uint16_t, ValueHotkeyMap>;
//using TypeHotkeyMap   = map<uint16_t, CodeHotkeyMap>;
using DeviceHotkeyMap = map<uint16_t, CodeHotkeyMap>;
static DeviceHotkeyMap gHotkeys[EV_CNT];
#pragma clang diagnostic pop

#if 0
template<typename T>
static void
vectorRemove(vector<T>& vec, T&& value)
{
	auto iter = vec.find(value);
	if (iter != vec.end())
		vec.erase(iter);
}
#endif

template<typename T, typename U>
static void
mapRemove(map<T,U>& m, T key)
{
	auto iter = m.find(key);
	if (iter != m.end())
		m.erase(iter);
}

static void
removeFD(int fd)
{
	if (fd < 0)
		return;
	if (std::find(gFDRemoveQueue.begin(), gFDRemoveQueue.end(), fd)
	    == gFDRemoveQueue.end())
	{
		gFDRemoveQueue.push_back(fd);
	}
}

static void
removeOutput(int fd) {
	removeFD(fd);
}

static void
removeOutput(const string& name)
{
	auto iter = gOutputs.find(name);
	if (iter == gOutputs.end())
		throw MsgException("no such output: %s", name.c_str());
	removeOutput(iter->second.fd());
}

static bool
writeToOutput(int fd, const void *data, size_t size)
{
	if (::write(fd, data, size) != static_cast<ssize_t>(size)) {
		::fprintf(stderr, "error writing to output, dropping\n");
		removeOutput(fd);
		return false;
	}
	return true;
}

static void
announceDeviceRemoval(Input& input)
{
	NE2Packet pkt = {};
	::memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = htobe16(uint16_t(NE2Command::RemoveDevice));
	pkt.remove_device.id = htobe16(input.id_);

	for (auto& oi: gOutputs)
		(void)writeToOutput(oi.second.fd(), &pkt, sizeof(pkt));
}

static void
cleanupDeviceHotkeys(uint16_t id)
{
	for (auto& devmap: gHotkeys)
		mapRemove(devmap, id);
}

static void
processRemoveQueue()
{
	for (int fd : gFDRemoveQueue) {
		auto cbs = gFDCBs.find(fd);
		if (cbs == gFDCBs.end())
			throw Exception("FD without cleanup callback");
		cbs->second.onRemove();
		gFDCBs.erase(cbs);
	}

	gFDRemoveQueue.clear();
}

static void
disconnectClient(int fd)
{
	removeFD(fd);
}

static void
finishClientRemoval(int fd) {
	auto iter = gCommandClients.find(fd);
	if (iter != gCommandClients.end()) {
		gCommandClients.erase(iter);
		return;
	}
	throw Exception("finishClientRemoval: failed to find fd");
}

static void
readCommand(FILE *file)
{
	char *line = nullptr;
	scope (exit) { ::free(line); };
	size_t len = 0;
	auto got = ::getline(&line, &len, file);
	if (got < 0) {
		if (errno)
			::fprintf(stderr,
			          "error reading from command client: %s",
			          ::strerror(errno));
		disconnectClient(::fileno(file));
		return;
	}
	if (got == 0) { // EOF
		disconnectClient(::fileno(file));
		return;
	}
	
	int fd = ::fileno(file);
	gCommandQueue.emplace_back(Command{fd, string{line}});
}

static void
addFD(int fd, short events = POLLIN | POLLHUP | POLLERR)
{
	gFDAddQueue.emplace_back(pollfd { fd, events, 0 });
}

static void
newCommandClient(UnixStream& server)
{
	IOHandle h = server.accept();
	int fd = h.fd();

	FILE *buffd = ::fdopen(fd, "rb");
	if (!buffd)
		throw ErrnoException("fdopen() failed");
	FILEHandle bufhandle { buffd };
	(void)h.release();

	addFD(fd);
	gFDCBs[fd] = FDCallbacks {
		[buffd]() { readCommand(buffd); },
		[fd]() { disconnectClient(fd); },
		[fd]() { disconnectClient(fd); },
		[fd]() { finishClientRemoval(fd); },
	};
	gCommandClients.emplace(fd, move(bufhandle));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
static void
toClient(int fd, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	auto length = ::vsnprintf(buf, sizeof(buf), fmt, ap);
	int err = errno;
	va_end(ap);
	if (length <= 0) {
		::fprintf(stderr, "faield to format client response: %s\n",
		          ::strerror(err));
		disconnectClient(fd);
	}
	if (fd < 0)
		::fwrite(buf, size_t(length), 1, stderr);
	else if (::write(fd, buf, size_t(length)) != length) {
		::fprintf(stderr,
		          "failed to write response to client command\n");
		disconnectClient(fd);
	}
}
#pragma clang diagnostic pop

static uint16_t
getNextInputID()
{
	if (gInputs.size() > UINT16_MAX)
		throw Exception(
		    "too many input devices (... the heck are you doing?)");

	if (gInputIDFreeList.empty())
		return static_cast<uint16_t>(gInputs.size());

	auto next = gInputIDFreeList.back();
	gInputIDFreeList.pop_back();
	return static_cast<uint16_t>(next);
}

static void
freeInputID(uint16_t id)
{
	gInputIDFreeList.push_back(id);
}

static void
closeDevice(InDevice *device)
{
	removeFD(device->fd());
}

static void
finishDeviceRemoval(InDevice *device)
{
	for (auto i = gInputs.begin(); i != gInputs.end(); ++i) {
		if (i->second.device_.get() == device) {
			announceDeviceRemoval(i->second);
			cleanupDeviceHotkeys(i->second.id_);
			gInputs.erase(i);
			return;
		}
	}
	throw Exception("finishDeviceRemoval: failed to find device");
}

static void
useOutput(const string& name)
{
	auto iter = gOutputs.find(name);
	if (iter == gOutputs.end())
		throw MsgException("no such output: %s", name.c_str());
	gCurrentOutput.fd = iter->second.fd();
	gCurrentOutput.name = name;
}

static bool
tryHotkey(uint16_t device, uint16_t type, uint16_t code, int32_t value)
{
	if (type >= EV_CNT)
		return false;
	DeviceHotkeyMap& devicemap = gHotkeys[type];
	auto codemap = devicemap.find(device);
	if (codemap == devicemap.end())
		return false;
	auto valuemap = codemap->second.find(code);
	if (valuemap == codemap->second.end())
		return false;
	auto cmditer = valuemap->second.find(value);
	if (cmditer == valuemap->second.end())
		return false;
	gCommandQueue.emplace_back(Command{-1, cmditer->second});
	return true;
}

static void
grab(bool on)
{
	gGrab = on;
	for (auto& i: gInputs)
		i.second.device_->grab(on);
}

static void
lostCurrentOutput()
{
	gCurrentOutput.fd = -1;
	gCurrentOutput.name = "<none>";
	if (gGrab)
		grab(false);
}

static void
readFromDevice(InDevice *device, uint16_t id)
{
	NE2Packet pkt = {};
	try {
		if (!device->read(&pkt.event.event))
			return closeDevice(device);
	} catch (Exception& ex) {
		::fprintf(stderr, "error reading device: %s\n", ex.what());
		return closeDevice(device);
	}

	const auto& ev = pkt.event.event;
	if (tryHotkey(id, ev.type, ev.code, ev.value))
		return;

	if (gCurrentOutput.fd == -1)
		return;

	// FIXME: currently we only write when grabbing; if anyone needs to b
	// e able to control this separately... PR welcome
	if (!gGrab)
		return;

	pkt.cmd = htobe16(uint16_t(NE2Command::DeviceEvent));
	pkt.event.id = htobe16(id);
	pkt.event.event.toNet();
	if (mustWrite(gCurrentOutput.fd, &pkt, sizeof(pkt)))
		return;

	// on error we drop the output:
	::fprintf(stderr, "error writing to output %s: %s\n",
	          gCurrentOutput.name.c_str(), ::strerror(errno));
	removeOutput(gCurrentOutput.fd);
	lostCurrentOutput();
}

static bool
announceDevice(Input& input, int fd)
{
	try {
		input.device_->writeNE2AddDevice(fd, input.id_);
		return true;
	} catch (Exception& ex) {
		::fprintf(stderr,
			  "error creating device on output, dropping: %s\n",
			  ex.what());
		removeOutput(fd);
		return false;
	}
}

static void
announceDevice(Input& input)
{
	for (auto& oi: gOutputs)
		announceDevice(input, oi.second.fd());
}

static void
announceAllDevices(int fd)
{
	for (auto& i: gInputs) {
		if (!announceDevice(i.second, fd))
			break;
	}
}

static void
addDevice(const string& name, const char *path)
{
	if (gInputs.find(name) != gInputs.end())
		throw MsgException("output already exists: %s", name.c_str());

	auto id = getNextInputID();
	try {
		Input input { id, uniq<InDevice> { new InDevice { path } } };
		InDevice *weakdevptr = input.device_.get();
		int fd = weakdevptr->fd();

		announceDevice(input);

		addFD(fd);
		gFDCBs[fd] = FDCallbacks {
			[=]() { readFromDevice(weakdevptr, id); },
			[=]() { closeDevice(weakdevptr); },
			[=]() { closeDevice(weakdevptr); },
			[=]() { finishDeviceRemoval(weakdevptr); },
		};
		gInputs.emplace(name, move(input));
	} catch (std::exception&) {
		freeInputID(id);
		throw;
	}
}

static void
removeDevice(const string& name)
{
	auto iter = gInputs.find(name);
	if (iter == gInputs.end())
		throw MsgException("no such device: %s", name.c_str());
	closeDevice(iter->second.device_.get());
}

static void
finishOutputRemoval(int fd)
{
	if (gCurrentOutput.fd == fd)
		lostCurrentOutput();
	for (auto i = gOutputs.begin(); i != gOutputs.end(); ++i) {
		if (i->second.fd() == fd) {
			gOutputs.erase(i);
			return;
		}
	}
	throw MsgException("finishOutputRemove: faile dot find fd");
}

// TODO:
// All of these would be nice to have
//   - unix:/a/path
//   - unix:@abstract/name
//           For ease of use.
//
// Less important:
//   - tcp:IP PORT
//   - tcp:HOSTNAME PORT
//   - tcp:[IP] PORT
//   - tcp:[HOSTNAME] PORT
//           Unsafe but still useful.
//   - tcp:DESTINATION PORT CERT KEY CACERTorPATH
//   - tcp:[DESTINATION] PORT CERT KEY CACERTorPATH
//           Annoying & require an ssl lib but more useful than the non-ssl
//           variant...
static void
addOutput_Finish(const string& name, IOHandle handle)
{
	int fd = handle.fd();
	announceAllDevices(fd);
	gOutputs.emplace(name, move(handle));
	gFDCBs.emplace(fd, FDCallbacks {
		[fd]() {
			::fprintf(stderr, "onRead on output");
			removeFD(fd);
		},
		[fd]() { removeFD(fd); },
		[fd]() { removeFD(fd); },
		[fd]() { finishOutputRemoval(fd); },
	});
}

static void
addOutput_Open(const string& name, const char *path)
{
	// Use O_NDELAY to not hang on FIFOs. FIFOs should already be waiting
	// for our connection, we remove O_NONBLOCK below again.
	int fd = ::open(path, O_WRONLY | O_NDELAY);
	if (fd < 0)
		throw ErrnoException("open(%s)", path);
	IOHandle handle { fd };

	int flags = ::fcntl(fd, F_GETFL);
	if (flags == -1)
		throw ErrnoException("failed to get output flags");
	if (::fcntl(fd, F_SETFL, flags & ~(O_NONBLOCK)) != 0)
		throw ErrnoException("failed to remove O_NONBLOCK");

	return addOutput_Finish(name, move(handle));
}

static void
addOutput_Exec(const string& name, const char *path)
{
	int pfd[2];
	if (::pipe(pfd) != 0)
		throw ErrnoException("pipe() failed");
	IOHandle pr { pfd[0] };
	IOHandle pw { pfd[1] };

	pid_t pid = ::fork();
	if (pid == -1)
		throw ErrnoException("fork() failed");

	if (!pid) {
		pw.close();

		if (pr.fd() != 0) {
			if (::dup2(pr.fd(), 0) != 0) {
				::perror("dup2");
				::exit(-1);
			}
			pr.close();
		}
		::execlp("/bin/sh", "/bin/sh", "-c", path, nullptr);
		::perror("exec() failed");
		::exit(-1);
	}
	pr.close();

	return addOutput_Finish(name, move(pw));
}

static void
addOutput(const string& name, const char *path)
{
	if (gOutputs.find(name) != gOutputs.end())
		throw MsgException("output already exists: %s", name.c_str());

	if (::strncmp(path, "exec:", sizeof("exec:")-1) == 0)
		return addOutput_Exec(name, path+(sizeof("exec:")-1));

	return addOutput_Open(name, path);
}

static void
grabCommand(const char *state)
{
	if (!::strcasecmp(state, "1") ||
	    !::strcasecmp(state, "on") ||
	    !::strcasecmp(state, "yes") ||
	    !::strcasecmp(state, "true"))
	{
		gGrab = true;
	}
	else if (!::strcasecmp(state, "0") ||
	         !::strcasecmp(state, "no") ||
	         !::strcasecmp(state, "off") ||
	         !::strcasecmp(state, "false"))
	{
		gGrab = false;
	}
	else if (!::strcasecmp(state, "toggle"))
	{
		gGrab = !gGrab;
	}
	else
		throw MsgException("unknown grab state: %s", state);
	grab(gGrab);
}

static void
addHotkey(uint16_t device, uint16_t type, uint16_t code, int32_t value,
          string command)
{
	if (type >= EV_CNT)
		throw MsgException("unknown event type: %u", type);
	DeviceHotkeyMap& devicemap = gHotkeys[type];
	auto codemap = devicemap.find(device);
	if (codemap == devicemap.end())
		codemap = devicemap.emplace(device, CodeHotkeyMap{})
		    .first;
	auto valuemap = codemap->second.find(code);
	if (valuemap == codemap->second.end())
		valuemap = codemap->second.emplace(code, ValueHotkeyMap{})
		    .first;
	valuemap->second[value] = move(command);
}

static void
removeHotkey(uint16_t device, uint16_t type, uint16_t code, int32_t value)
{
	if (type >= EV_CNT)
		throw MsgException("unknown event type: %u", type);
	DeviceHotkeyMap& devicemap = gHotkeys[type];
	auto codemap = devicemap.find(device);
	if (codemap == devicemap.end())
		return;
	auto valuemap = codemap->second.find(code);
	if (valuemap == codemap->second.end())
		return;
	mapRemove(valuemap->second, value);
}

static void
shellCommand(const char *cmd)
{
	// let's keep it simple for now...
	// stdout/stderr stay
	(void)!::system(cmd);
}

static inline constexpr bool
isWhite(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool
skipWhite(const char* &p)
{
	while (isWhite(*p))
		++p;
	return *p != 0;
}

static string
parseString(const char* &p)
{
	string str;
	char quote = *p++;
	bool escape = false;
	while (*p && *p != quote) {
		if (escape) {
			escape = false;
			switch (*p) {
			 case '\\':  str.append(1, '\\'); break;
			 case 't':   str.append(1, '\t'); break;
			 case 'r':   str.append(1, '\r'); break;
			 case 'n':   str.append(1, '\n'); break;
			 case 'f':   str.append(1, '\f'); break;
			 case 'v':   str.append(1, '\v'); break;
			 case 'b':   str.append(1, '\b'); break;
			 case '"':   str.append(1, '\"'); break;
			 case '\'':  str.append(1, '\''); break;
			 case '0':   str.append(1, '\0'); break;
			 default:
				str.append(1, '\\');
				str.append(1, *p);
				break;
			}
		} else if (*p == '\\') {
			escape = true;
		} else {
			str.append(1, *p);
		}
		++p;
	}
	if (*p) // skip quote
		++p;
	return str;
}

static void
clientCommand_Device(int clientfd, const vector<string>& args)
{
	if (args.size() < 2)
		throw Exception("'device': missing subcommand");

	if (args[1] == "add") {
		if (args.size() != 4)
			throw Exception(
			    "'device add' requires a name and a path");
		addDevice(args[2], args[3].c_str());
		toClient(clientfd, "added device %s\n", args[2].c_str());
	}
	else if (args[1] == "remove") {
		if (args.size() != 3)
			throw Exception(
			    "'device remove' requires a name");
		removeDevice(args[2]);
		toClient(clientfd, "removing device %s\n",
		         args[2].c_str());
	}
	else
		throw MsgException("unknown device subcommand: %s",
		                   args[1].c_str());
}

static void
clientCommand_Output(int clientfd, const vector<string>& args)
{
	if (args.size() < 2)
		throw Exception("'output': missing subcommand");

	if (args[1] == "add") {
		if (args.size() <= 3)
			throw Exception(
			    "'output add' requires a name and a path");
		string cmd = join(' ', args.begin()+3, args.end());
		addOutput(args[2], cmd.c_str());
		toClient(clientfd, "added output %s\n", args[2].c_str());
	}
	else if (args[1] == "remove") {
		if (args.size() != 3)
			throw Exception(
			    "'output remove' requires a name");
		removeOutput(args[2]);
		toClient(clientfd, "removing output %s\n",
		         args[2].c_str());
	}
	else if (args[1] == "use") {
		if (args.size() != 3)
			throw Exception(
			    "'output use' requires a name");
		useOutput(args[2]);
		toClient(clientfd, "output = %s\n",
		         gCurrentOutput.name.c_str());
	}
	else
		throw MsgException("unknown output subcommand: %s",
		                   args[1].c_str());
}

static void
clientCommand_Hotkey(int clientfd, const vector<string>& args)
{
	if (args.size() < 2)
		throw Exception("'hotkey': missing subcommand");

	if (args[1] == "add") {
		if (args.size() < 5)
			throw Exception(
			    "'hotkey add' requires"
			    " a device, a hotkey and a command");
		auto input = gInputs.find(args[2]);
		if (input == gInputs.end())
			throw MsgException("no such device: %s",
			                   args[2].c_str());

		const auto& hotkeydef = args[3];
		auto dot1 = hotkeydef.find(':');
		if (dot1 == hotkeydef.npos || dot1 >= hotkeydef.length()-1)
			throw MsgException("invalid hotkey definition: %s",
			                   hotkeydef.c_str());
		auto dot2 = hotkeydef.find(':', dot1+1);
		if (dot2 == hotkeydef.npos || dot2 >= hotkeydef.length()-1)
			throw MsgException("invalid hotkey definition: %s",
			                   hotkeydef.c_str());


		unsigned int type = String2EV(hotkeydef.c_str(), dot1);
		if (type == unsigned(-1))
			throw MsgException("no such event type: %s",
			                   hotkeydef.c_str());
		if (type > EV_MAX)
			throw MsgException("bad event type: %u", type);

		unsigned long code = 0xffff+1;
		if (!parseULong(&code, hotkeydef.c_str() + dot1+1, dot2-dot1-1)
		    || code > 0xffff)
			throw MsgException("bad event code: %s",
			                   hotkeydef.c_str() + dot1+1);
		long value;
		if (!parseLong(&value, hotkeydef.c_str() + dot2+1, size_t(-1)))
			throw MsgException("bad event value: %s",
			                   hotkeydef.c_str() + dot2+1);

		string cmd = join(' ', args.begin()+4, args.end());
		addHotkey(input->second.id_,
		          uint16_t(type), uint16_t(code), int32_t(value),
		          cmd.c_str());
		toClient(clientfd,
		         "added hotkey %u:%u:%i for device %u\n",
		         type, code, value, input->second.id_);
	}
	else if (args[1] == "remove") {
		if (args.size() != 3)
			throw Exception(
			    "'hotkey remove' requires a hotkey");

		auto input = gInputs.find(args[2]);
		if (input == gInputs.end())
			throw MsgException("no such device: %s",
			                   args[2].c_str());

		const auto& hotkeydef = args[3];
		auto dot1 = hotkeydef.find(':');
		if (dot1 == hotkeydef.npos || dot1 >= hotkeydef.length()-1)
			throw MsgException("invalid hotkey definition: %s",
			                   hotkeydef.c_str());
		auto dot2 = hotkeydef.find(':', dot1+1);
		if (dot2 == hotkeydef.npos || dot2 >= hotkeydef.length()-1)
			throw MsgException("invalid hotkey definition: %s",
			                   hotkeydef.c_str());


		unsigned int type = String2EV(hotkeydef.c_str(), dot1);
		if (type == unsigned(-1))
			throw MsgException("no such event type: %s",
			                   hotkeydef.c_str());
		if (type > EV_MAX)
			throw MsgException("bad event type: %u", type);

		unsigned long code = 0xffff+1;
		if (!parseULong(&code, hotkeydef.c_str() + dot1+1, dot2-dot1-1)
		    || code > 0xffff)
			throw MsgException("bad event code: %s",
			                   hotkeydef.c_str() + dot1+1);
		long value;
		if (!parseLong(&value, hotkeydef.c_str() + dot2+1, size_t(-1)))
			throw MsgException("bad event value: %s",
			                   hotkeydef.c_str() + dot2+1);

		removeHotkey(input->second.id_,
		             uint16_t(type), uint16_t(code), int32_t(value));
		toClient(clientfd,
		         "removed hotkey %u:%u:%i for device %u\n",
		         type, code, value, input->second.id_);

	}
	else
		throw MsgException("unknown hotkey subcommand: %s",
		                   args[1].c_str());
}

static void
clientCommand_Info(int clientfd, const vector<string>& args)
{
	(void)args;

	toClient(clientfd, "Grab: %s\n", gGrab ? "on" : "off");
	toClient(clientfd, "Inputs: %zu\n", gInputs.size());
	for (auto& i: gInputs) {
		toClient(clientfd, "    %u: %s: %i\n",
		         i.second.id_,
		         i.first.c_str(),
		         i.second.device_->fd());
	}

	toClient(clientfd, "Outputs: %zu\n", gOutputs.size());
	for (auto& i: gOutputs) {
		toClient(clientfd, "    %s: %i\n",
		         i.first.c_str(),
		         i.second.fd());
	}

	toClient(clientfd, "Current output: %i: %s\n",
	         gCurrentOutput.fd, gCurrentOutput.name.c_str());

	toClient(clientfd, "Hotkeys:\n");
	unsigned int evtype = 0;
	for (auto& di: gHotkeys) {
		for (auto& ci: di) {
			auto device = ci.first;
			for (auto& vi: ci.second) {
				auto code = vi.first;
				for (auto& v: vi.second) {
					auto value = v.first;
					toClient(clientfd,
					         "    %u: %s:%u:%i => %s\n",
					         device,
					         EV2String(evtype),
					         code, value,
					         v.second.c_str());
				}
			}
		}
		++evtype;
	}
}

static void sourceCommandFile(int clientfd, const char *path);
static void
clientCommand(int clientfd, const vector<string>& args)
{
	if (args.empty())
		return;

	if (args[0] == "nop") {
	} else if (args[0] == "device")
		clientCommand_Device(clientfd, args);
	else if (args[0] == "output")
		clientCommand_Output(clientfd, args);
	else if (args[0] == "hotkey")
		clientCommand_Hotkey(clientfd, args);
	else if (args[0] == "info")
		clientCommand_Info(clientfd, args);
	else if (args[0] == "grab") {
		if (args.size() != 2)
			throw Exception("'grab' requires 1 parameter");
		grabCommand(args[1].c_str());
		//toClient(clientfd, "grab = %u\n", gGrab ? 1 : 0);
	}
	else if (args[0] == "use") {
		if (args.size() != 2)
			throw Exception("'use' requires 1 parameter");
		useOutput(args[1]);
		//toClient(clientfd, "output = %s\n",
		//         gCurrentOutput.name.c_str());
	}
	else if (args[0] == "exec") {
		if (args.size() <= 2)
			throw Exception("'exec' requires 1 parameter");
		string cmd = join(' ', args.begin()+3, args.end());
		shellCommand(cmd.c_str());
	}
	else if (args[0] == "source") {
		if (args.size() != 2)
			throw Exception("'source' requires 1 parameter");
		sourceCommandFile(clientfd, args[1].c_str());
	}
	else if (args[0] == "quit") {
		gQuit = true;
	}
	else
		throw MsgException("unknown command: %s", args[0].c_str());

	if (clientfd < 0)
		return;
	// If it came from an actual client we send an OK back
	toClient(clientfd, "Ok.\n");
}

static void
parseClientCommand(int clientfd, const char *cmd, size_t length)
{
	if (!length)
		return;

	auto end = cmd + length;

	if (!skipWhite(cmd))
		return;

	vector<string> args;
	while (cmd < end) {
		if (*cmd == '"' || *cmd == '\'') {
			args.emplace_back(parseString(cmd));
		} else {
			auto beg = cmd;
			do { ++cmd; } while (*cmd && !isWhite(*cmd));
			args.emplace_back(beg, cmd-beg);
		}
		if (!skipWhite(cmd))
			break;
	}

	clientCommand(clientfd, args);
}

static void
processCommandQueue()
{
	for (const auto& command: gCommandQueue) {
		try {
			parseClientCommand(command.client_,
			                   command.command_.c_str(),
			                   command.command_.length());
		} catch (Exception& ex) {
			toClient(command.client_,
			        "ERROR: %s\n", ex.what());
		}
	}
	gCommandQueue.clear();
}

static void
sourceCommandFile(int clientfd, const char *path)
{
	FILE *file = ::fopen(path, "rb");
	if (!file)
		throw ErrnoException("open(%s)", path);
	char *line = nullptr;

	scope (exit) {
		::fclose(file);
		::free(line);
	};

	size_t bufsize = 0;
	ssize_t length;
	while ((length = ::getline(&line, &bufsize, file)) != -1) {
		if (!length)
			continue;
		line[--length] = 0;
		const char *p = line;
		while (*p && isspace(*p)) {
			++p;
			--length;
		}
		if (!*p || *p == '#')
			continue;
		parseClientCommand(clientfd, p, size_t(length));
	}
	if (::feof(file))
		return;
	if (errno)
		throw ErrnoException("error reading from %s", path);
}

static void
signull(int sig)
{
	switch (sig) {
	 case SIGTERM:
	 case SIGQUIT:
	 case SIGINT:
	 // but this is actually the default
	 default:
		gQuit = true;
		break;
	 case SIGCHLD:
	 {
		int status = 0;
		do {
			// reap zombies
		} while (::waitpid(-1, &status, WNOHANG) > 0);
		break;
	 }
	}
}

int
cmd_daemon(int argc, char **argv)
{
	static struct option longopts[] = {
		{ "help",   no_argument,       nullptr, 'h' },
		{ "source", required_argument, nullptr, 's' },
		{ nullptr, 0, nullptr, 0 }
	};

	vector<const char*> command_files;

	int c, optindex = 0;
	opterr = 1;
	while (true) {
		c = ::getopt_long(argc, argv, "hls:", longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
		 case 'h':
			usage_daemon(stdout, EXIT_SUCCESS);
			// break; usage is [[noreturn]]
		 case 's':
			command_files.push_back(optarg);
			break;
		 case '?':
			break;
		 default:
			::fprintf(stderr, "getopt error\n");
			return -1;
		}
	}

	if (optind+1 != argc) {
		::fprintf(stderr, "missing socket name\n");
		return 2;
	}

	const char *sockname = argv[optind++];

	signal(SIGINT, signull);
	signal(SIGTERM, signull);
	signal(SIGQUIT, signull);
	signal(SIGCHLD, signull);
	signal(SIGPIPE, SIG_IGN);

	UnixStream server;
	if (sockname[0] == '@')
		server.listen<true>(&sockname[1]);
	else {
		(void)::unlink(sockname);
		server.listen<false>(sockname);
	}

	vector<struct pollfd> pfds;
	pfds.resize(1);
	pfds[0].fd = server.fd();

	gFDCBs[server.fd()] = FDCallbacks {
		[&]() { newCommandClient(server); },
		[ ]() { gQuit = true; },
		[ ]() { gQuit = true; },
		[ ]() { throw Exception("removed server socket"); },
	};

	for (auto& i: pfds) {
		i.events = POLLIN | POLLHUP | POLLERR;
		i.revents = 0;
	}

	for (auto file: command_files)
		sourceCommandFile(-1, file);
	command_files.clear();
	command_files.shrink_to_fit();
	while (!gQuit) {
		processCommandQueue();

		if (!gFDAddQueue.empty()) {
			pfds.insert(pfds.end(), gFDAddQueue.begin(),
			            gFDAddQueue.end());
			gFDAddQueue.clear();
		}

		pfds.erase(
		    std::remove_if(pfds.begin(), pfds.end(),
		        [](struct pollfd& pfd) {
		            return std::find(gFDRemoveQueue.begin(),
		                             gFDRemoveQueue.end(),
		                             pfd.fd)
		                != gFDRemoveQueue.end();
		        }
		    ),
		    pfds.end());
		processRemoveQueue();

		auto got = ::poll(pfds.data(), pfds.size(), -1);
		if (got == -1) {
			if (errno == EINTR) {
				::fprintf(stderr, "interrupted\n");
				continue;
			}
			throw ErrnoException("poll interrupted");
		}
		if (!got)
			::fprintf(stderr, "empty poll?\n");

		for (auto& i: pfds) {
			auto cbs = gFDCBs.find(i.fd);
			auto revents = i.revents;
			i.revents = 0;

			if (cbs == gFDCBs.end())
				throw Exception(
				    "internal: callback map broken");

			if (revents & POLLERR)
				cbs->second.onError();
			if (gQuit) break;
			if (revents & POLLHUP)
				cbs->second.onHUP();
			if (gQuit) break;
			if (revents & POLLIN)
				cbs->second.onRead();
			if (gQuit) break;
		}

	}
	::fprintf(stderr, "shutting down\n");

	gFDRemoveQueue.clear();
	gFDCBs.clear();          // destroy possible captures
	gCommandClients.clear(); // disconnect all clients

	return 0;
}
