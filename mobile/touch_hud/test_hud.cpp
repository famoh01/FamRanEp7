#include "touch_hud.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace mobile;

static float g_dx = 9, g_dy = 9;
static int g_attacks = 0, g_skill = 0, g_potions = 0, g_run = -1;
static int g_saved_slot = -1, g_saved_skill = -1;
static Window g_window = Window::None;
static int g_window_events = 0;

static bool Pressed(TouchHUD& hud, int id, Button b) {
    Vec2 c = hud.ButtonCenter(b);
    bool consumed = hud.OnTouch(Touch::Down, id, c);
    hud.OnTouch(Touch::Up, id, c);
    return consumed;
}

int main() {
    Hooks h;
    h.MoveDir = [](float dx, float dy) { g_dx = dx; g_dy = dy; };
    h.BasicAttack = [] { ++g_attacks; };
    h.UseSkill = [](int id) { g_skill = id; };
    h.SaveSlot = [](int s, int id) { g_saved_slot = s; g_saved_skill = id; };
    h.UsePotion = [] { ++g_potions; };
    h.OnWindow = [](Window w) { g_window = w; ++g_window_events; };
    h.OnRunChanged = [](bool r) { g_run = r ? 1 : 0; };

    TouchHUD hud;
    hud.Init(2400.f, 1080.f, 3.0f, h);   // typical 20:9 phone, 3x density

    // ---- layout ----
    // No slot pair overlaps; every slot is on screen and clear of the attack button.
    for (int i = 0; i < TouchHUD::kSlots; ++i)
        for (int j = i + 1; j < TouchHUD::kSlots; ++j)
            assert(Dist(hud.SlotCenter(i), hud.SlotCenter(j)) >= 2.f * hud.SlotRadius());
    for (int i = 0; i < TouchHUD::kSlots; ++i) {
        Vec2 c = hud.SlotCenter(i);
        assert(c.x - hud.SlotRadius() >= 0 && c.y - hud.SlotRadius() >= 0);
        assert(Dist(c, hud.AttackCenter()) >= hud.AttackRadius() + hud.SlotRadius());
    }
    // Every small button is on screen, no two overlap, none sits on the attack button or a slot.
    const float br = hud.ButtonRadius();
    for (int i = 0; i < TouchHUD::kButtonCount; ++i) {
        Button bi = static_cast<Button>(i);
        Vec2 c = hud.ButtonCenter(bi);
        assert(c.x - br >= 0 && c.y - br >= 0 && c.x + br <= 2400.f && c.y + br <= 1080.f);
        assert(Dist(c, hud.AttackCenter()) >= hud.AttackRadius() + br);
        for (int s = 0; s < TouchHUD::kSlots; ++s)
            assert(Dist(c, hud.SlotCenter(s)) >= hud.SlotRadius() + br);
        for (int j = i + 1; j < TouchHUD::kButtonCount; ++j)
            assert(Dist(c, hud.ButtonCenter(static_cast<Button>(j))) >= 2.f * br);
    }
    // The panel's close button sits inside the panel.
    assert(hud.PanelRect().Contains(hud.PanelCloseCenter()));

    // ---- joystick (window closed) ----
    Vec2 jb = hud.JoystickOrigin();
    assert(jb.x < 2400.f * 0.3f && jb.y > 1080.f * 0.6f);
    assert(hud.JoystickRadius() == 180.f);             // 60 dp at 3x
    assert(!hud.OnTouch(Touch::Down, 9, {1200, 500})); // far from the base: not consumed
    assert(!hud.JoystickActive());
    assert(hud.OnTouch(Touch::Down, 1, jb));
    assert(hud.JoystickActive());
    hud.OnTouch(Touch::Move, 1, {jb.x + 90, jb.y});    // half of max radius
    assert(std::fabs(g_dx - 0.5f) < 0.01f && std::fabs(g_dy) < 0.01f);
    hud.OnTouch(Touch::Move, 1, {jb.x + 400, jb.y});   // past max radius: clamped to 1
    assert(std::fabs(g_dx - 1.0f) < 0.01f && std::fabs(g_dy) < 0.01f);
    hud.OnTouch(Touch::Move, 1, {jb.x + 10, jb.y});    // dead zone: stop
    assert(g_dx == 0.f && g_dy == 0.f);
    hud.OnTouch(Touch::Up, 1, {0, 0});
    assert(!hud.JoystickActive() && g_dx == 0.f && g_dy == 0.f);

    // ---- attack button + hold repeat ----
    Vec2 a = hud.AttackCenter();
    assert(hud.OnTouch(Touch::Down, 2, a));
    assert(g_attacks == 1);
    hud.Update(0.5f);
    assert(g_attacks == 2);
    hud.OnTouch(Touch::Up, 2, a);
    hud.Update(0.5f);
    assert(g_attacks == 2);

    // ---- skills: drop from the skill window, then tap the slot ----
    assert(hud.DropSkill(777, hud.SlotCenter(2)));
    assert(g_saved_slot == 2 && g_saved_skill == 777);
    assert(!hud.DropSkill(777, {10, 10}));
    hud.OnTouch(Touch::Down, 3, hud.SlotCenter(2));
    assert(g_skill == 777);
    hud.OnTouch(Touch::Up, 3, hud.SlotCenter(2));

    // ---- run / walk toggle ----
    assert(!hud.Running() && g_run == -1);
    assert(Pressed(hud, 4, Button::Run));
    assert(hud.Running() && g_run == 1 && hud.ButtonOn(Button::Run));
    assert(Pressed(hud, 4, Button::Run));
    assert(!hud.Running() && g_run == 0);

    // ---- potions: auto at a set HP percentage, manual with cooldown ----
    hud.SetHp(100, 100);
    assert(!hud.AutoPotion() && hud.PotionPercent() == 50);
    hud.Update(1.f);
    assert(g_potions == 0);                                  // auto is off
    assert(Pressed(hud, 5, Button::AutoPotion));             // turn auto on
    assert(hud.AutoPotion() && hud.ButtonOn(Button::AutoPotion));
    hud.SetHp(60, 100);
    hud.Update(0.1f);
    assert(g_potions == 0);                                  // 60% > 50%: no potion
    hud.SetHp(50, 100);
    hud.Update(0.1f);
    assert(g_potions == 1);                                  // 50% <= 50%: drink
    assert(hud.PotionCooldownLeft() > 2.f);
    hud.SetHp(40, 100);
    hud.Update(1.f);
    assert(g_potions == 1);                                  // still cooling down
    hud.Update(2.5f);
    assert(g_potions == 2);                                  // cooldown over, still low
    assert(Pressed(hud, 6, Button::Potion) && g_potions == 2);   // manual tap ignored on cooldown
    hud.SetHp(100, 100);
    hud.Update(3.f);                                          // cooldown over, HP full: no auto
    assert(Pressed(hud, 6, Button::Potion) && g_potions == 3);  // manual drink after cooldown

    // ---- potion percent setting: 10..90, tap cycles ----
    assert(Pressed(hud, 7, Button::PotionPercent) && hud.PotionPercent() == 60);
    for (int i = 0; i < 4; ++i) Pressed(hud, 7, Button::PotionPercent);
    assert(hud.PotionPercent() == 10);                       // 90 wraps to 10
    hud.SetHp(15, 100);                                      // 15% > 10%: no auto potion yet
    hud.Update(3.5f);
    assert(g_potions == 3);
    hud.SetHp(10, 100);
    hud.Update(0.1f);
    assert(g_potions == 4);
    hud.SetHp(100, 100);
    hud.Update(3.f);
    assert(Pressed(hud, 7, Button::PotionPercent) && hud.PotionPercent() == 20);   // restore a sane value

    // ---- windows: each button opens one, modal until closed ----
    int events_before = g_window_events;
    assert(Pressed(hud, 20, Button::Bag));
    assert(hud.OpenWindow() == Window::Bag && g_window == Window::Bag);
    int attacks = g_attacks;
    assert(hud.OnTouch(Touch::Down, 21, a));                  // attack button swallowed
    assert(g_attacks == attacks);
    assert(hud.OnTouch(Touch::Down, 22, hud.SlotCenter(0)));  // skill slot swallowed
    assert(g_skill == 777);
    assert(hud.OnTouch(Touch::Down, 23, {1200, 500}));        // background swallowed
    assert(hud.OnTouch(Touch::Down, 24, jb));                 // joystick swallowed
    assert(!hud.JoystickActive());
    assert(hud.OnTouch(Touch::Down, 25, hud.PanelCloseCenter()));   // close
    assert(hud.OpenWindow() == Window::None && g_window == Window::None);

    assert(Pressed(hud, 26, Button::Menu) && hud.OpenWindow() == Window::Menu);
    hud.CloseWindow();
    assert(Pressed(hud, 27, Button::Character) && hud.OpenWindow() == Window::Character);
    hud.CloseWindow();
    assert(Pressed(hud, 28, Button::Club) && hud.OpenWindow() == Window::Club);
    hud.CloseWindow();
    assert(Pressed(hud, 29, Button::Skill) && hud.OpenWindow() == Window::Skill);
    hud.CloseWindow();
    assert(hud.OpenWindow() == Window::None && g_window == Window::None);
    assert(g_window_events == events_before + 10);            // 5 opens + 5 closes

    // Attack repeat is blocked while a panel is open.
    hud.OnTouch(Touch::Down, 30, a);
    hud.Update(0.f);
    int atk_before = g_attacks;
    assert(Pressed(hud, 31, Button::Bag));                    // opens bag, releases attack
    hud.Update(1.0f);
    assert(g_attacks == atk_before);
    hud.CloseWindow();

    // Opening a panel while the stick is held releases the stick.
    hud.OnTouch(Touch::Down, 32, jb);
    assert(hud.JoystickActive());
    hud.OnTouch(Touch::Move, 32, {jb.x + 120, jb.y});
    assert(g_dx > 0.5f);
    assert(Pressed(hud, 33, Button::Bag));
    assert(!hud.JoystickActive() && g_dx == 0.f && g_dy == 0.f);
    hud.CloseWindow();

    // ---- resize keeps state ----
    hud.Resize(2204.f, 1080.f, 2.75f);
    assert(hud.Running() == false && hud.AutoPotion() && hud.PotionPercent() == 20);
    assert(hud.SlotSkill(2) == 777);

    // A tap in the middle of the screen (no window) is not consumed.
    assert(!hud.OnTouch(Touch::Down, 40, {1100, 500}));

    std::puts("touch_hud: all checks passed");
    return 0;
}
