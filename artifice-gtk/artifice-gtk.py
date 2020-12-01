#!/usr/bin/env python3

import argparse
import logging
from typing import List
import sys
import signal

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('UDisks', '2.0')
gi.require_version('GUdev', '1.0')
from gi.repository import Gtk, Gio

from artifice.volume_manager import VolumeManager
from artifice.exceptions import AlreadyUnlockedError


logger = logging.getLogger(__name__)


class App(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="edu.ucsc.ssrc.artifice-gtk", flags=Gio.ApplicationFlags.HANDLES_OPEN)
        self.manager = None  # type: VolumeManager

    def do_activate(self):
        if self.manager:
            # Raise window of the primary instance
            self.manager.window.present()
        else:
            self.manager = VolumeManager(self)

    def do_open(self, files: List[Gio.File], n_files, hint: str):
        logger.debug("in do_open. files: %s", files)

        # Show the window before unlocking the files
        self.activate()

        for file in files:
            try:
                self.manager.unlock_file_container(file.get_path(), open_after_unlock=True)
            except AlreadyUnlockedError:
                self.manager.open_file_container(file.get_path())


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("PATH", nargs="*", help="file containers to unlock")
    return parser.parse_args()


def init(args):
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)
    logger.debug("args: %r", args)


def main():
    args = parse_args()
    init(args)
    app_args = sys.argv[:1] + args.PATH

    # Workaround for https://bugzilla.gnome.org/show_bug.cgi?id=622084
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    app = App()
    app.run(app_args)


if __name__ == "__main__":
    main()
