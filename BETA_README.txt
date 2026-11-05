MonitorMage - Read me
First beta release - package 0.111.2; application version 0.111.0

MonitorMage changes the colors and brightness of your screen. It stays near
the Windows clock so you can leave your chosen look on while using other apps.


REQUIREMENTS AND LIMITS

Use a 64-bit Windows PC running Windows 10 or Windows 11. HDR gamma currently
requires a supported NVIDIA display configuration; HDR on every graphics card
is not promised. The HDR tests so far used a GTX 1660 and LG OLED B2 on Windows 10.

Saturation and Channel mixer apply to all displays and can be skipped by some
fullscreen games. Windows Color filters remains a separate Windows setting.
Imported LUTs must describe separate red, green and blue curves; general 3D
color-grading LUTs are not supported. Gamut expansion is not included yet.


GET STARTED

1. If MonitorMage is already running, right-click its icon near the Windows
   clock and choose Exit.
2. Extract the whole downloaded ZIP into a folder you can save files in,
   such as a folder inside Documents. Do not run it from inside the ZIP.
3. Open the extracted folder and double-click MonitorMage.exe. There is no
   installer, and you do not need to install .NET.
4. Choose Gentle contrast from the dropdown near the top of the window.
   On a fresh start, MonitorMage is on but its effects are off until you choose
   a look or turn an effect on.
5. Click Contrast curves on the left to see its controls. Move Contrast a
   little and compare the picture.
6. Click the power icon to turn MonitorMage's adjustments off, then on again
   to compare. You can also right-click the tray icon and choose Master power.

To keep your settings when moving to a new folder: exit MonitorMage, copy
MonitorMage.ini and your saved looks from the old presets folder to the new
folder, then open the new MonitorMage.exe. Settings are stored beside the app.

OPEN, HIDE OR EXIT

- Left-click the MonitorMage icon near the clock to open the editor. If you
  cannot see it, click the small arrow near the clock to show hidden icons.
- The window's X hides the editor; MonitorMage keeps running in the tray.
- Right-click the tray icon and choose Exit to stop MonitorMage.
- Hover over an icon to see its short label. Tab moves between controls;
  Shift+Tab moves back. Small desktops get scrollbars when needed.

TRY AND SAVE A LOOK

1. Choose a look from the top dropdown. Try Gentle contrast, Warm glow,
   Grayscale or Red filter B&W.
2. Click an effect's name on the left to open its settings. Its switch turns
   that effect on or off. You can combine several effects.
3. Move the sliders. The small mark on each track shows its default value.
   Right-click or double-click a slider to reset that one value.
4. Click Save as, enter a name and save your new look.
5. Use Save to keep later changes to that look. With Autosave checked, changes
   are saved automatically. Leave it unchecked if you want to experiment
   without changing the saved look. Revert goes back to the saved settings.

Reset below an effect's name resets only that effect's settings.
In Custom curves, drag a point to change the curve. Double-click a point to
reset it, or right-click the graph to reset the whole selected curve.

To share a look, click Settings, then Preset folder, and find its saved file
under looks. Give that file to the other person. To add a shared look, put it
in your presets/looks folder and reopen the editor.

Export saves color curves only. Use the saved look file to share Saturation
and Channel mixer too. The graph also shows only the color curves.

MAKE A BLACK-AND-WHITE LOOK

1. Choose Red filter B&W from the look dropdown, or turn on Channel mixer.
2. Open Channel mixer on the left.
3. Adjust Red, Green and Blue. Increasing a color's contribution makes things
   of that color lighter in the black-and-white picture.
4. Compare with the power button, then use Save as to keep a look you like.

The mix is balanced so white stays white. Setting all three sliders to zero
uses the standard mix. Reset restores the default mix.
Saturation changes color strength: 0% is black and white, 100% leaves colors
unchanged, and higher values make colors stronger. Very strong settings can
lose detail in bright colors.
Saturation and Channel mixer affect all displays, even if you select just
one display for the other effects.

CHOOSE A DISPLAY AND USE HDR

1. If the display dropdown below the drawn monitor is unavailable, turn
   MonitorMage's power off temporarily.
2. Choose the display you want from that dropdown.
3. Turn MonitorMage's power back on.

If Windows duplicates the same picture on two screens, some adjustments must
change both together. They appear as a combined display in the dropdown.

The sun icon shows whether Windows HDR is on for the selected display.
Click it, or choose Windows HDR from the tray menu, to change that setting
where your display supports it.
HDR gamma under Corrections adjusts ordinary content while Windows HDR is
on. It normally pauses when HDR is off. Also apply in SDR mode lets you use
it with HDR off as well.

SET UP NIGHT LIGHT

Night light makes the picture warmer. MonitorMage's version works even when
Windows does not offer its own Night light option.

1. Open Night light under Corrections and turn its switch on.
2. Adjust Warmth to a comfortable level.
3. For automatic evening use, enable the schedule and set From and Until.
   Enter times in 24-hour form, for example 21:00 and 07:00.
4. Adjust Fade minutes if you want it to change gradually.
5. Use the display selector on this page and its inclusion checkbox to choose
   which displays receive Night light. Duplicated screens change together.

Bypass look pauses your creative effects but keeps Night light and monitor
corrections. Master power pauses MonitorMage's adjustments, including Night light.
Night light settings are kept separately from shared looks.
If Windows Night light is also on, the two warmth adjustments can add together.
Windows Color filters has its own switch under Corrections. It is a Windows
setting: use that switch or Windows Settings to turn it off. Closing MonitorMage
or using master power does not turn Windows Color filters off.

USE A DIFFERENT LOOK FOR A PROGRAM

1. Save the look you want to use for that program.
2. Click the diamond icon, or the profile text directly below it, to open
   Application profiles.
3. Check Automatically select application profiles.
4. Click Running programs to choose a program that is already open. If it is
   missing, try Background apps. Browse lets you select its program file.
5. Set Action to Use look, then choose your saved look under Look.
6. Click Add / update rule, then Apply.
7. Switch to that program. When you switch away, MonitorMage restores the look
   you were using before. The profile text below the diamond shows what is active.

Other Action choices can pause creative effects, pause MonitorMage, or pause
HDR gamma while that program is in front.

START WITH WINDOWS WITHOUT OPENING THE EDITOR

1. Open Settings using either gear icon.
2. Check Start with Windows to launch MonitorMage when you sign in.
3. Check Start minimized to start quietly in the tray.
4. Left-click the tray icon whenever you want to open the editor.

Your chosen look still applies when starting in the tray. The editor is loaded
when you open it, so you can leave MonitorMage running without opening a window.

Start minimized also applies when you open MonitorMage manually. If you move
MonitorMage to another folder, turn Start with Windows off and on again from
the new copy so Windows knows where to find it.

IF SOMETHING DOES NOT LOOK RIGHT

- Check that master power and the effect's own switch are both on. A saved
  look can have some or all effects switched off.
- If a game changes resolution, wait about three seconds. MonitorMage tries
  briefly to restore the color effects after the change.
- If colors still do not return, try Bypass look, then turn bypass off again.
  Its keyboard shortcut is shown in Settings and the tray menu.
- Some games' fullscreen modes ignore Saturation and Channel mixer, even
  when other effects work. Try the game's Borderless or Windowed option.
- Magnifier or another color-adjustment app may conflict with color effects.
  After changing or closing it, return to MonitorMage and try again.
- If the window stays too small after a resolution change, right-click its
  title bar and choose Restore. Scrollbars and Tab can help reach controls.
- For a color-effect problem, open Settings and click Color effect report.
  This saves and opens a local report; nothing is uploaded automatically.
  Read it before sharing, as it includes display details and program names.

Extra MonitorMage processes in Task Manager are normal. They help restore your
previous colors if the main app crashes. They start and stop automatically;
you only need to open MonitorMage.exe once.

See Test checklist.txt for short checks and what to include when reporting
a problem.



LICENSE AND PROJECT

LICENSE.txt contains the distribution terms. Keep it with copies of the app.
Project, releases and issue reports: https://github.com/CeeJayDK/MonitorMage
