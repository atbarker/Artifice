#!/usr/bin/env python3

import gi

gi.require_version('Gtk', '3.0')
gi.require_version('GLib', '2.0')
gi.require_version('Gio', '2.0')
gi.require_version('UDisks', '2.0')
gi.require_version('GUdev', '1.0')

from gi.repository import Gtk, GLib, Gio, UDisks, GUdev

gio_volume_monitor = Gio.VolumeMonitor.get()
gio_volumes = gio_volume_monitor.get_volumes()

udisks_client = UDisks.Client.new_sync()
manager = udisks_client.get_manager()
udev_client = GUdev.Client()

for v in gio_volumes:
    device_file = v.get_identifier(Gio.VOLUME_IDENTIFIER_KIND_UNIX_DEVICE)
    if not device_file:
        continue

    print(device_file)

    udev_volume = udev_client.query_by_device_file(device_file)
    if not udev_volume:
        print("!!! {}: no udev volume".format(device_file))

    udisks_block = udisks_client.get_block_for_dev(udev_volume.get_device_number())
    object_path = udisks_block.get_object_path()
    udisks_object = udisks_client.get_object(object_path)
    print(udisks_object)
    print(" ---- ")

print(dir(Gtk.MountOperation().do_ask_password()))
