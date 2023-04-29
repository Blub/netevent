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
