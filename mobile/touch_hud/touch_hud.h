// touch_hud.h - mobile touch HUD skeleton (header-only, C++17).
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

// Adapters into the existing game code. Any hook may be left null.
struct Hooks {
    void (*MoveDir)(float dx, float dy) = nullptr;     // each in [-1,1]; dy < 0 = up on screen; (0,0) = stop
    void (*BasicAttack)() = nullptr;                   // auto-target nearest enemy and attack
    void (*UseSkill)(int skillId) = nullptr;           // cast a skill (server validates)
    void (*OpenInventory)() = nullptr;                 // open the bag window
    void (*SaveSlot)(int slot, int skillId) = nullptr; // persist quick-slot layout
};

enum class Touch { Down, Move, Up };

class TouchHUD {
public:
    static constexpr int kSlots = 5;

    // screenW/H in pixels, dp = pixels per density-independent pixel.
    void Init(float screenW, float screenH, float dp, const Hooks& hooks) {
        w_ = screenW; h_ = screenH; dp_ = dp; hooks_ = hooks;
        for (int& s : slots_) s = 0;
        LayoutButtons();
    }

    // Feed every pointer event from the Android activity. Returns true if the
    // HUD consumed it; otherwise pass it on to the existing UI / camera.
    bool OnTouch(Touch t, int id, Vec2 p) {
        switch (t) {
            case Touch::Down: return Down(id, p);
            case Touch::Move: return Move(id, p);
            case Touch::Up:   return Up(id);
        }
        return false;
    }

    // Call once per frame: holding the attack button repeats basic attacks.
    void Update(float dtSec) {
        if (atkId_ == -1) return;
        atkTimer_ += dtSec;
        if (atkTimer_ >= 0.4f) {
            atkTimer_ = 0.f;
            if (hooks_.BasicAttack) hooks_.BasicAttack();
        }
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

    // Read-only state for the renderer.
    Vec2  AttackCenter() const { return atk_; }
    float AttackRadius() const { return atkR_; }
    Vec2  SlotCenter(int i) const { return slotPos_[i]; }
    float SlotRadius() const { return slotR_; }
    int   SlotSkill(int i) const { return slots_[i]; }
    Vec2  BagCenter() const { return bag_; }
    float BagRadius() const { return bagR_; }
    bool  JoystickActive() const { return joyId_ != -1; }
    Vec2  JoystickOrigin() const { return joyOrigin_; }
    Vec2  JoystickKnob() const { return joyKnob_; }

private:
    void LayoutButtons() {
        const float kPi = 3.14159265f;
        const float margin = 24.f * dp_;
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

        bagR_ = 24.f * dp_;
        bag_ = { w_ - margin - bagR_, margin + 64.f * dp_ };         // under the minimap
    }

    int SlotAt(Vec2 p) const {
        for (int i = 0; i < kSlots; ++i)
            if (Dist(p, slotPos_[i]) <= slotR_) return i;
        return -1;
    }

    bool Down(int id, Vec2 p) {
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
        if (Dist(p, bag_) <= bagR_) {
            if (hooks_.OpenInventory) hooks_.OpenInventory();
            return true;
        }
        if (p.x < w_ * 0.4f && joyId_ == -1) {          // floating joystick, left side
            joyId_ = id; joyOrigin_ = p; joyKnob_ = p;
            return true;
        }
        return false;
    }

    bool Move(int id, Vec2 p) {
        if (id != joyId_) return id == atkId_;
        const float maxR = 60.f * dp_;
        Vec2 d { p.x - joyOrigin_.x, p.y - joyOrigin_.y };
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > maxR) { d.x *= maxR / len; d.y *= maxR / len; len = maxR; }
        joyKnob_ = { joyOrigin_.x + d.x, joyOrigin_.y + d.y };
        if (hooks_.MoveDir) {
            if (len / maxR < 0.15f) hooks_.MoveDir(0.f, 0.f);   // dead zone
            else hooks_.MoveDir(d.x / maxR, d.y / maxR);
        }
        return true;
    }

    bool Up(int id) {
        if (id == joyId_) {
            joyId_ = -1;
            if (hooks_.MoveDir) hooks_.MoveDir(0.f, 0.f);
            return true;
        }
        if (id == atkId_) { atkId_ = -1; return true; }
        return false;
    }

    float w_ = 0, h_ = 0, dp_ = 1;
    Hooks hooks_;
    Vec2 atk_, bag_, slotPos_[kSlots], joyOrigin_, joyKnob_;
    float atkR_ = 0, slotR_ = 0, bagR_ = 0, atkTimer_ = 0;
    int slots_[kSlots] = {};
    int joyId_ = -1, atkId_ = -1;
};

}  // namespace mobile
