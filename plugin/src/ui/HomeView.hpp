// NA_Magicos: the Home screen (the channel's interface design): the logo and the navigation, the instruments,
// the stage, the audio controls, the smart features and the play bar. Drawing and hit testing only; PianoUI
// owns the state and turns the actions into parameter changes, instrument choices and views.
#pragma once
#include "Layout.hpp"
#include "NanoVG.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pe {

namespace home {
enum Action : int {
    None = -1,
    NavHome, NavPiano, NavSounds, NavEffects, NavHall,
    Volume, Settings, Bigger,
    SeeAll, InstPrev, InstNext, Group0, Group1, Group2, Group3,
    Knob0, Knob1, Knob2, Knob3, // Reverb, Delay, Chorus, EQ (tilt)
    SmartAuto, SmartChords, SmartOneTouch,
    ModeKeyboard, ModeChords, ModeMelody,
    TransDown, TransUp, KeyPrev, KeyNext,
    Count
};
}

struct HomeModel {
    float w = 900, h = 600;                  // the controls' area (window-logical)
    std::string instrument, group;           // the instrument playing and its menu group
    std::vector<std::string> groups;         // the four category cards (menu group names), "" for none
    int groupIndex = -1;                     // which card is the instrument's group
    float knob[4] {};                        // 0 … 1: reverb, delay, chorus, EQ tilt (0.5 flat)
    std::string knobText[4];
    int mode = 0, transpose = 0, key = 0;    // the play bar
    float volumeNorm = 0.5f; std::string volumeText;
    int hot = home::None;                    // under the pointer
    bool big = false;                        // the window made large (the Bigger button lit)
};

class HomeView {
public:
    void draw(DGL_NAMESPACE::NanoVG& vg, const HomeModel& m);
    /// the action at a window-logical point (after a draw), home::None for none
    int at(float x, float y) const;
    const Rect& rect(int action) const { return fRect[action]; }
    const Rect& volumeTrack() const { return fVolTrack; }

private:
    void layout(const HomeModel& m);
    Rect fRect[home::Count] {};
    float u = 1; // the scale: 1 at 1100 x 620
    Rect fTop, fNav, fLeft, fRight, fAudio, fSmart, fBar, fStage, fFeature, fLogo;
    std::unique_ptr<DGL_NAMESPACE::NanoImage> fLogoImg, fStageImg, fGrandImg;
    bool fImgTried = false;
    Rect fVolTrack {};
};

} // namespace pe
