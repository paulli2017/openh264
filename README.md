OpenH264
========
OpenH264 is a codec library which supports H.264 encoding and decoding. It is suitable for use in real time applications such as WebRTC. See http://www.openh264.org/ for more details.

Experimental alpha encoder
-------------------------
This branch adds `ENCODER_OPTION_NEUTRAL_CHROMA` (`0x10000`, pointer to an `int`
containing 0 or 1). It is disabled by default. Enable it after `InitializeExt`
and after any LTR setup, before the first `EncodeFrame`.

The caller guarantees that every input U/V sample is 128. Supply valid I420
planes/strides; null chroma pointers are not supported. This initial version
requires one spatial layer, even fixed dimensions, and denoising disabled.
Reinitialize to change mode after encoding or to update parameter sets/LTR
configuration while the mode is enabled. Unsupported settings return an error.
No public parameter-structure layout or existing export changes are required.

When enabled (luma-only mode), the encoder does no chroma processing: chroma
intra mode search, residual transform/quantization/reconstruction, chroma motion
compensation, chroma deblocking, chroma reference padding and chroma skip SADs
are all bypassed. Because input and reconstructed chroma are always 128, each
bypassed step is an identity, so the output is unchanged. The bitstream is still
standard 4:2:0 with zero chroma residual (not 4:0:0) and decodes with an
unmodified decoder; decoded luma matches mode-off encoding and chroma is 128.

Windows x64 builds use Meson with MSVC and NASM on PATH. This branch corrects
the v2.4.1 Windows x86/x64 Meson flags: `X86_ASM` and `HAVE_AVX2` must be
defined for both C and C++, and `HAVE_AVX2` for NASM (`X86_32_ASM` also applies
to x86). Assembling the routines alone does not enable CPU detection/dispatch.
The existing runtime checks select supported instructions; generic C++ code
does not require a global `/arch:AVX2` flag.
Release builds also use `b_ndebug=if-release`, matching the upstream Makefile's
`-DNDEBUG` behavior. Debug/debugoptimized builds retain assertions. For an
already configured Release build, run `meson configure build-alpha -Db_ndebug=if-release`.

```powershell
meson setup build-alpha --buildtype=release -Dtests=enabled
meson compile -C build-alpha -j 8
.\build-alpha\test\api\test_api.exe --gtest_filter=Alpha/NeutralChromaTest.*
```

Keep an unmodified codec checkout from tag `v2.4.1` in a separate worktree.
Apply the same Meson flag correction to that control; do not apply the neutral
chroma changes. Build both with identical compiler/build settings.

`test\neutral_chroma_benchmark.cpp` is a Windows dynamic-DLL benchmark and
cross-decoder regression check. From an x64 Visual Studio developer shell:

```powershell
New-Item -ItemType Directory -Path artifacts -Force
cl /nologo /O2 /EHsc /std:c++17 /Icodec\api test\neutral_chroma_benchmark.cpp /Foartifacts\neutral_chroma_benchmark.obj /Feartifacts\neutral_chroma_benchmark.exe
.\artifacts\neutral_chroma_benchmark.exe <baseline.dll> <experimental.dll> <stock.dll> 1280 720 90
```

The original invocation compares source baseline, experimental mode off, and
experimental mode on over three synthetic-input runs, retaining the original
CSV columns. The benchmark now explicitly matches AlphaExamples' QP32, GOP60,
MEDIUM complexity, background detection, single-thread/single-slice defaults.
Adaptive quantization is requested but OpenH264 disables it in constant-QP
`RC_OFF_MODE`; the effective parameters are checked for every encoder. Earlier
synthetic measurements used the library's default complexity and are superseded.

For a real sequence, append a tightly packed, headerless R8 file and optionally
a repetition count. The requested number of frames is preloaded before timing:

```powershell
.\artifacts\neutral_chroma_benchmark.exe <baseline.dll> <experimental.dll> <stock.dll> 2560 1440 274 <raw-r8-file> 8
```

Raw mode adds stock encoding to the three controls, pins only the benchmark
thread to the first allowed logical CPU, and reports mean/p50/p95/p99 call time
and calling-thread CPU cycles. Each variant has independent encoder/reference
state. Execution order rotates each frame and repetition, and the first five
frames are excluded from timing. NAL copying, stock decoding, validation and
file I/O are outside the measured calls. Decoded Y is compared byte-for-byte
on every frame, decoded U/V must be 128, and every variant's encoded frame size
must match. Only current decoded frames are retained; full input is preloaded.
CPU-cycle results apply to the checked single-thread configuration, not to
total worker-thread CPU usage.

Encoder Features
----------------
- Constrained Baseline Profile up to Level 5.2 (Max frame size is 36864 macro-blocks)
- Arbitrary resolution, not constrained to multiples of 16x16
- Rate control with adaptive quantization, or constant quantization
- Slice options: 1 slice per frame, N slices per frame, N macroblocks per slice, or N bytes per slice
- Multiple threads automatically used for multiple slices
- Temporal scalability up to 4 layers in a dyadic hierarchy
- Simulcast AVC up to 4 resolutions from a single input
- Spatial simulcast up to 4 resolutions from a single input
- Long Term Reference (LTR) frames
- Memory Management Control Operation (MMCO)
- Reference picture list modification
- Single reference frame for inter prediction
- Multiple reference frames when using LTR and/or 3-4 temporal layers
- Periodic and on-demand Instantaneous Decoder Refresh (IDR) frame insertion
- Dynamic changes to bit rate, frame rate, and resolution
- Annex B byte stream output
- YUV 4:2:0 planar input

Decoder Features
----------------
- Constrained Baseline Profile up to Level 5.2 (Max frame size is 36864 macro-blocks)
- Arbitrary resolution, not constrained to multiples of 16x16
- Single thread for all slices
- Long Term Reference (LTR) frames
- Memory Management Control Operation (MMCO)
- Reference picture list modification
- Multiple reference frames when specified in Sequence Parameter Set (SPS)
- Annex B byte stream input
- YUV 4:2:0 planar output

OS Support
----------
- Windows 64-bit and 32-bit
- Mac OS X 64-bit and 32-bit
- Mac OS X ARM64
- Linux 64-bit and 32-bit
- Android 64-bit and 32-bit
- iOS 64-bit and 32-bit
- Windows Phone 32-bit

Architectures verified to be working
----------
- ppc64el

Processor Support
-----------------
- Intel x86 optionally with MMX/SSE (no AVX yet, help is welcome)
- ARMv7 optionally with NEON, AArch64 optionally with NEON
- Any architecture using C/C++ fallback functions

Building the Library
--------------------
NASM needed to be installed for assembly code: workable version 2.10.06 or above, NASM can be downloaded from http://www.nasm.us/.
For Mac OSX 64-bit NASM needed to be below version 2.11.08 as NASM 2.11.08 will introduce error when using RIP-relative addresses in Mac OSX 64-bit

To build the arm assembly for Windows Phone, gas-preprocessor is required. It can be downloaded from git://git.libav.org/gas-preprocessor.git

For Android Builds
------------------
To build for android platform, You need to install android sdk and ndk. You also need to export `**ANDROID_SDK**/tools` to PATH. On Linux, this can be done by

    export PATH=**ANDROID_SDK**/tools:$PATH

The codec and demo can be built by

    make OS=android NDKROOT=**ANDROID_NDK** TARGET=**ANDROID_TARGET**

Valid `**ANDROID_TARGET**` can be found in `**ANDROID_SDK**/platforms`, such as `android-12`.
You can also set `ARCH`, `NDKLEVEL` according to your device and NDK version.
`ARCH` specifies the architecture of android device. Currently `arm`, `arm64`, `x86` and `x86_64` are supported, the default is `arm`. (`mips` and `mips64` can also be used, but there's no specific optimization for those architectures.)
`NDKLEVEL` specifies android api level, the default is 12. Available possibilities can be found in `**ANDROID_NDK**/platforms`, such as `android-21` (strip away the `android-` prefix).

By default these commands build for the `armeabi-v7a` ABI. To build for the other android
ABIs, add `ARCH=arm64`, `ARCH=x86`, `ARCH=x86_64`, `ARCH=mips` or `ARCH=mips64`.
To build for the older `armeabi` ABI (which has armv5te as baseline), add `APP_ABI=armeabi` (`ARCH=arm` is implicit).
To build for 64-bit ABI, such as `arm64`, explicitly set `NDKLEVEL` to 21 or higher.

For iOS Builds
--------------
You can build the libraries and demo applications using xcode project files
located in `codec/build/iOS/dec` and `codec/build/iOS/enc`.

You can also build the libraries (but not the demo applications) using the
make based build system from the command line. Build with

    make OS=ios ARCH=**ARCH**

Valid values for `**ARCH**` are the normal iOS architecture names such as
`armv7`, `armv7s`, `arm64`, and `i386` and `x86_64` for the simulator.
Another settable iOS specific parameter
is `SDK_MIN`, specifying the minimum deployment target for the built library.
For other details on building using make on the command line, see
'For All Platforms' below.

For Linux Builds
--------------

You can build the libraries (but not the demo applications) using the
make based build system from the command line. Build with

    make OS=linux ARCH=**ARCH**

 You can set `ARCH` according to your linux device .
`ARCH` specifies the architecture of the device. Currently `arm`, `arm64`, `x86` and `x86_64` are supported   

 NOTICE:
 	If your computer is x86 architecture, for build the libnary which be used on arm/aarch64 machine, you may need to use cross-compiler, for example:
 		make OS=linux CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ ARCH=arm64
   		 or
    	make OS=linux CC=arm-linux-gnueabi-gcc CXX=arm-linux-gnueabi-g++ ARCH=arm


For Windows Builds
------------------

"make" must be installed. It is recommended to install the Cygwin and "make" must be selected to be included in the installation. After the installation, please add the Cygwin bin path to your PATH.

openh264/build/AutoBuildForWindows.bat is provided to help compile the libraries on Windows platform.  
Usage of the .bat script:  

    `AutoBuildForWindows.bat Win32-Release-ASM` for x86 Release build  
    `AutoBuildForWindows.bat Win64-Release-ASM` for x86_64 Release build  
    `AutoBuildForWindows.bat ARM64-Release-ASM` for arm64 release build  
for more usage, please refer to the .bat script help.  

For All Platforms
-------------------

Using make
----------

From the main project directory:
- `make` for automatically detecting architecture and building accordingly
- `make ARCH=i386` for x86 32-bit builds
- `make ARCH=x86_64` for x86 64-bit builds
- `make ARCH=arm64` for arm64 Mac 64-bit builds
- `make V=No` for a silent build (not showing the actual compiler commands)
- `make DEBUGSYMBOLS=True` for two libraries, one is normal libraries, another one is removed the debugging symbol table entries (those created by the -g option)

The command line programs `h264enc` and `h264dec` will appear in the main project directory.

A shell script to run the command-line apps is in `testbin/CmdLineExample.sh`

Usage information can be found in `testbin/CmdLineReadMe`

Using meson
-----------

Meson build definitions have been added, and are known to work on Linux
and Windows, for x86 and x86 64-bit.

See <http://mesonbuild.com/Installing.html> for instructions on how to
install meson, then:

``` shell
meson builddir
ninja -C builddir
```

Run the tests with:

``` shell
meson test -C builddir -v
```

Install with:

``` shell
ninja -C builddir install
```

Using the Source
----------------
- `codec` - encoder, decoder, console (test app), build (makefile, vcproj)
- `build` - scripts for Makefile build system
- `test` - GTest unittest files
- `testbin` - autobuild scripts, test app config files
- `res` - yuv and bitstream test files

Known Issues
------------
See the issue tracker on https://github.com/cisco/openh264/issues
- Encoder errors when resolution exceeds 3840x2160
- Encoder errors when compressed frame size exceeds half uncompressed size
- Decoder errors when compressed frame size exceeds 1MB
- Encoder RC requires frame skipping to be enabled to hit the target bitrate,
  if frame skipping is disabled the target bitrate may be exceeded

License
-------
BSD, see `LICENSE` file for details.
