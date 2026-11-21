#include "touch_hud.h"
#include <cassert>
#include <cstdio>

static float g_dx = 9, g_dy = 9;
static int g_attacks = 0, g_skill = 0, g_bag = 0, g_saved_slot = -1, g_saved_skill = -1;

int main() {
    using namespace mobile;
    Hooks h;
    h.MoveDir = [](float dx, float dy) { g_dx = dx; g_dy = dy; };
    h.BasicAttack = [] { ++g_attacks; };
    h.UseSkill = [](int id) { g_skill = id; };
    h.OpenInventory = [] { ++g_bag; };
    h.SaveSlot = [](int s, int id) { g_saved_slot = s; g_saved_skill = id; };

    TouchHUD hud;
    hud.Init(2400.f, 1080.f, 3.0f, h);   // typical 20:9 phone, 3x density

    // No slot pair overlaps.
    for (int i = 0; i < TouchHUD::kSlots; ++i)
        for (int j = i + 1; j < TouchHUD::kSlots; ++j)
            assert(Dist(hud.SlotCenter(i), hud.SlotCenter(j)) >= 2.f * hud.SlotRadius());
    // Every slot is fully on screen and outside the attack button.
    for (int i = 0; i < TouchHUD::kSlots; ++i) {
        Vec2 c = hud.SlotCenter(i);
        assert(c.x - hud.SlotRadius() >= 0 && c.y - hud.SlotRadius() >= 0);
        assert(Dist(c, hud.AttackCenter()) >= hud.AttackRadius() + hud.SlotRadius());
    }

    // Joystick: drag right -> dx > 0, release -> stop.
    assert(hud.OnTouch(Touch::Down, 1, {300, 700}));
    assert(hud.JoystickActive());
    hud.OnTouch(Touch::Move, 1, {300 + 90, 700});      // half of max radius (180px)
    assert(std::fabs(g_dx - 0.5f) < 0.01f && std::fabs(g_dy) < 0.01f);
    hud.OnTouch(Touch::Move, 1, {300 + 400, 700});     // far past max radius: clamped to 1
    assert(std::fabs(g_dx - 1.0f) < 0.01f && std::fabs(g_dy) < 0.01f);
    hud.OnTouch(Touch::Move, 1, {300 + 10, 700});      // inside dead zone: stop
    assert(g_dx == 0.f && g_dy == 0.f);
    hud.OnTouch(Touch::Up, 1, {0, 0});
    assert(!hud.JoystickActive() && g_dx == 0.f && g_dy == 0.f);

    // Attack button + hold repeat.
    Vec2 a = hud.AttackCenter();
    assert(hud.OnTouch(Touch::Down, 2, a));
    assert(g_attacks == 1);
    hud.Update(0.5f);
    assert(g_attacks == 2);
    hud.OnTouch(Touch::Up, 2, a);
    hud.Update(0.5f);
    assert(g_attacks == 2);

    // Drag a skill from the skill window and drop on slot 2, then tap it.
    assert(hud.DropSkill(777, hud.SlotCenter(2)));
    assert(g_saved_slot == 2 && g_saved_skill == 777);
    assert(!hud.DropSkill(777, {10, 10}));            // dropped on empty space
    hud.OnTouch(Touch::Down, 3, hud.SlotCenter(2));
    assert(g_skill == 777);

    // Bag button.
    hud.OnTouch(Touch::Down, 4, hud.BagCenter());
    assert(g_bag == 1);

    // Touch in the middle of the screen is not consumed (camera/UI gets it).
    assert(!hud.OnTouch(Touch::Down, 5, {1200, 500}));

    std::puts("touch_hud: all checks passed");
    return 0;
}
