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
static int g_stock = 100, g_item = -1, g_item_uses = 0;
static int g_world_taps = 0;
static Vec2 g_tap;
static Hooks g_hooks;   // the hooks Init() installed (the target test swaps them temporarily)

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

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
    h.UsePotion = [] {
        if (g_stock <= 0) return false;
        --g_stock;
        ++g_potions;
        return true;
    };
    h.UseItem = [](int id) { g_item = id; ++g_item_uses; };
    h.OnWindow = [](Window w) { g_window = w; ++g_window_events; };
    h.OnRunChanged = [](bool r) { g_run = r ? 1 : 0; };

    g_hooks = h;
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
    assert(hud.PanelRect().Contains(hud.MenuClubCenter()));
    assert(hud.OnTouch(Touch::Down, 30, hud.MenuClubCenter()) && hud.OpenWindow() == Window::Club);  // club is inside the menu
    hud.OnTouch(Touch::Up, 30, hud.MenuClubCenter());
    hud.CloseWindow();
    assert(Pressed(hud, 27, Button::Character) && hud.OpenWindow() == Window::Character);
    hud.CloseWindow();
    assert(Pressed(hud, 29, Button::Skill) && hud.OpenWindow() == Window::Skill);
    hud.CloseWindow();
    assert(hud.OpenWindow() == Window::None && g_window == Window::None);
    assert(g_window_events == events_before + 9);            // bag open+close, menu, club, character, skill: 5 opens + 4 closes

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

    // ---- skill window: drag a skill from the grid onto a quick slot ----
    const int skillIds[TouchHUD::kSkillCells] = {101, 102, 103, 104, 105, 106};
    hud.SetSkills(skillIds, TouchHUD::kSkillCells);
    assert(Pressed(hud, 60, Button::Skill) && hud.OpenWindow() == Window::Skill);
    for (int i = 0; i < TouchHUD::kSkillCells; ++i)            // grid stays inside the panel
        assert(hud.PanelRect().Contains(hud.SkillCellCenter(i)));
    assert(hud.OnTouch(Touch::Down, 61, hud.SkillCellCenter(1)));
    assert(hud.Dragging() && hud.DragSkill() == 102);
    hud.OnTouch(Touch::Move, 61, hud.SlotCenter(3));
    assert(Dist(hud.DragPos(), hud.SlotCenter(3)) < 0.01f);
    hud.OnTouch(Touch::Up, 61, hud.SlotCenter(3));
    assert(!hud.Dragging() && hud.SlotSkill(3) == 102);
    assert(g_saved_slot == 3 && g_saved_skill == 102);
    assert(hud.OpenWindow() == Window::Skill);                 // window stays open for more drags
    // Release on empty space: nothing changes.
    hud.OnTouch(Touch::Down, 62, hud.SkillCellCenter(2));
    hud.OnTouch(Touch::Up, 62, {1200, 500});
    assert(!hud.Dragging() && hud.SlotSkill(3) == 102 && g_saved_skill == 102);
    // Closing the window mid-drag cancels the drag.
    hud.OnTouch(Touch::Down, 63, hud.SkillCellCenter(0));
    assert(hud.Dragging());
    hud.CloseWindow();
    assert(!hud.Dragging() && hud.OpenWindow() == Window::None);
    // Empty grid cells cannot be dragged.
    const int fewIds[1] = {201};
    hud.SetSkills(fewIds, 1);
    assert(Pressed(hud, 64, Button::Skill));
    hud.OnTouch(Touch::Down, 65, hud.SkillCellCenter(4));
    assert(!hud.Dragging());
    hud.CloseWindow();

    // ---- bag: items and counts; tapping an item uses it ----
    hud.Update(3.f);                                          // cooldown is over
    hud.SetHp(100, 100);
    const int bagIds[2] = {1, 2};
    const int bagCounts[2] = {5, 1};
    hud.SetBagItems(bagIds, bagCounts, 2);
    assert(hud.BagItemAt(0) == 1 && hud.BagCountAt(0) == 5 && hud.BagItemAt(2) == 0);
    for (int i = 0; i < TouchHUD::kBagCells; ++i) {           // grid stays inside the panel
        assert(hud.PanelRect().Contains(hud.BagCellCenter(i)));
        for (int j = i + 1; j < TouchHUD::kBagCells; ++j)
            assert(Dist(hud.BagCellCenter(i), hud.BagCellCenter(j)) >= 2.f * hud.BagCellRadius());
    }
    assert(Pressed(hud, 70, Button::Bag) && hud.OpenWindow() == Window::Bag);
    hud.OnTouch(Touch::Down, 71, hud.BagCellCenter(0));       // potion stack
    hud.OnTouch(Touch::Up, 71, hud.BagCellCenter(0));
    assert(g_item == 1 && g_item_uses == 1);
    hud.OnTouch(Touch::Down, 72, hud.BagCellCenter(5));       // empty cell: nothing
    hud.OnTouch(Touch::Up, 72, hud.BagCellCenter(5));
    assert(g_item_uses == 1);
    hud.CloseWindow();

    // Manual potion with no stock: no heal, but the cooldown still starts.
    const int potionsBefore = g_potions;
    g_stock = 0;
    assert(Pressed(hud, 73, Button::Potion) && g_potions == potionsBefore);
    assert(hud.PotionCooldownLeft() > 0.f);
    g_stock = 100;
    hud.Update(3.f);

    // ---- resize keeps state ----
    hud.Resize(2204.f, 1080.f, 2.75f);
    assert(hud.Running() == false && hud.AutoPotion() && hud.PotionPercent() == 20);
    assert(hud.SlotSkill(2) == 777);

    // A tap in the middle of the screen: nothing of the HUD is hit.
    if (g_world_taps == 0) assert(!hud.OnTouch(Touch::Down, 40, {1100, 500}));

    // ---- target selection: a playfield tap goes to the game, HUD controls do not ----
    g_world_taps = 0;
    Hooks tapHooks = g_hooks;
    tapHooks.OnWorldTap = [](Vec2 p) { ++g_world_taps; g_tap = p; };
    hud.SetHooks(tapHooks);
    assert(hud.OnTouch(Touch::Down, 41, {1100, 500}));      // playfield -> OnWorldTap
    assert(g_world_taps == 1 && Near(g_tap.x, 1100.f));
    hud.OnTouch(Touch::Down, 42, hud.AttackCenter());      // attack button is the HUD's
    assert(g_world_taps == 1);
    hud.OnTouch(Touch::Up, 42, hud.AttackCenter());
    hud.OnTouch(Touch::Down, 43, hud.SlotCenter(0));       // skill slot is the HUD's
    assert(g_world_taps == 1);
    hud.OnTouch(Touch::Up, 43, hud.SlotCenter(0));
    hud.SetHooks(g_hooks);

    // ---- stat bars, level and target ----
    hud.SetStats(70.f, 140.f, 30.f, 60.f, 50.f, 200.f, 3);
    assert(Near(hud.HpRatio(), 0.5f) && Near(hud.MpRatio(), 0.5f) && Near(hud.ExpRatio(), 0.25f));
    assert(hud.Level() == 3);
    hud.SetStats(999.f, 140.f, -5.f, 60.f, 50.f, 0.f, 0);   // clamped, level at least 1
    assert(Near(hud.HpRatio(), 1.f) && Near(hud.MpRatio(), 0.f) && hud.Level() == 1);
    hud.SetTarget(7, 30.f, 120.f);
    assert(hud.TargetId() == 7 && Near(hud.TargetHpRatio(), 0.25f));
    hud.SetTarget(0, 0.f, 0.f);
    assert(hud.TargetId() == 0 && Near(hud.TargetHpRatio(), 0.f));

    // ---- skill cooldowns count down ----
    hud.SetSkillCooldown(1, 2.f);
    assert(Near(hud.SkillCooldown(1), 2.f) && Near(hud.SkillCooldown(0), 0.f));
    hud.Update(0.5f);
    assert(Near(hud.SkillCooldown(1), 1.5f));
    hud.Update(5.f);
    assert(Near(hud.SkillCooldown(1), 0.f));

    std::puts("touch_hud: all checks passed");
    return 0;
}
