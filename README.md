# NikonTether

Linux tethered-capture app for Nikon cameras over USB. Live view, remote
control of exposure/WB/quality/AF, and automatic download of NEF RAW files into
a dated session folder with an instant preview (extracted from the JPEG that
Nikon embeds in every NEF).

Built with **C++/Qt 6** and **libgphoto2** (PTP).

## Dependencies

```bash
sudo apt install libgphoto2-dev qt6-base-dev qt6-base-dev-tools \
                 build-essential cmake ninja-build
# optional, for debugging the camera link:
sudo apt install gphoto2
```

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/nikontether
```

## Usage

1. Plug in the Nikon body via USB and power it on.
2. Press **Connect**. The control combos populate from what the camera reports.
3. Press **Live View** to stream the sensor feed. Click the feed to set the AF
   point (on supported bodies).
4. Adjust Shutter / Aperture / ISO / White Balance / Quality from the dock.
5. Press **Capture**. The NEF downloads into
   `~/Pictures/Tether/<date>_<session>/` and its embedded preview appears in the
   filmstrip and the large preview window.
6. **New Session…** starts a fresh dated folder.

### If Connect fails with a USB claim error

The Linux desktop may auto-mount the camera and hold the device. Unmount it:

```bash
gio mount -u gphoto2://$(gphoto2 --auto-detect | awk -F'usb:' 'NR>2{print "["$2"]"}' | head -1)
# or simply:
killall gvfsd-gphoto2
```

## Notes

- Nikon models name their PTP config widgets inconsistently. On connect the app
  probes several candidate names per control (see `kLogicalControls` in
  `src/camera/CameraWorker.cpp`) and binds to whatever the body actually
  exposes. Run `gphoto2 --list-config` to see your model's exact names.
- All libgphoto2 access runs on a dedicated worker thread; the GUI talks to it
  only through queued signals/slots (`CameraController`).
