#!/usr/bin/env python3

from gi.repository import GLib
from pydbus import SystemBus

import subprocess

loop = GLib.MainLoop()

MODE_CREATE = 0
MODE_MOUNT  = 1
MODE_SHADOW = 2

KiB = 1024
MiB = KiB * 1024
GiB = MiB * 1024

AFS_FULL_PREFIX = "/dev/mapper/artifice_"
AFS_PREFIX = "artifice_"

DEFAULT_ENTROPY_PATH = "/home"

def build_table(size, mode, passphrase, underlying_device, entropy):
    return "0 {} artifice {} {} {} --entropy {}".format(size, mode, passphrase, underlying_device, entropy)

class ArtificeOperation(object):
    """
            <node>
                    <interface name='edu.ucsc.ssrc.artifice.ArtificeOperation'>
                            <method name='Create'>
                                    <arg type='s' name='device' direction='in'/>
                                    <arg type='s' name='password' direction='in'/>
                                    <arg type='s' name='underlying_device' direction='in'/>
                                    <arg type='b' name='response' direction='out'/>
                            </method>
                            <method name='MountOrCreate'>
                                    <arg type='s' name='device' direction='in'/>
                                    <arg type='s' name='password' direction='in'/>
                                    <arg type='s' name='underlying_device' direction='in'/>
                                    <arg type='b' name='response' direction='out'/>
                            </method>
                            <method name='Remove'>
                                    <arg type='s' name='device' direction='in'/>
                                    <arg type='b' name='response' direction='out'/>
                            </method>
                            <method name='Mount'>
                                    <arg type='s' name='device' direction='in'/>
                                    <arg type='s' name='password' direction='in'/>
                                    <arg type='s' name='underlying_device' direction='in'/>
                                    <arg type='b' name='response' direction='out'/>
                            </method>
                            <method name='MkfsExt4'>
                                    <arg type='s' name='device' direction='in'/>
                                    <arg type='b' name='response' direction='out'/>
                            </method>
                            <method name='Quit'/>
                    </interface>
            </node>
    """

    def _dm_create(self, mode, device, password, underlying_device):
        # TODO: Determine size that we want to make
        table = build_table(100 * MiB, mode, password, underlying_device, DEFAULT_ENTROPY_PATH)
        proc = subprocess.run(["dmsetup", "create", AFS_PREFIX + device, "--table", table])
        return proc.returncode == 0

    def _dm_remove(self, device):
        proc = subprocess.run(["dmsetup", "remove", AFS_PREFIX + device])
        return proc.returncode == 0

    def _create(self, device, password, underlying_device):
        return self._dm_create(MODE_CREATE, device, password, underlying_device)

    def _mount(self, device, password, underlying_device):
        return self._dm_create(MODE_MOUNT, device, password, underlying_device)

    def Create(self, device, password, underlying_device):
        print("Got create request for {} {}".format(device, underlying_device))
        return self._create(device, password, underlying_device)

    def MountOrCreate(self, device, password, underlying_device):
        print("Got mount or create request for {} {}".format(device, underlying_device))
        if self._mount(device, password, underlying_device):
            print("Mounted successfully")
            return True

        return self._create(device, password, underlying_device)

    def Remove(self, device):
        return self._dm_remove(device)

    def Mount(self, device, password, underlying_device):
        return self._mount(device, password, underlying_device)

    def MkfsExt4(self, device):
        proc = subprocess.run(["mkfs.ext4", AFS_FULL_PREFIX + device])
        return proc.returncode == 0

    def Quit(self):
        """removes this object from the DBUS connection and exits"""
        loop.quit()

bus = SystemBus()
bus.publish("edu.ucsc.ssrc.artifice.ArtificeOperation", ArtificeOperation())
print("artificed started")
loop.run()
