kbdscroll
=========

Summary
-------
Scrolling and cursor movement from a capacitive keyboard, a trackpad or a
navigation pad, for LuneOS.

Description
-----------
A number of phones carry a touch-sensitive surface that is not the
touchscreen: the capacitive keyboard of a BlackBerry KEYone, KEY2 or
Passport, the keys of a Unihertz Titan or Titan Pocket, a trackpad beside
the keys, a navigation pad. The kernel exposes it as an ordinary evdev
device, and nothing in a Wayland stack does anything with it.

`kbdscroll` reads that surface and replays a one-finger slide as a finger
drag on the touchscreen, so every toolkit (Mojo, Enyo, QML, Chromium)
scrolls and flings exactly as it would under a real finger, with no toolkit
or compositor support. Each slide is locked to the axis it starts on: an up
or down slide drags from the middle of the screen, a sideways one from the
edge it moves away from, so a pager has the whole width to travel. Typing
always wins: a key press during a slide, or just before it, cancels the
gesture. With the cursor modifier held (Alt by default) the same slide sends
arrow keys instead, which moves the text cursor.

Nothing is device-specific. The input devices are found by capability and
by name, every number is a setting, and the defaults are worked out from the
geometry of the hardware that was found.

Surfaces read: multitouch protocol B (slots), multitouch protocol A (no
slots, a lift being a frame with no contact in it - the Titan's `mtk-pad`),
single-touch `ABS_X`/`ABS_Y` with `BTN_TOUCH`, and relative pads, whose
gesture boundary is synthesised from stillness (the Zinwa Q25's trackpad).
The injected contact is written into the touchscreen's own device in that
screen's protocol, or into a virtual touchscreen of its own (`screen =
uinput`). On a screen with one contact stream (protocol A, or a single slot)
nothing is injected while a real finger is down, and a finger that lands
mid-drag takes the stream over.

Devices with a profile under `profiles/`:

- BlackBerry KEY2 (`athena`)
- Zinwa Q25 (`q25`): switched off, the compositor already makes the trackpad
  a pointer
- Unihertz Titan Pocket (`titan_pocket`)

The Unihertz Titan needs no profile: what is found automatically is right.

Configuration
-------------
`/etc/kbdscroll.conf` is the distro default, with every setting commented out
and explained. Per-machine settings go in a drop-in under
`/etc/kbdscroll.conf.d/`; later files win, so `90-local.conf` overrides a
shipped `10-<machine>.conf`. Profiles are installed under
`/etc/kbdscroll.conf.d/by-codename/` and the one matching the
`LUNEOS_DEVICE_CODENAME` in `/run/luneos-device/device.env` (written by
luneos-device-config) is read at run time, so one generic image carries them
all. `KBDSCROLL_<SETTING>` in the environment overrides the files, and
`kbdscroll key=value` on the command line overrides everything.

    kbdscroll --list      what was found, what was chosen, and why
    kbdscroll --help      every setting with its current value
    kbdscroll --debug     log every contact and decision

Porting to a new device: run `kbdscroll --list`. If the surface and the
touchscreen were told apart correctly there is usually nothing else to do;
if not, set `pad` and `screen` by name. Then check the direction with
`--debug`, correct the mounting with `pad-rotate`, `pad-invert-x` and
`pad-invert-y`, and only then touch `scale` and `slop`.

Building
--------
CMake, with libc and the kernel's evdev and uinput headers only:

    cmake -B build
    cmake --build build
    ctest --test-dir build
    cmake --install build

`SYSTEMD_UNITDIR` sets where `kbdscroll.service` goes (default
`<prefix>/lib/systemd/system`), and `-DBUILD_TESTING=OFF` leaves the tests
out. The daemon needs read access to `/dev/input/event*` and write access to
the touchscreen's node or to `/dev/uinput`; the unit runs it as root.

Tests
-----
`tests/test_kbdscroll.c` includes the daemon and drives its protocol handling
directly: the frames it writes to protocol A and one-slot screens, its view of
a real finger on them, and protocol A surfaces, from frames recorded on the
hardware. `tests/run-host-tests.sh` runs it with gcc and clang, plain and
under AddressSanitizer and UndefinedBehaviorSanitizer.

License
-------
MIT; see `LICENSE`.
