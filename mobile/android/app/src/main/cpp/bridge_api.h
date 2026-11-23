// mobile/android/app/src/main/cpp/bridge_api.h
// The small C++ surface that main.cpp (the render thread) uses to reach net_bridge.cpp (the net
// thread). Declaring it in one header keeps both sides in sync: the JNI entry points themselves are
// only called from Java, while the HUD hooks (plain function pointers) call these wrappers.
#pragma once

struct android_app;   // <android_native_app_glue.h>

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

// Saves the JavaVM and MainActivity so the render thread can post round-4 jobs to Java (which runs
// them on its net thread). Call once from android_main with the android_app.
void BridgeAttachApp(android_app* app);

// ---------------------------------------------------------------- round 4 panels
// Server actions of the round-4 panels. Each one queues the work on the net thread (the render
// thread never touches the network) and returns whether it was queued, not whether the server
// accepted: the answer arrives through SocialView and the next panel refresh.
bool BridgeTalkToNpc(long long npcId);
bool BridgeUseGate(long long gateId);
bool BridgePartyAction(int kind, long long partyId);   // kind: 0 = accept, 1 = leave
bool BridgeSendChat(const char* text, int channel);    // channel: 0 = map, 1 = party

// Opens the social panel (party + NPCs) in Java: an AlertDialog with one entry per row, which calls
// the natives in net_bridge.cpp. Called from the HUD hook on the render thread.
void BridgeOpenSocialPanel();
// Opens the shop/dialog panel of the NPC the player just talked to.
void BridgeOpenNpcDialog();
// Opens the chat panel (map / party channel, quick messages).
void BridgeOpenChatPanel();
