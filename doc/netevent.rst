========
netevent
========

--------------------------------------
show, share, clone evdev event devices
--------------------------------------

:Author: Wolfgang Bumiller
:Manual section: 1
:Manual group: netevent Manual

.. TODO: email

SYNOPSIS
========

``netevent`` show *DEVICE* [\ *COUNT*\ ]

``netevent`` cat [\ *OPTIONS*\ ] *DEVICE*

``netevent`` create [\ *OPTIONS*\ ] *DEVICE*

``netevent`` daemon [\ *OPTIONS*\ ] *SOCKETNAME*

``netevent`` command *SOCKETNAME* *COMMAND*

OPTIONS
=======

Some options can be used on multiple commands.

All subcommands:
----------------

``-h, --help``
    Show a short usage message.

``netevent create``
----------------------------------------

``--duplicates=``\ *MODE*
    Change how duplicate devices are to be treated. *MODE* can be:

    ``reject``

        The default. If a device with an already existing ID is received, treat
        this as an error and exit.

    ``resume``

        Assume the source was restarted and is sending the same device again.
        Currently this does not verify whether that's actually the case.

    ``replace``

        Remove the previous device and replace it with the new one.
        Since ``resume`` does not verify the device, this is the preferred mode
        if the destination event device node does not need to be persistent.


``netevent cat`` and ``netevent create``
----------------------------------------

``-l, --legacy``
    Use a netevent 1 compatible protocol.

``--no-legacy``
    Use a netevent 2 compatible protocol. This is the default.

``netevent cat`` and ``netevent show``
--------------------------------------

``-g, --grab``
    Grab the input device to prevent it from also firing events on the system.
    This is the default.

``-G, --no-grab``
    Do not grab the input device.

``netevent daemon``
-------------------

``-s, --source=``\ *FILE*
    Run commands from the specified file. Can be specified multiple times.
    This can be used to fully setup the daemon with outputs, devices and
    hotkeys. See the `DAEMON COMMANDS` section for details.

DAEMON COMMANDS
===============

``nop``
    Nothing. Bind as hotkey to ignore an event and be explicit about it.

``grab``\  *on*\ \|\ *off*\ \|\ *toggle*
    Set the grabbing state. Currently this also controls whether events are
    passed to the current output.

``use`` *OUTPUT*
    Set the current output.

``output add`` [``--resume``] *NAME* *OUTPUT_SPEC*
    Add a new output. *NAME* can be an arbitrary name used later for
    ``output remove`` or ``use`` commands. *OUTPUT_SPEC* can currently be
    either a file/fifo, a command to pipe to when prefixed with *exec:*, or the
    name of a unix or abstract socket when using *unix:/path* or
    *unix:@abstractName*. See the examples above.

    If the ``--resume`` parameter is provided, assume the destination already
    knows all the existing devices and do not recreate them.

``output remove`` *NAME*
    Remove an existing output.

``output use`` *NAME*
    Long version of ``use`` *NAME*.

``exec`` *COMMAND*
    Execute a command. Mostly useful for hotkeys.

``source`` *FILE*
    Execute daemon commands from a file.

``quit``
    Cause the daemon to quit.

``hotkey add`` *DEVICE* *EVENT* *COMMAND*
    Add a hotkey to an existing device. *DEVICE* is the name used when
    adding the device via ``device add``. *EVENT* is an event specification
    of the form *TYPE*:*CODE*:*VALUE*, as printed out by ``netevent show``.
    *COMMAND* is a daemon command to be executed when the event is read.

``hotkey remove`` *DEVICE* *EVDENT*
    Remove a hotkey for an event on a device.

``device add`` *NAME* *EVENT_DEVICE_FILE*
    Register an evdev device.

``device remove`` *NAME*
    Remove an evdev device.

``info``
    Show current inputs, outputs, devices and hotkeys.

BUGS
====

Please report bugs to via https://github.com/Blub/netevent/issues\ .
