// mobile/android/app/src/main/cpp/bridge_api.h
// The small C++ surface that main.cpp (the render thread) uses to reach net_bridge.cpp (the net
// thread). Declaring it in one header keeps both sides in sync: the JNI entry points themselves are
// only called from Java, while the HUD hooks (plain function pointers) call these wrappers.
#pragma once

// Locks a monster as the target of the attack button and skills (0 clears the selection).
void BridgeSelectTarget(long long monsterId);
// Queues one basic attack; the next sync tick sends it (the render thread never touches the network).
void BridgeRequestAttack();
// Queues one skill cast on the locked target.
void BridgeRequestSkill(int skillId);
// Drinks the potion in an inventory slot through the server (the answer carries the new HP/MP).
bool BridgeUsePotion(int slot);
// Milliseconds left on a skill's cooldown (0 = ready), read by the render thread every frame.
int BridgeSkillCooldownMs(int skillId);
