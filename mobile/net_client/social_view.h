// mobile/net_client/social_view.h
// Round 4 client state: maps and warp gates, NPCs and their shops, the chat log and the party.
//
// Everything the HUD needs to draw is parsed here from the server's answers — the client never
// decides prices, gate rules, EXP or who is in the party. No Android/JNI/socket code, so the whole
// module is unit-tested on the host (make -C mobile/net_client test-social).
//
// Threading, same as world_realtime: the net thread calls Apply*/Chat*/Party*, the render thread
// reads Snapshot()/Messages().
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace mobile::net {

// One warp gate on the current map (Api._gate_view on the server).
struct GateView {
    long long gateId = 0;
    std::string name;
    long long toMap = 0;
    std::string toMapName;
    double x = 0.5, y = 0.5;      // where the player must stand (map fractions)
    double radius = 0.0;
    int requiredLevel = 1;
    int requiredMoney = 0;
    double distance = 0.0;
    bool inRange = false;
};

// One NPC standing on the current map (Api._npc_view on the server).
struct NpcView {
    long long npcId = 0;
    std::string name;
    std::string role;
    std::string shopName;         // empty = this NPC has no shop
    double x = 0.5, y = 0.5;
};

// One line of the shop the server returned from the talk answer.
struct ShopEntry {
    long long itemId = 0;
    std::string name;
    std::string type;
    int buyPrice = 0;             // price the server charges (never a client-side guess)
    bool stackable = false;
    int attackBonus = 0;
    int defenseBonus = 0;
    int hpRecovery = 0;
    int mpRecovery = 0;
};

// A quest offered by the NPC the player is talking to.
struct QuestOffer {
    long long questId = 0;
    std::string name;
    int requiredCount = 0;
    int rewardExp = 0;
    int rewardMoney = 0;
};

// The answer of POST .../npc/{id}/talk — what the shop/dialog panel draws.
struct NpcDialog {
    bool open = false;
    long long npcId = 0;
    std::string npcName;
    std::string shopName;
    int sellRatePercent = 30;
    std::vector<std::string> lines;
    std::vector<ShopEntry> shop;
    std::vector<QuestOffer> quests;
    int money = 0;                // the player's money when the panel opened
};

// One chat message (the server filters by map/party).
struct ChatMessage {
    std::string channel;          // map | party | system
    std::string name;
    std::string body;
    long long atMs = 0;
};

// One member of the player's party.
struct PartyMemberView {
    long long characterId = 0;
    std::string name;
    std::string cls;
    int level = 1;
    int hp = 0;
    bool leader = false;
    bool dead = false;
    std::string mapName;
};

// The party the player is in (empty `partyId` 0 = no party).
struct PartyView {
    long long partyId = 0;
    int maxMembers = 4;
    std::vector<PartyMemberView> members;
    bool joined() const { return partyId != 0; }
};

// A pending invitation addressed to the player.
struct PartyInvite {
    long long partyId = 0;
    std::string inviter;
};

// ---------------------------------------------------------------- parsing helpers

// Warp gates of a GET .../map/gates or world/sync answer ("gates": [...]).
bool ParseGates(const std::string& json, std::vector<GateView>& out);
// NPCs of a world/sync answer ("npcs": [...]).
bool ParseNpcs(const std::string& json, std::vector<NpcView>& out);
// The talk answer of POST .../npc/{id}/talk.
bool ParseNpcDialog(const std::string& json, NpcDialog& out);
// The chat history answer ("messages": [...]) and one send answer (a single message object).
bool ParseChatHistory(const std::string& json, std::vector<ChatMessage>& out);
bool ParseChatSent(const std::string& json, ChatMessage& out);
// The party state answer ("party": {...} or null, "invites": [...]).
bool ParsePartyState(const std::string& json, PartyView& party, std::vector<PartyInvite>& invites);
// The buy/sell answers: how much money moved and the player's money after the trade.
bool ParseTrade(const std::string& json, int& paid, int& received, int& moneyAfter, int& quantity);

// "HTTP 409 gate_locked" -> "gate_locked" ("" when the text carries no server code).
std::string ErrorCode(const std::string& err);

// ---------------------------------------------------------------- shared state

class SocialView {
public:
    // Net thread. The full world/sync answer carries the gates and the NPCs of the map.
    void ApplySync(const std::string& json);
    void SetGates(std::vector<GateView> gates);
    void SetNpcs(std::vector<NpcView> npcs);

    // Net thread. The talk answer opens (or, with an empty answer, closes) the shop panel.
    void ApplyTalk(const std::string& json);
    bool ApplyTalkError(const std::string& err);   // false = the panel stays closed
    void CloseDialog();

    // Net thread. Chat history replaces the log; one send answer appends a line.
    void ApplyChatHistory(const std::string& json);
    void ApplyChatSent(const std::string& json);
    // A line the app itself produced (a disconnect notice, a refused gate), drawn in the log.
    void AddSystemLine(const std::string& text);

    // Net thread. Party state replaces the party and the pending invites.
    void ApplyPartyState(const std::string& json);

    // Render thread. Copies for one frame.
    struct Snapshot {
        std::vector<GateView> gates;
        std::vector<NpcView> npcs;
        NpcDialog dialog;
        PartyView party;
        std::vector<PartyInvite> invites;
        std::vector<ChatMessage> chat;   // newest last, at most kMaxChat lines
    };
    Snapshot Get() const;
    std::vector<ChatMessage> Messages() const;   // chat only (the log panel redraws often)

    // Either thread: which NPC the player is talking to (0 = none) and the gate they touched.
    long long DialogNpc() const;
    // Nearest gate within its radius (0 when the player is not standing on one).
    long long GateUnderPlayer() const;

    void Clear();

    static constexpr size_t kMaxChat = 60;

private:
    mutable std::mutex mu_;
    std::vector<GateView> gates_;
    std::vector<NpcView> npcs_;
    NpcDialog dialog_;
    PartyView party_;
    std::vector<PartyInvite> invites_;
    std::vector<ChatMessage> chat_;
};

// The single social view of the app (shared by the net thread and the render thread).
SocialView& Social();

}  // namespace mobile::net
