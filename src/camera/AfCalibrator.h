#pragma once

// Pure, Qt-free direct calibrator for the AF coordinate frame size.
//
// Two clicks and no iteration: the point the user wants focused (target),
// and the point in the live view where focus actually landed (observed,
// judged by sharpness). The AF command sent to the camera is
// afX = targetNormX * guessW (see AfMapping.h), and the camera focuses at
// fraction afX/trueW of its real frame. So the observed click directly
// reveals the true frame size in one shot:
//
//   trueW = guessW * targetNormX / observedNormX
//   trueH = guessH * targetNormY / observedNormY
//
// Both axes are solved from the single target/observed pair -- a target
// off-center in both x and y (e.g. near a corner) gives the best signal on
// both axes at once.
class AfCalibrator {
public:
    enum class Stage { AwaitTarget, AwaitObserved, Done };

    void begin(int guessW, int guessH) {
        m_guessW = guessW;
        m_guessH = guessH;
        m_stage = Stage::AwaitTarget;
        m_ok = false;
    }

    Stage stage() const { return m_stage; }

    // First click: the point the user wants focused (normalized 0..1).
    void setTarget(double normX, double normY) {
        m_targetX = normX;
        m_targetY = normY;
        m_stage = Stage::AwaitObserved;
    }

    // AF command to fire at the target, using the current guess frame size.
    void afCommand(int &afX, int &afY) const {
        afX = clampRound(m_targetX * m_guessW, m_guessW);
        afY = clampRound(m_targetY * m_guessH, m_guessH);
    }

    // Second click: where focus actually landed (normalized 0..1). Returns
    // false (stage() becomes Done, ok() stays false) if the derived frame
    // size is degenerate or out of sane bounds -- caller should ask the user
    // to redo both clicks rather than trust a wild result.
    bool setObserved(double normX, double normY) {
        m_stage = Stage::Done;
        m_ok = false;
        if (normX <= kEps || normY <= kEps) return false;

        double w = m_guessW * (m_targetX / normX);
        double h = m_guessH * (m_targetY / normY);
        if (w < kMinW || w > kMaxW || h < kMinH || h > kMaxH) return false;

        m_resultW = int(w + 0.5);
        m_resultH = int(h + 0.5);
        m_ok = true;
        return true;
    }

    bool ok() const { return m_ok; }
    int resultW() const { return m_resultW; }
    int resultH() const { return m_resultH; }

private:
    static constexpr double kEps = 0.01;
    static constexpr int kMinW = 200, kMaxW = 3000;
    static constexpr int kMinH = 150, kMaxH = 2200;

    static int clampRound(double v, int hi) {
        int r = int(v + 0.5);
        if (r < 0) r = 0;
        if (r > hi) r = hi;
        return r;
    }

    int m_guessW = 1600, m_guessH = 1175;
    double m_targetX = 0.5, m_targetY = 0.5;
    Stage m_stage = Stage::AwaitTarget;
    bool m_ok = false;
    int m_resultW = 0, m_resultH = 0;
};
