#pragma once

#include <android/input.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "port/input.h"
#include "port/net.h"

namespace mobile_ui {

struct Settings {
    bool showControls = true;
    bool fullscreen = true;  // the whole display, always (the Wii's 16:9 pillar is not offered)
    bool showCameraCutout = true;
    int languageIndex = 0;
    // The language screen comes up at the next start (the first one, and
    // again whenever the player asks for it in the settings).
    bool askLanguage = true;
    // The game's Wii Remote strap reminder at its start.
    bool showStrapScreen = false;
    // The Wii Remote's rumble, as soft vibration of the phone.
    bool vibration = true;
    // The touch controls go away while a game controller is connected.
    bool hideWithController = true;
    int playerCharacter = 0;
    float opacity = 0.65f;
    float scale = 1.0f;
    bool dynamicStick = false;  // fixed thumbstick by default
    float stickOffsetX = 0.0f;
    float stickOffsetY = 0.0f;
    float buttonOffsetX = 0.0f;
    float buttonOffsetY = 0.0f;
    bool showDpad = true;
    bool dpadOnRight = false;
    float dpadOffsetX = 0.0f;
    float dpadOffsetY = 0.0f;
    bool rightStickPointer = true;

    // Graphics Preset: 0=Low (720p), 1=Medium (900p), 2=High (1080p normal, default), 3=Ultra (Native + Shading/Reflections)
    int graphicsPreset = 2;

    // HD UI & HUD Textures (Super Mario 3D All-Stars high-res assets, ON by default in Plus)
    bool hdUiTextures = false;  // Plus-only feature, no longer built

    // Advanced Mods (all OFF by default, user preferences saved and restored across sessions)
    bool modSkipCutscenes = false;
    bool modInfiniteHealth = false;
    bool modInfinitePowerup = false;
    bool modAdvancedHaptics = false;
    bool modFlightEverywhere = false;

    int settingsVersion = 0;  // 2: fixed thumbstick default applied; 3: whole display

    void load(const std::string& path);
    void save(const std::string& path) const;
};

class UIOverlay {
public:
    static UIOverlay& get();

    bool init();
    void setSavePath(const std::string& path);

    // Processes an Android motion or key event.
    // Returns true if consumed (e.g., settings menu or virtual controls).
    bool handleInput(const AInputEvent* event, PortPadState& pad,
                     int screenW, int screenH, int vpX, int vpY, int vpW, int vpH);

    // Render the overlay (touch controls + settings menu)
    void render(int screenW, int screenH);

    bool needsRedraw();
    bool isSettingsOpen() const { return mSettingsOpen || mDrawerAnim > 0.01f; }
    void closeSettings();
    void openSettings();
    void setPointer(float x, float y);
    Settings& settings() { return mSettings; }
    void setPlusEdition(bool plus) { mPlusEdition = plus; }
    bool isPlusEdition() const { return mPlusEdition; }
    // A game controller is connected (the touch controls then hide, unless
    // the player keeps them).
    void setGamepadConnected(bool connected);
    bool controlsVisible() const { return mSettings.showControls && !(mSettings.hideWithController && mGamepadConnected); }

    // Start-up language screen: shown before the game boots; the game starts
    // once a language is picked (touch, or D-pad / stick + A on a gamepad).
    void startLanguagePicker();
    bool isLanguagePickerActive() const { return mPickerActive; }
    // The languages the installed disc has texts in (bit i: language i of
    // port_set_game_language); only those are offered.  0: disc unknown, all
    // are.  A saved language the disc lacks falls back to the disc's first.
    void setAvailableLanguages(unsigned mask);
    int availableLanguageCount() const { return mLangCount; }
    // Picks the disc's only language without showing the picker.
    void pickOnlyLanguage();
    // Starts with the saved language, without showing the picker.
    void useSavedLanguage();
    // A gamepad key while the picker is up: dx/dy move the selection,
    // confirm picks it.  Returns true when consumed.
    bool pickerKey(int dx, int dy, bool confirm);

private:
    UIOverlay();

    Settings mSettings;
    std::string mSavePath;
    bool mPlusEdition = false;
    bool mInitialized = false;
    bool mDirty = false;
    bool mGamepadConnected = false;

    // Language picker
    bool mPickerActive = false;
    int mPickerSel = 0;  // a slot of mLangList; mLangCount: the strap switch (gamepad)
    int mPickerLastLang = 0;  // the slot the selection left for the switch
    int mLangList[8] = {0, 1, 2, 3, 4, 5, 6, 7};  // the languages offered
    int mLangCount = 7;
    int languageSlot(int language) const;
    // The language under a point of the settings' language slider.
    int sliderLanguageAt(float t) const;
    bool pickerTouch(const AInputEvent* event, int screenW, int screenH);
    void pickLanguage(int index, bool fromPicker = true);
    void setShowStrapScreen(bool show);

    // The settings menu (standard edition): a starry panel of rows, laid out
    // once for drawing and for touches alike.
    struct MenuRow {
        int kind;  // section heading, switch, segmented choice, buttons, note
        int id;
        const char* label;
        const char* options[4];
        int optionCount;
        float y, h;  // within the content, at scroll 0 (pixels)
    };
    void menuRows(std::vector<MenuRow>& rows, float ui) const;
    int menuValue(int id) const;
    void menuAct(int id, int option);
    bool menuTouch(const AInputEvent* event, int screenW, int screenH);
    int mMenuTab = 0;
    int mMenuPressRow = -1;  // the row under the finger
    int mMenuPressOption = 0;
    float mMenuAnim[24] = {};  // switch knobs, by row id

    // UI state
    bool mSettingsOpen = false;
    float mDrawerAnim = 0.0f; // 0.0 = closed offscreen, 1.0 = fully open
    int64_t mLastAnimTime = 0;
    bool mEditLayoutMode = false;
    int mDraggingCluster = 0; // 0 = none, 1 = stick cluster, 2 = button cluster
    float mDragStartX = 0.0f, mDragStartY = 0.0f;
    float mInitialStickOfsX = 0.0f, mInitialStickOfsY = 0.0f;
    float mInitialBtnOfsX = 0.0f, mInitialBtnOfsY = 0.0f;
    float mInitialDpadOfsX = 0.0f, mInitialDpadOfsY = 0.0f;

    // Settings Navigation & Scrolling
    int mSettingsTab = 0;              // 0 = Controls, 1 = Display, 2 = System/Advanced
    float mDrawerScrollY = 0.0f;       // Current scroll offset (pixels)
    float mDrawerScrollTarget = 0.0f;
    bool mDrawerIsScrolling = false;
    int mDrawerScrollPointerId = -1;
    float mDrawerTouchStartY = 0.0f;
    float mDrawerTouchStartX = 0.0f;
    float mDrawerScrollStartOffset = 0.0f;

    // Fluid M3 Tab Indicator Animation
    float mTabIndicatorX = 0.0f;
    float mTabIndicatorW = 0.0f;
    float mTabTargetX = 0.0f;
    float mTabTargetW = 0.0f;

    // Fluid M3 Switch Knob Animations (0.0 = OFF, 1.0 = ON)
    float mSwAnimShowControls = 1.0f;
    float mSwAnimShowDpad = 1.0f;
    float mSwAnimFullscreen = 0.0f;
    float mSwAnimCameraCutout = 1.0f;
    float mSwAnimStrapScreen = 1.0f;
    float mSwAnimAskLanguage = 1.0f;
    bool mDraggingLanguageSlider = false;
    float mSwAnimHdUi = 1.0f;
    float mSwAnimSkipCutscenes = 0.0f;
    float mSwAnimInfiniteHp = 0.0f;
    float mSwAnimInfinitePowerup = 0.0f;
    float mSwAnimHaptics = 0.0f;
    float mSwAnimFlightEverywhere = 0.0f;

    // Netplay State (Local Wi-Fi & Online)
    bool mNetIsHosting = false;
    int mNetSelectedMode = 1; // 1 = Co-op, 2 = Co-Star

    // Warp Menu Sub-View (Plus Edition)
    bool mWarpMenuOpen = false;
    int mWarpCategoryIndex = 0;        // 0=All, 1=Terrace, 2=Fountain, 3=Kitchen, 4=Bedroom, 5=Engine, 6=Garden, 7=Special
    int mWarpSelectedGalaxy = 0;
    int mWarpSelectedScenario = 1;
    float mWarpScrollY = 0.0f;
    float mWarpScrollTarget = 0.0f;
    bool mWarpIsScrolling = false;
    int mWarpScrollPointerId = -1;
    float mWarpTouchStartY = 0.0f;
    float mWarpTouchStartX = 0.0f;
    float mWarpScrollStartOffset = 0.0f;

    // Virtual Stick State
    int mStickPointerId = -1;
    float mStickBaseX = 0.0f;
    float mStickBaseY = 0.0f;
    float mStickKnobX = 0.0f;
    float mStickKnobY = 0.0f;

    // Button press states (for visual press effect)
    bool mBtnAPressed = false;
    bool mBtnBPressed = false;
    bool mBtnZPressed = false;
    bool mBtnSpinPressed = false;
    bool mBtnPlusPressed = false;
    bool mBtnCfgPressed = false;
    bool mBtnDpadLeft = false;
    bool mBtnDpadRight = false;
    bool mBtnDpadUp = false;
    bool mBtnDpadDown = false;
    uint32_t mPrevButtons = 0;
    bool mPrevSpinPressed = false;

    // Persistent in-game Star Pointer state (normalized -1..1, default to screen center 0, 0)
    float mPointerX = 0.0f;
    float mPointerY = 0.0f;

    // GL resources
    unsigned int mProg = 0;
    unsigned int mVao = 0;
    unsigned int mVbo = 0;
    unsigned int mFontTex = 0;
    int mAtlasW = 0;
    int mAtlasH = 0;
};

inline UIOverlay& get() {
    return UIOverlay::get();
}

}  // namespace mobile_ui
