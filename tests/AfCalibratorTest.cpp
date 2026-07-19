#include "camera/AfCalibrator.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

// Simulate the user's feedback for a known true frame size on one axis.
static AfCalibrator::Feedback judge(int guess, int truth) {
    if (guess < truth - 4) return AfCalibrator::Feedback::Inward;
    if (guess > truth + 4) return AfCalibrator::Feedback::Outward;
    return AfCalibrator::Feedback::OnTarget;
}

static void runOne(int trueW, int trueH) {
    AfCalibrator c;
    c.begin(200, 3000, 150, 2200);

    // Width axis.
    c.setTarget(0.9, 0.5);
    int rounds = 0;
    while (true) {
        assert(++rounds <= 12);
        bool converged = c.applyFeedback(judge(c.currentGuess(), trueW));
        if (converged) break;
    }
    c.nextAxis();
    assert(!c.done());

    // Height axis.
    c.setTarget(0.5, 0.9);
    rounds = 0;
    while (true) {
        assert(++rounds <= 12);
        bool converged = c.applyFeedback(judge(c.currentGuess(), trueH));
        if (converged) break;
    }
    c.nextAxis();
    assert(c.done());

    assert(std::abs(c.resultW() - trueW) <= 16);
    assert(std::abs(c.resultH() - trueH) <= 16);
}

int main() {
    runOne(500, 400);
    runOne(900, 700);
    runOne(1500, 1100);
    runOne(640, 426);

    // afCommand maps target * guess for the active axis, target * other for the
    // passive axis.
    {
        AfCalibrator c;
        c.begin(200, 3000, 150, 2200);
        c.setTarget(0.5, 0.5);
        int gx = c.currentGuess(); // width guess = mid(200,3000)=1600
        int afx = 0, afy = 0;
        c.afCommand(1000, 800, afx, afy);
        assert(afx == (int)(0.5 * gx + 0.5));
        assert(afy == (int)(0.5 * 800 + 0.5));
    }

    std::puts("AfCalibratorTest: all assertions passed");
    return 0;
}
