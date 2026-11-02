# DeejMix

**Physical controls for your desktop audio.**

DeejMix is my take on a desktop mixer for deej-style hardware. It connects the knobs and sliders on a USB microcontroller to application, master output, and microphone volumes, with a Qt interface that shows what each channel is doing.

This version brings together the Windows audio backend and ongoing Linux support, so I can keep developing the mixer around my own setup.

## What it does

- Assign physical channels to running audio applications, master output, or microphone input.
- Detect serial hardware automatically at 9600 or 115200 baud.
- Switch between blue slider and purple knob views, with configurable accent colors.
- Adjust hardware value bounds when a control does not reach its full range.
- Save settings and channel assignments locally in SQLite.
- Keep channel assignments when an application stops playing or closes, and restore them after restarting DeejMix.
- Keep the mixer running in the system tray.

## Platform support

| Platform | Audio backend | Notes |
| --- | --- | --- |
| Windows 10/11 | Windows Core Audio | Includes application process names and executable icon extraction. |
| Linux | PulseAudio client API | Also works with PipeWire through its PulseAudio compatibility server. Support is under development. |

On Linux, application names come from active playback streams. Start playing audio in an application if it does not appear in the list. The Linux backend currently uses linear volume scaling, even when the logarithmic option is enabled.

## Build from source

Run the following commands from the directory containing `CMakeLists.txt`.

You will need a C++17 compiler, CMake 3.16 or newer, and Qt with the **Widgets**, **Sql**, **SerialPort**, and **Network** modules. Qt 6 is the intended setup; the CMake configuration also accepts Qt 5. The Qt SQLite driver is required at runtime.

### Linux

Install the dependencies with your distribution's package manager. In addition to Qt and the build tools, you need `pkg-config` and the PulseAudio development library (`libpulse`). A running PulseAudio server or PipeWire PulseAudio compatibility server is required for audio control.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/DeejMix
```

If CMake cannot locate Qt, set `CMAKE_PREFIX_PATH` to your Qt installation when configuring the build.

### Windows

Use Qt Creator with a matching Qt compiler kit, or configure from a shell where your compiler and Qt tools are available:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The executable location depends on the CMake generator: typically `build/DeejMix.exe` or `build/Release/DeejMix.exe`.

For a portable Windows folder, [deploy.ps1](deploy.ps1) builds a release and bundles Qt dependencies with `windeployqt`. Update the Qt and MinGW paths in the script to match your installation before running it. The script recreates the `DeejMix_Release` output folder.

## Connect your hardware

Use a microcontroller running firmware that sends deej-style serial readings. Each line contains one value per channel, separated by `|`, with values from **0 to 1023** and a newline at the end:

```text
0|256|512|1023
```

The application accepts the standard `\r\n` line ending. Hardware firmware and wiring instructions are maintained separately in the [original deej project](https://github.com/omriharel/deej).

1. Connect the controller over USB and launch DeejMix.
2. Wait for serial detection and the channel displays to appear.
3. Play audio in the applications you want to control, then assign them to channels in either slider or knob view.
4. Move each physical control to check the assignment.
5. Adjust the hardware value bounds in settings if the controls cannot reach minimum or maximum volume.

On Linux, your user must have permission to access the controller's serial device. System tray availability depends on your desktop environment.

An assigned application stays selected while its audio stream is absent. When it returns, DeejMix reapplies the physical control's volume on the next audio poll (about two seconds). Choose another application to replace an assignment, or choose **Select App...** to clear it.

## Local settings

DeejMix stores `deejmix.db` in the application data directory selected by Qt for your operating system. It holds settings and channel assignments and is separate from the source directory.

## Development notes

- [src/](src/) contains the interface, audio backend, serial connection, and database code.
- [resources/](resources/) contains the Qt resources and stylesheet.
- [CMakeLists.txt](CMakeLists.txt) defines the build and platform dependencies.
- [check_ports.ps1](check_ports.ps1) is a Windows serial-port helper.

The `--selftest` argument exercises the audio backend. **It changes the master volume and, when available, the first application's volume.** It does not restore the previous levels.

Regression tests use Qt Test and run with an isolated settings directory and no connection to your PulseAudio server:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Credits

DeejMix builds on the idea behind [deej by omriharel](https://github.com/omriharel/deej): using affordable physical controls to manage desktop audio. This version keeps that practical approach while developing its own Qt interface and platform integration.
