# Installing the firmware on the satellite, step by step

For someone who has flashed the kit's original firmware once and nothing more. Every step says
what to do and what should happen. If something does not happen as written, stop and send the
last 30 lines of the terminal.

## Before you start

- A computer with internet access. Windows, Mac or Linux, it does not matter.
- The same USB adapter and cable you used to flash the original ESP32-CAM firmware.
- A USB cable for the Nano, only for the last step, which is optional.
- The satellite with a charged battery, and the solar wings free to move. The new firmware moves
  the wings about 12 seconds after power-up.

## Step 1: install VS Code

1. Go to https://code.visualstudio.com and download the version for your system.
2. Install it with the defaults and open it.

## Step 2: install PlatformIO inside VS Code

1. In the left sidebar click the Extensions icon, the four squares.
2. In the search box type `PlatformIO IDE` and click Install on the PlatformIO result.
3. Wait. It takes a few minutes, and ends with a notification in the bottom-right corner that the
   installation is complete.
4. Close VS Code and open it again. A new icon appears in the sidebar, an alien head. That is
   PlatformIO.

## Step 3: download the code

1. Open https://github.com/Shahar373/shahar-mysat, click the green **Code** button, then
   **Download ZIP**.
2. Extract it. You get a folder with a long name. Rename it to `mysat` and move it somewhere
   simple, for example `C:\mysat` on Windows or `~/mysat` on a Mac.
3. Check that the folder contains a file called `platformio.ini`. If it is one folder deeper, that
   inner folder is the one you want.

## Step 4: open the project

1. In VS Code: menu File, then Open Folder, and choose the `mysat` folder.
2. If you are asked whether you trust the authors of the folder, answer Yes.
3. In the status bar at the bottom PlatformIO starts configuring the project. The first time it
   downloads the ESP32 build tools, close to a gigabyte. Wait until the status bar goes quiet.
   This can take ten minutes.

## Step 5: build without connecting anything

1. Click the alien icon in the sidebar.
2. A tree called PROJECT TASKS opens. Expand `obc`, then `General`, and click `Build`.
3. A terminal opens at the bottom and a lot of lines scroll past. At the end there must be a line
   with `SUCCESS` and a duration.
4. If it says `FAILED` instead, copy the last 30 lines and send them. Do not continue.

## Step 6: connect the ESP32-CAM in flashing mode

Exactly as for the original firmware, following "Microcontroller Setup" in the kit's guide. As a
reminder of what happens there:

1. Connect the adapter to the computer and to the board.
2. Put the ESP32-CAM into flashing mode: the IO0 pin connected to GND (or the IO0 button held
   down, if your adapter has one) while you press RESET.
3. On Windows open Device Manager and check that a new COM port appears under Ports. On a Mac
   run `ls /dev/cu.*` in a terminal and look for usbserial. If nothing appears, the adapter's
   driver is missing (CH340 or CP2102, depending on the chip on it).

## Step 7: flash the ESP32-CAM

1. In PROJECT TASKS: `obc`, then `General`, then `Upload`.
2. The terminal shows `Connecting.....` with dots. If the dots go on for more than ten seconds,
   press RESET on the board while IO0 is still connected to GND.
3. After some tens of seconds of percentages you get `Hash of data verified` and then `SUCCESS`.
   The upload is deliberately set to a slow, dependable speed, so a one-megabyte image takes about
   a minute and a half.
4. Disconnect IO0 from GND and press RESET.

## Step 8: flash the dashboard files

Not required, but worth it. Without this there is a compact built-in page that also works.

1. Flashing mode again, as in step 6.
2. In PROJECT TASKS: `obc`, then `Platform`, then `Upload Filesystem Image`.
3. Wait for `SUCCESS`, disconnect IO0, press RESET.

## Step 9: see that the satellite is alive

1. In PROJECT TASKS: `obc`, then `General`, then `Monitor`. A text window opens. The speed is
   already set.
2. Press RESET on the board. Lines should appear, among them `MYSAT OBC 2.1.0-demo` and the list
   of sensors.
3. A line like `assuming the stock Nano firmware` is normal: the satellite has recognised that the
   Nano still runs the original firmware, and it works with it.
4. Warning: about 12 seconds after RESET the wings deploy. That is the normal launch sequence.
   The wings must be free.
5. In this window you type commands and press Enter. Type `status` and you get a telemetry
   screen. Type `help` and you get the list.

## Step 10: run the demonstration

1. Type `demo on` and press Enter. From now on pulling the pin runs the demonstration instead of
   the launch sequence, and this is remembered across reflashes.
2. Type `demo` and you see the schedule: a 5-second countdown, wings twice, light three times.
3. Type `demo run` to see it now. Keep your hands away from the wings.
4. Disconnect the adapter from the satellite, insert the pin, wait a few seconds, pull the pin.
   After 5 seconds of a white blink once per second, the demonstration starts.

## Step 11 (recommended, not required): flash the Nano

With the Nano's original firmware the demonstration works, but every wing movement is a full
sweep to the end stop, and there is no confirmation that the movement happened. The new Nano
firmware gives gentle sweeps that stop short of the end stop, and an angle confirmation after
every movement.

1. Connect the Nano to the computer with its own USB cable. No special flashing mode is needed.
2. In PROJECT TASKS: `aux`, then `General`, then `Upload`.
3. If you get an error containing `not in sync`, your Nano has the old bootloader. Open
   `platformio.ini`, replace `nanoatmega328new` with `nanoatmega328`, save and try again.
4. After `SUCCESS`, press RESET on the ESP32-CAM. The monitor should show the line
   `v2 firmware detected`.

## If something gets stuck

| Symptom | What it means and what to do |
|---|---|
| No COM port or usbserial | The adapter's driver. Search by the chip on it (CH340 or CP2102), install, reconnect. |
| `Connecting.....` forever | The board is not in flashing mode. IO0 to GND, then RESET, try again. |
| `Timed out waiting for packet header` | A weak cable or adapter. Try another cable, or another USB socket, not through a hub. |
| The satellite reboots in a loop after flashing | Usually a weak battery or a cable that cannot supply the current. Charge and try again. |
| `FAILED` in the build | Send the last 30 lines of the terminal. |
| The wings do not move at all | Check the `aux` line in `status`. If it says `no link`, the Nano is not answering on I2C. Make sure it is powered (its LED blinks). |

## Useful commands afterwards

```
demo            # demonstration status and its schedule
demo run        # run it now
demo stop       # stop it
demo off        # back to the normal deployment when the pin is pulled
params list     # every setting
params set demo.light_on_ms 5000     # for example: light on for 5 seconds
```

The dashboard: join the WiFi network the satellite creates, `MYSAT-1-XXXX`, and browse to
`http://192.168.4.1`.
