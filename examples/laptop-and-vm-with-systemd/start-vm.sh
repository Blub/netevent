#!/bin/sh

### Variant A:
### If we're using the udev rules & systemd service file:
###     dev-input-by\x2dname-vm0\x2dkbd.device
### and dev-input-by\x2dname-vm0\x2dmouse.device
mouse_device="/dev/input/by-name/vm0-mouse"
keyboard_device="/dev/input/by-name/vm0-kbd"
###
###
###
### Variant B:
### Alternatively:
### When not using udev rules + systemd service files, find the devices by
### name via sysfs:
for i in /sys/class/input/event*; do
  name="$(<$i/device/name)"
  if [[ $name = vm0-kbd ]]; then
    keyboard_device="/dev/${i#/sys/class/}"
  elif [[ $name = vm0-mouse ]]; then
    mouse_device="/dev/${i#/sys/class/}"
  fi
done
if [[ -z $keyboard_device || -z $mouse_device ]]; then
  die "usage: vm0-kbd/mouse missing"
fi
###
###
###

exec qemu-system-x86_64 \
  <actual arguments> \
  -device usb-tablet \
  -object "input-linux,id=ev-kbd,evdev=$keyboard_device,repeat=on" \
  -object "input-linux,id=ev-mouse,evdev=$mouse_device"

# Be aware of issues with repeat=on in some qemu versions...
