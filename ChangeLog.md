# Release 2.2.3

* Fix compilation on 32bit devices. ([#31)
* Fix non-abstract unix sockets, they were accidentally removed on connect. ([#32], [#33])
* Use more portable sed expression for getting `NETEVENT_VERSION` from this
  file, to fix freebsd builds. ([#34], [#35])
* Fix build with new clang warnings.

[#31]: https://github.com/Blub/netevent/pull/31
[#32]: https://github.com/Blub/netevent/issues/32
[#33]: https://github.com/Blub/netevent/pull/33
[#34]: https://github.com/Blub/netevent/issues/34
[#35]: https://github.com/Blub/netevent/pull/35

# Release 2.2.2

This is a maintenance release to fix builds with newer compilers.

# Release 2.2.1

* add `-V`/`--version` command line option. (#22)

# Release 2.2

* Writing outputs and grabbing input is now managed separately.
* The `grab` command is deprecated and split into the following two new
  commands:
  * `grab-devices` for input device grabbing.
  * `write-events` for sending the events.
* support out of tree builds

# Prior to 2.2

See git history.
