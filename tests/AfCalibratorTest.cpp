#include "camera/AfCalibrator.h"

#include <cassert>
#include <cmath>
#include <cstdio>

// Given a target click, a guessed frame size, and the true frame size, compute
// the normalized position where the camera would actually focus -- i.e. the
// "observed" click a user would make while judging live-view sharpness.
static void observedFor(double targetX, double targetY, int guessW, int guessH,
                        int trueW, int trueH, double &obsX, double &obsY) {
    obsX = (targetX * guessW) / trueW;
    obsY = (targetY * guessH) / trueH;
}

static void runOne(int guessW, int guessH, int trueW, int trueH,
                   double targetX, double targetY) {
    AfCalibrator c;
    c.begin(guessW, guessH);
    assert(c.stage() == AfCalibrator::Stage::AwaitTarget);

    c.setTarget(targetX, targetY);
    assert(c.stage() == AfCalibrator::Stage::AwaitObserved);

    int afx = 0, afy = 0;
    c.afCommand(afx, afy);
    assert(afx == int(targetX * guessW + 0.5));
    assert(afy == int(targetY * guessH + 0.5));

    double obsX = 0, obsY = 0;
    observedFor(targetX, targetY, guessW, guessH, trueW, trueH, obsX, obsY);

    bool ok = c.setObserved(obsX, obsY);
    assert(ok);
    assert(c.ok());
    assert(c.stage() == AfCalibrator::Stage::Done);
    assert(std::abs(c.resultW() - trueW) <= 1);
    assert(std::abs(c.resultH() - trueH) <= 1);
}

int main() {
    runOne(640, 426, 640, 426, 0.75, 0.25); // already-correct guess
    runOne(1600, 1175, 2000, 1600, 0.85, 0.15); // true frame larger than guess
    runOne(1600, 1175, 2400, 2000, 0.2, 0.8);
    runOne(1600, 1175, 3000, 2200, 0.9, 0.9); // exactly at the sane-bounds edge
    runOne(1600, 1175, 900, 700, 0.3, 0.4);   // true frame smaller than guess
    runOne(1600, 1175, 2000, 1400, 0.5, 0.5); // center target still solvable

    // Degenerate observed click (exactly zero) is rejected.
    {
        AfCalibrator c;
        c.begin(1600, 1175);
        c.setTarget(0.5, 0.5);
        bool ok = c.setObserved(0.0, 0.5);
        assert(!ok);
        assert(!c.ok());
        assert(c.stage() == AfCalibrator::Stage::Done);
    }

    // Nonsensical result (outside sane bounds) is rejected, not clamped.
    {
        AfCalibrator c;
        c.begin(1600, 1175);
        c.setTarget(0.9, 0.9);
        bool ok = c.setObserved(0.05, 0.9); // implies width ~28800, way out of bounds
        assert(!ok);
        assert(!c.ok());
    }

    std::puts("AfCalibratorTest: all assertions passed");
    return 0;
}

