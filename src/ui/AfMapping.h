#pragma once

// Pure, Qt-free mapping from a click in the drawn (letterboxed) live-view
// image to the camera's AF coordinate frame. Kept dependency-free so it can be
// unit-tested without a QApplication.
//
// The displayed image and the AF frame both cover the full live-view field of
// view, so a normalized position within the drawn rect maps directly to the AF
// frame. fw/fh is the AF coordinate frame size (Nikon header ImageWidth/Height),
// which libgphoto2 does not expose, hence it is a user-adjustable setting.
namespace afmap {

struct Result {
    int x = 0;
    int y = 0;
    bool valid = false;
};

// px,py: click in widget coordinates.
// rx,ry,rw,rh: the rect where the image is painted (letterboxed).
// fw,fh: AF coordinate frame size.
inline Result mapClickToAf(int px, int py, int rx, int ry, int rw, int rh,
                           int fw, int fh) {
    if (rw <= 0 || rh <= 0 || fw <= 0 || fh <= 0) return {};
    if (px < rx || py < ry || px >= rx + rw || py >= ry + rh) return {};

    double nx = double(px - rx) / rw;
    double ny = double(py - ry) / rh;
    int ax = int(nx * fw + 0.5);
    int ay = int(ny * fh + 0.5);
    if (ax < 0) ax = 0;
    if (ax > fw) ax = fw;
    if (ay < 0) ay = 0;
    if (ay > fh) ay = fh;
    return {ax, ay, true};
}

} // namespace afmap
