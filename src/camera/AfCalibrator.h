#pragma once

// Pure, Qt-free binary-search calibrator for the AF coordinate frame size.
// One axis at a time; the caller feeds three-way user feedback. See the design
// doc for the monotonic direction argument (too small -> inward; too big ->
// outward; correct -> on-target).
class AfCalibrator {
public:
    enum class Axis { Width, Height };
    enum class Feedback { Inward, OnTarget, Outward };

    void begin(int loW, int hiW, int loH, int hiH) {
        m_loW = loW; m_hiW = hiW; m_loH = loH; m_hiH = hiH;
        m_axis = Axis::Width;
        m_done = false;
        m_iter = 0;
        m_targetX = m_targetY = 0.5;
        m_resultW = (loW + hiW) / 2;
        m_resultH = (loH + hiH) / 2;
    }

    void setTarget(double normX, double normY) {
        m_targetX = normX;
        m_targetY = normY;
    }

    Axis axis() const { return m_axis; }
    bool done() const { return m_done; }

    int currentGuess() const {
        return m_axis == Axis::Width ? (m_loW + m_hiW) / 2 : (m_loH + m_hiH) / 2;
    }

    void afCommand(int otherW, int otherH, int &afX, int &afY) const {
        int g = currentGuess();
        int w = m_axis == Axis::Width ? g : otherW;
        int h = m_axis == Axis::Height ? g : otherH;
        afX = clampRound(m_targetX * w, w);
        afY = clampRound(m_targetY * h, h);
    }

    // Returns true if the active axis converged this step.
    bool applyFeedback(Feedback f) {
        int &lo = (m_axis == Axis::Width) ? m_loW : m_loH;
        int &hi = (m_axis == Axis::Width) ? m_hiW : m_hiH;
        int &result = (m_axis == Axis::Width) ? m_resultW : m_resultH;
        int guess = (lo + hi) / 2;

        if (f == Feedback::OnTarget) {
            result = guess;
            return true;
        }
        if (f == Feedback::Inward) lo = guess;   // too small -> raise floor
        else                       hi = guess;   // Outward: too big -> lower ceiling

        int mid = (lo + hi) / 2;
        if (hi - lo <= kTol || ++m_iter >= kCap) {
            result = mid;
            m_iter = 0;
            return true;
        }
        return false;
    }

    void nextAxis() {
        if (m_axis == Axis::Width) {
            m_axis = Axis::Height;
            m_iter = 0;
        } else {
            m_done = true;
        }
    }

    int resultW() const { return m_resultW; }
    int resultH() const { return m_resultH; }

private:
    static constexpr int kTol = 16;
    static constexpr int kCap = 12;

    static int clampRound(double v, int hi) {
        int r = int(v + 0.5);
        if (r < 0) r = 0;
        if (r > hi) r = hi;
        return r;
    }

    int m_loW = 200, m_hiW = 3000, m_loH = 150, m_hiH = 2200;
    int m_resultW = 1600, m_resultH = 1175;
    Axis m_axis = Axis::Width;
    bool m_done = false;
    int m_iter = 0;
    double m_targetX = 0.5, m_targetY = 0.5;
};
