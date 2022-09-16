# OpenGL samples for Wayland

These programs are OpenGL samples for Wayland.

## Build

Use poky SDK for build.

Assume that the poky SDK is installed in /opt/poky/VERSION/.

```
$ . /opt/poky/VERSION/environment-setup-aarch64-poky-linux
$ meson --build.pkg-config-path ${OECORE_NATIVE_SYSROOT}/usr/lib/pkgconfig build/
$ ninja -C build
```

## run

Place images in the working directory (where you want to run the program).
For example, place the built program in the working directory (/path/to/work) and run the program.

example:
```
$ cp build/gl-* /path/to/work/
$ cp -arp images /path/to/work/
$ cd /path/to/work
$ ./gl-bullet
```

## License

All files in pvrscope folder are distributed in accordance with the [PowerVR Tools Software End User Licence Agreement](https://developer.imaginationtech.com/terms/powervr-tools-software-eula/). All other files are distributed under the MIT License.
