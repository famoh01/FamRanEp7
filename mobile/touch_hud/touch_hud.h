// touch_hud.h - mobile touch HUD (header-only, C++17).
// Shared by the Android app (mobile/android) and the desktop tests.
// Connect the Hooks to the existing RAN EP7 game code; do not guess their
// signatures - read the original classes first and write thin adapters.
#pragma once
#include <algorithm>
#include <cmath>

namespace mobile {

struct Vec2 { float x = 0.f, y = 0.f; };

inline float Dist(Vec2 a, Vec2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    bool Contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
};

// Full-screen panels that open from the HUD buttons. None = no panel open.
enum class Window { None, Menu, Character, Club, Skill, Bag };

// Small round buttons (the attack button and joystick are handled separately).
enum class Button { Menu, Character, Club, Bag, Skill, Run, Potion, AutoPotion, PotionPercent };

// Adapters into the existing game code. Any hook may be left null.
struct Hooks {
    void (*MoveDir)(float dx, float dy) = nullptr;     // each in [-1,1]; dy < 0 = up on screen; (0,0) = stop
    void (*BasicAttack)() = nullptr;                   // auto-target nearest enemy and attack
    void (*UseSkill)(int skillId) = nullptr;           // cast a skill (server validates)
    void (*SaveSlot)(int slot, int skillId) = nullptr; // persist quick-slot layout
    bool (*UsePotion)() = nullptr;                     // drink one potion (manual or auto); false = none left
    void (*UseItem)(int itemId) = nullptr;             // a bag item was tapped
    void (*OnWindow)(Window w) = nullptr;              // a panel opened (w != None) or closed (None)
    void (*OnRunChanged)(bool running) = nullptr;      // run/walk toggle changed
};

enum class Touch { Down, Move, Up };

class TouchHUD {
public:
    static constexpr int kSlots = 5;
    static constexpr int kSkillCells = 6;               // skill window grid (3 x 2)
    static constexpr int kBagCells = 24;                // bag window grid (6 x 4)
    static constexpr int kButtonCount = 9;
    static constexpr int kPotionMin = 10, kPotionMax = 90, kPotionStep = 10, kPotionDefault = 50;
    static constexpr float kPotionCooldown = 3.f;      // seconds between potions (manual or auto)

    // screenW/H in pixels, dp = pixels per density-independent pixel.
    void Init(float screenW, float screenH, float dp, const Hooks& hooks) {
        hooks_ = hooks;
        for (int& s : slots_) s = 0;
        running_ = false;
        autoPotion_ = false;
        percent_ = kPotionDefault;
        window_ = Window::None;
        hp_ = 0.f;
        hpMax_ = 0.f;
        potionCd_ = 0.f;
        joyId_ = -1;
        atkId_ = -1;
        atkTimer_ = 0.f;
        Resize(screenW, screenH, dp);
    }

    // Recompute positions only; keeps state (slots, toggles, open window, HP).
    void Resize(float screenW, float screenH, float dp) {
        w_ = screenW; h_ = screenH; dp_ = dp;
        LayoutButtons();
    }

    // Feed every pointer event from the Android activity. Returns true if the
    // HUD consumed it; otherwise pass it on to the existing UI / camera.
    bool OnTouch(Touch t, int id, Vec2 p) {
        switch (t) {
            case Touch::Down: return Down(id, p);
            case Touch::Move: return Move(id, p);
            case Touch::Up:   return Up(id, p);
        }
        return false;
    }

    // Call once per frame: attack repeat, cooldown, and auto potion.
    void Update(float dtSec) {
        if (potionCd_ > 0.f) potionCd_ = std::max(0.f, potionCd_ - dtSec);
        if (autoPotion_ && hpMax_ > 0.f && potionCd_ <= 0.f && HpRatio() * 100.f <= float(percent_)) {
            UsePotionNow();
        }
        if (window_ != Window::None || atkId_ == -1) return;   // panels block attacks
        atkTimer_ += dtSec;
        if (atkTimer_ >= 0.4f) {
            atkTimer_ = 0.f;
            if (hooks_.BasicAttack) hooks_.BasicAttack();
        }
    }

    // The game reports its current HP every frame; auto potion reads it.
    void SetHp(float cur, float max) {
        hpMax_ = std::max(0.f, max);
        hp_ = std::min(std::max(0.f, cur), hpMax_);
    }

    // The skill window calls this when the player drops an icon dragged out of
    // it. Returns true if the drop landed on one of the HUD skill slots.
    bool DropSkill(int skillId, Vec2 p) {
        int s = SlotAt(p);
        if (s < 0) return false;
        slots_[s] = skillId;
        if (hooks_.SaveSlot) hooks_.SaveSlot(s, skillId);
        return true;
    }

    void SetSlot(int slot, int skillId) {          // restore a saved layout
        if (slot >= 0 && slot < kSlots) slots_[slot] = skillId;
    }

    void CloseWindow() { SetWindow(Window::None); }  // e.g. Android back button

    // Skills shown in the skill window, in grid order. 0 = empty cell.
    void SetSkills(const int* ids, int count) {
        for (int i = 0; i < kSkillCells; ++i) skills_[i] = (i < count) ? ids[i] : 0;
    }

    // Bag contents in grid order. id 0 = empty cell.
    void SetBagItems(const int* ids, const int* counts, int count) {
        for (int i = 0; i < kBagCells; ++i) {
            bagIds_[i] = (i < count) ? ids[i] : 0;
            bagCounts_[i] = (i < count) ? counts[i] : 0;
        }
    }

    // Read-only state for the renderer.
    Vec2  AttackCenter() const { return atk_; }
    float AttackRadius() const { return atkR_; }
    Vec2  SlotCenter(int i) const { return slotPos_[i]; }
    float SlotRadius() const { return slotR_; }
    int   SlotSkill(int i) const { return slots_[i]; }
    Vec2  ButtonCenter(Button b) const { return btn_[static_cast<int>(b)]; }
    float ButtonRadius() const { return btnR_; }
    bool  ButtonOn(Button b) const {
        if (b == Button::Run) return running_;
        if (b == Button::AutoPotion) return autoPotion_;
        return false;
    }
    bool  Running() const { return running_; }
    bool  AutoPotion() const { return autoPotion_; }
    int   PotionPercent() const { return percent_; }
    float HpRatio() const { return hpMax_ > 0.f ? hp_ / hpMax_ : 1.f; }
    float PotionCooldownLeft() const { return potionCd_; }
    Window OpenWindow() const { return window_; }
    Rect  PanelRect() const { return panel_; }
    int   SkillAt(int i) const { return skills_[i]; }
    int   BagItemAt(int i) const { return bagIds_[i]; }
    int   BagCountAt(int i) const { return bagCounts_[i]; }
    Vec2  BagCellCenter(int i) const { return bagPos_[i]; }
    float BagCellRadius() const { return bagR_; }
    Vec2  SkillCellCenter(int i) const { return skillPos_[i]; }
    float SkillCellRadius() const { return skillR_; }
    bool  Dragging() const { return dragPtr_ != -1; }
    int   DragSkill() const { return dragSkill_; }
    Vec2  DragPos() const { return dragPos_; }
    Vec2  PanelCloseCenter() const { return closeC_; }
    float PanelCloseRadius() const { return closeR_; }
    bool  JoystickActive() const { return joyId_ != -1; }
    Vec2  JoystickOrigin() const { return joyBase_; }   // fixed base, bottom-left
    Vec2  JoystickKnob() const { return joyKnob_; }
    float JoystickRadius() const { return joyR_; }      // base radius = max knob travel

private:
    static Window WindowFor(Button b) {
        switch (b) {
            case Button::Menu:      return Window::Menu;
            case Button::Character: return Window::Character;
            case Button::Club:      return Window::Club;
            case Button::Skill:     return Window::Skill;
            case Button::Bag:       return Window::Bag;
            default:                return Window::None;
        }
    }

    void LayoutButtons() {
        const float kPi = 3.14159265f;
        // 40 dp from the edges, so buttons stay clear of the system gesture zones on phones.
        const float margin = 40.f * dp_;
        atkR_ = 44.f * dp_;
        atk_ = { w_ - margin - atkR_, h_ - margin - atkR_ };

        // Skill slots on a quarter-circle arc around the attack button
        // (left of it -> above it). Radius is derived so neighbours never overlap.
        slotR_ = 27.f * dp_;
        const float stepDeg = 90.f / float(kSlots - 1);
        const float halfStep = stepDeg * 0.5f * kPi / 180.f;
        const float arcR = std::max(atkR_ + slotR_ + 8.f * dp_,
                                    (slotR_ + 3.f * dp_) / std::sin(halfStep));
        for (int i = 0; i < kSlots; ++i) {
            float a = (180.f + stepDeg * float(i)) * kPi / 180.f;   // screen y points down
            slotPos_[i] = { atk_.x + arcR * std::cos(a), atk_.y + arcR * std::sin(a) };
        }

        // Small buttons on a 58dp grid.
        btnR_ = 24.f * dp_;
        const float gap = 58.f * dp_;
        auto at = [this](Button b, Vec2 p) { btn_[static_cast<int>(b)] = p; };
        // Top-left row: menu, character info, club.
        at(Button::Menu,      { margin + btnR_,           margin + btnR_ });
        at(Button::Character, { margin + btnR_ + gap,     margin + btnR_ });
        at(Button::Club,      { margin + btnR_ + 2 * gap, margin + btnR_ });
        // Top-right row: bag, skill window beside it. Kept high so they do not
        // collide with the top skill slot on the arc.
        at(Button::Bag,       { w_ - margin - btnR_,       margin + btnR_ });
        at(Button::Skill,     { w_ - margin - btnR_ - gap, margin + btnR_ });
        // Bottom-centre row: potion, auto-potion toggle, percent setting.
        const float rowY = h_ - margin - btnR_;
        at(Button::Potion,        { w_ * 0.5f - gap, rowY });
        at(Button::AutoPotion,    { w_ * 0.5f,       rowY });
        at(Button::PotionPercent, { w_ * 0.5f + gap, rowY });

        // Fixed joystick base in the bottom-left corner (always visible).
        joyR_ = 60.f * dp_;
        joyBase_ = { margin + joyR_, h_ - margin - joyR_ };
        joyKnob_ = joyBase_;
        // Run/walk toggle sits just above the joystick base.
        at(Button::Run, { joyBase_.x, joyBase_.y - joyR_ - btnR_ - 8.f * dp_ });

        // Full-screen panel in the middle, close button in its top-right corner.
        panel_ = { w_ * 0.12f, h_ * 0.10f, w_ * 0.76f, h_ * 0.80f };
        closeR_ = 22.f * dp_;
        closeC_ = { panel_.x + panel_.w - 34.f * dp_, panel_.y + 34.f * dp_ };
        // Skill grid inside the panel: 3 columns, 2 rows, below the title.
        skillR_ = 34.f * dp_;
        for (int i = 0; i < kSkillCells; ++i) {
            const int col = i % 3, row = i / 3;
            skillPos_[i] = { panel_.x + panel_.w * (float(col) + 0.5f) / 3.f,
                             panel_.y + 120.f * dp_ + float(row) * 90.f * dp_ };
        }
        // Bag grid inside the panel: 6 columns, 4 rows, below the title.
        bagR_ = 19.f * dp_;
        for (int i = 0; i < kBagCells; ++i) {
            const int col = i % 6, row = i / 6;
            bagPos_[i] = { panel_.x + panel_.w * (float(col) + 0.5f) / 6.f,
                           panel_.y + 130.f * dp_ + float(row) * 44.f * dp_ };
        }
    }

    int SlotAt(Vec2 p) const {
        for (int i = 0; i < kSlots; ++i)
            if (Dist(p, slotPos_[i]) <= slotR_) return i;
        return -1;
    }

    void SetWindow(Window w) {
        if (w == window_) return;
        window_ = w;
        dragPtr_ = -1;                     // a drag does not survive a panel change
        dragSkill_ = 0;
        atkId_ = -1;                       // stop attack repeat while a panel is open
        if (joyId_ != -1) {                // release the stick
            joyId_ = -1;
            joyKnob_ = joyBase_;
            if (hooks_.MoveDir) hooks_.MoveDir(0.f, 0.f);
        }
        if (hooks_.OnWindow) hooks_.OnWindow(w);
    }

    bool UsePotionNow() {
        const bool used = hooks_.UsePotion ? hooks_.UsePotion() : false;
        potionCd_ = kPotionCooldown;   // also after a failed try, so auto-potion does not retry every frame
        return used;
    }

    int BagCellAt(Vec2 p) const {
        for (int i = 0; i < kBagCells; ++i)
            if (Dist(p, bagPos_[i]) <= bagR_) return i;
        return -1;
    }

    bool Press(Button b) {
        const Window w = WindowFor(b);
        if (w != Window::None) { SetWindow(w); return true; }
        switch (b) {
            case Button::Run:
                running_ = !running_;
                if (hooks_.OnRunChanged) hooks_.OnRunChanged(running_);
                break;
            case Button::Potion:
                if (potionCd_ <= 0.f) UsePotionNow();
                break;
            case Button::AutoPotion:
                autoPotion_ = !autoPotion_;
                break;
            case Button::PotionPercent:
                percent_ = (percent_ >= kPotionMax) ? kPotionMin : percent_ + kPotionStep;
                break;
            default:
                break;
        }
        return true;
    }

    bool Down(int id, Vec2 p) {
        if (window_ != Window::None) {     // modal: panel swallows every touch
            if (Dist(p, closeC_) <= closeR_) { SetWindow(Window::None); return true; }
            if (window_ == Window::Bag) {
                const int c = BagCellAt(p);
                if (c >= 0 && bagIds_[c] > 0 && hooks_.UseItem) hooks_.UseItem(bagIds_[c]);
            }
            if (window_ == Window::Skill && dragPtr_ == -1) {
                for (int i = 0; i < kSkillCells; ++i) {
                    if (skills_[i] > 0 && Dist(p, skillPos_[i]) <= skillR_) {
                        dragPtr_ = id; dragSkill_ = skills_[i]; dragPos_ = p;
                        break;
                    }
                }
            }
            return true;
        }
        if (Dist(p, atk_) <= atkR_) {
            atkId_ = id; atkTimer_ = 0.f;
            if (hooks_.BasicAttack) hooks_.BasicAttack();
            return true;
        }
        int s = SlotAt(p);
        if (s >= 0) {
            if (slots_[s] > 0 && hooks_.UseSkill) hooks_.UseSkill(slots_[s]);
            return true;
        }
        for (int i = 0; i < kButtonCount; ++i) {
            if (Dist(p, btn_[i]) <= btnR_) return Press(static_cast<Button>(i));
        }
        if (joyId_ == -1 && Dist(p, joyBase_) <= joyR_ * 1.5f) {   // touch near the base starts the stick
            joyId_ = id;
            joyKnob_ = joyBase_;
            return true;
        }
        return false;
    }

    bool Move(int id, Vec2 p) {
        if (id == dragPtr_) { dragPos_ = p; return true; }
        if (window_ != Window::None) return true;
        if (id != joyId_) return id == atkId_;
        const float maxR = joyR_;
        Vec2 d { p.x - joyBase_.x, p.y - joyBase_.y };
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > maxR) { d.x *= maxR / len; d.y *= maxR / len; len = maxR; }
        joyKnob_ = { joyBase_.x + d.x, joyBase_.y + d.y };
        if (hooks_.MoveDir) {
            if (len / maxR < 0.15f) hooks_.MoveDir(0.f, 0.f);   // dead zone
            else hooks_.MoveDir(d.x / maxR, d.y / maxR);
        }
        return true;
    }

    bool Up(int id, Vec2 p) {
        if (id == dragPtr_) {              // drop the dragged skill where the finger lifted
            DropSkill(dragSkill_, p);
            dragPtr_ = -1;
            dragSkill_ = 0;
            return true;
        }
        if (id == joyId_) {
            joyId_ = -1;
            joyKnob_ = joyBase_;
            if (hooks_.MoveDir) hooks_.MoveDir(0.f, 0.f);
            return true;
        }
        if (id == atkId_) { atkId_ = -1; return true; }
        return false;
    }

    float w_ = 0, h_ = 0, dp_ = 1;
    Hooks hooks_;
    Vec2 atk_, slotPos_[kSlots], joyBase_, joyKnob_;
    Vec2 btn_[kButtonCount];
    Rect panel_;
    Vec2 closeC_;
    int skills_[kSkillCells] = {};
    int bagIds_[kBagCells] = {};
    int bagCounts_[kBagCells] = {};
    Vec2 bagPos_[kBagCells];
    float bagR_ = 0;
    Vec2 skillPos_[kSkillCells];
    float skillR_ = 0;
    int dragPtr_ = -1, dragSkill_ = 0;
    Vec2 dragPos_;
    float atkR_ = 0, slotR_ = 0, btnR_ = 0, joyR_ = 0, closeR_ = 0, atkTimer_ = 0;
    float potionCd_ = 0, hp_ = 0, hpMax_ = 0;
    int slots_[kSlots] = {};
    int percent_ = kPotionDefault;
    int joyId_ = -1, atkId_ = -1;
    bool running_ = false, autoPotion_ = false;
    Window window_ = Window::None;
};

}  // namespace mobile
