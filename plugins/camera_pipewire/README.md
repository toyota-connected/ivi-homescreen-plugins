# Camera Plugin [WIP]

This plugin is used with the pub.dev package `camera`
https://pub.dev/packages/camera

This file contains the development plan for the Camera Plugin project.

## Current Progress

This camera plugin provides the following functions in the functional unit test case:

Enumerates available camera devices.

Displays a live preview from the selected camera.

Supports pausing and resuming the preview image.

Saves a picture from the current camera stream.

## Functional Test Case

https://github.com/toyota-connected/tcna-packages/tree/main/packages/camera/camera_linux/example

## Error enumerating cameras

If you see errors in homescreen log similar to:

```
/dev/media0[]: Failed to open media device at /dev/media0: Permission denied
```

Likely the user needs to be added to the `video` group:

```
sudo usermod -a -G video $LOGNAME
```

Reboot after. Logout/login will not always work.