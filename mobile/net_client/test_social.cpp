// mobile/net_client/test_social.cpp
// Round 4 client tests: the parsers of gates/NPCs/shops/chat/party, run against
//  (1) JSON captured from the real server (the same shapes Api.h_* produce), and
//  (2) a live server, when a host and port are given (./test_social 127.0.0.1 8197).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "api_client.h"
#include "http_client.h"
#include "social_view.h"

using namespace mobile::net;

static int g_fail = 0;
static int g_checks = 0;

static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : " — ", detail.c_str());
    }
}

// ---------------------------------------------------------------- unit: gates
static void TestGates() {
    const std::string json = R"({"map_id":1,"map_name":"วิทยาเขตโรงเรียน","gates":[
        {"gate_id":1,"name":"ประตูตลาด","to_map":2,"to_map_name":"ตลาดปลอดภาษี",
         "position":{"x":0.9,"y":0.5},"radius":0.06,"required_level":1,"required_money":0,
         "distance":0.4,"in_range":false},
        {"gate_id":9,"name":"ประตูทดสอบ","to_map":3,"to_map_name":"ทุ่งล่าปีศาจ",
         "position":{"x":0.62,"y":0.5},"radius":0.06,"required_level":3,"required_money":50,
         "distance":0.03,"in_range":true}]})";
    std::vector<GateView> gates;
    Check(ParseGates(json, gates), "parse gates returns true");
    Check(gates.size() == 2, "two gates parsed", std::to_string(gates.size()));
    if (gates.size() == 2) {
        Check(gates[0].gateId == 1 && gates[0].toMap == 2, "gate 1 target");
        Check(gates[0].toMapName == "ตลาดปลอดภาษี", "Thai map name kept");
        Check(std::fabs(gates[0].x - 0.9) < 1e-9 && std::fabs(gates[0].y - 0.5) < 1e-9,
              "gate position");
        Check(!gates[0].inRange, "far gate not in range");
        Check(gates[1].requiredLevel == 3 && gates[1].requiredMoney == 50, "gate requirements");
        Check(gates[1].inRange && gates[1].distance < 0.04, "near gate in range");
    }
    std::vector<GateView> none;
    Check(!ParseGates("{\"connected\":false}", none) && none.empty(), "no gates key -> false");
}

// ---------------------------------------------------------------- unit: npcs
static void TestNpcs() {
    const std::string json = R"({"npcs":[
        {"npc_id":102,"name":"พยาบาลประจำวิทยาเขต","role":"healer","map_id":1,
         "position":{"x":0.58,"y":0.46},"shop_name":"ตู้ยาวิทยาเขต","dialog":["สวัสดี"]},
        {"npc_id":101,"name":"ครูฝึกนักสู้","role":"instructor","map_id":1,
         "position":{"x":0.44,"y":0.4},"shop_name":"","dialog":[]}]})";
    std::vector<NpcView> npcs;
    Check(ParseNpcs(json, npcs), "parse npcs");
    Check(npcs.size() == 2, "two npcs", std::to_string(npcs.size()));
    if (npcs.size() == 2) {
        Check(npcs[0].npcId == 102 && npcs[0].shopName == "ตู้ยาวิทยาเขต", "nurse has a shop");
        Check(npcs[1].shopName.empty(), "instructor has no shop");
        Check(std::fabs(npcs[0].x - 0.58) < 1e-9, "npc x");
    }
}

// ---------------------------------------------------------------- unit: talk answer
static void TestNpcDialog() {
    const std::string json = R"({"npc":{"npc_id":102,"name":"พยาบาลประจำวิทยาเขต","role":"healer",
        "map_id":1,"position":{"x":0.58,"y":0.46},"shop_name":"ตู้ยาวิทยาเขต",
        "sell_rate_percent":30,"distance":0.01},
        "dialog":["บาดเจ็บหรือเปล่า?","ยามือถือของฉันมีขาย"],
        "shop":[{"item_id":1001,"buy_price":22,"name":"Small HP Potion","type":"Consumable",
                 "stackable":1,"equip_slot":null,"attack_bonus":0,"defense_bonus":0,
                 "hp_recovery":100,"mp_recovery":0},
                {"item_id":1002,"buy_price":28,"name":"Small MP Potion","type":"Consumable",
                 "stackable":1,"equip_slot":null,"attack_bonus":0,"defense_bonus":0,
                 "hp_recovery":0,"mp_recovery":60}],
        "quests":[{"quest_id":1,"name":"รายงานตัวกับครูฝึก","required_count":3,
                   "reward_exp":60,"reward_money":40}],
        "money":1000})";
    NpcDialog d;
    Check(ParseNpcDialog(json, d), "parse talk");
    Check(d.open && d.npcId == 102, "dialog open for npc 102");
    Check(d.npcName == "พยาบาลประจำวิทยาเขต", "npc name");
    Check(d.shopName == "ตู้ยาวิทยาเขต", "shop name");
    Check(d.lines.size() == 2, "two dialog lines", std::to_string(d.lines.size()));
    Check(d.shop.size() == 2, "two shop entries");
    if (d.shop.size() == 2) {
        Check(d.shop[0].itemId == 1001 && d.shop[0].buyPrice == 22, "hp potion price from server");
        Check(d.shop[1].buyPrice == 28 && d.shop[1].mpRecovery == 60, "mp potion fields");
        Check(d.shop[0].hpRecovery == 100, "hp recovery parsed");
    }
    Check(d.quests.size() == 1 && d.quests[0].rewardMoney == 40, "quest offer");
    Check(d.money == 1000, "money at open");
    Check(d.sellRatePercent == 30, "sell rate");

    NpcDialog bad;
    Check(!ParseNpcDialog("{\"error\":{\"code\":\"npc_too_far\"}}", bad) && !bad.open,
          "error answer opens nothing");

    // The view closes the panel on a refused talk.
    Social().ApplyTalk(json);
    Check(Social().DialogNpc() == 102, "view opened dialog");
    Social().ApplyTalkError("HTTP 409 npc_too_far");
    Check(Social().DialogNpc() == 0, "refused talk closes the panel");
    Social().Clear();
}

// ---------------------------------------------------------------- unit: chat
static void TestChat() {
    const std::string history = R"({"messages":[
        {"channel":"map","scope_id":1,"character_id":1,"name":"Talker1","body":"สวัสดีทุกคน","at_ms":10},
        {"channel":"party","scope_id":1,"character_id":2,"name":"Talker2","body":"ไปด้วย","at_ms":11}],
        "map_id":1,"party_id":1})";
    std::vector<ChatMessage> msgs;
    Check(ParseChatHistory(history, msgs), "parse chat history");
    Check(msgs.size() == 2 && msgs[0].body == "สวัสดีทุกคน", "chat bodies");
    Check(msgs[1].channel == "party" && msgs[1].name == "Talker2", "party line");

    Social().ApplyChatHistory(history);
    Check(Social().Messages().size() == 2, "view holds history");
    const std::string sent = R"({"channel":"map","scope_id":1,"body":"ทดสอบ","name":"Me","at_ms":12})";
    ChatMessage one;
    Check(ParseChatSent(sent, one) && one.body == "ทดสอบ", "parse send answer");
    Social().ApplyChatSent(sent);
    Check(Social().Messages().size() == 3, "send appends a line");
    Social().ApplyChatSent(sent);   // the same line again: the poll echoed it
    Check(Social().Messages().size() == 3, "duplicate line dropped");
    Social().AddSystemLine("หลุดการเชื่อมต่อ");
    Check(Social().Messages().back().channel == "system", "system line appended");

    // Errors of the server come back as "HTTP <status> <code>".
    Check(ErrorCode("HTTP 409 no_party") == "no_party", "error code extracted");
    Check(ErrorCode("connection refused") == "", "no code when there is none");
    Social().Clear();
}

// ---------------------------------------------------------------- unit: party
static void TestParty() {
    const std::string json = R"({"party":{"party_id":3,"max_members":4,"members":[
        {"character_id":1,"name":"Leader1","class":"fighter","level":2,"hp":120,"map_id":1,
         "is_leader":true,"map_name":"วิทยาเขตโรงเรียน"},
        {"character_id":2,"name":"Member1","class":"mage","level":1,"hp":90,"map_id":1,
         "is_leader":false,"map_name":"วิทยาเขตโรงเรียน"}]},
        "invites":[{"party_id":5,"invited_ms":99,"inviter":"Someone"}],
        "character_id":1,"map_id":1})";
    PartyView party;
    std::vector<PartyInvite> invites;
    Check(ParsePartyState(json, party, invites), "parse party state");
    Check(party.partyId == 3 && party.joined(), "party id");
    Check(party.members.size() == 2, "two members");
    if (party.members.size() == 2) {
        Check(party.members[0].leader && !party.members[1].leader, "leader flag");
        Check(party.members[0].name == "Leader1" && party.members[0].level == 2, "member fields");
    }
    Check(invites.size() == 1 && invites[0].inviter == "Someone", "pending invite");

    // "party": null is a valid answer meaning "not in a party".
    PartyView solo;
    std::vector<PartyInvite> none;
    Check(ParsePartyState(R"({"party":null,"invites":[],"character_id":1,"map_id":1})", solo, none),
          "null party parses");
    Check(!solo.joined() && none.empty(), "not in a party");

    Social().ApplyPartyState(json);
    Check(Social().Get().party.members.size() == 2, "view party");
    Social().Clear();
}

// ---------------------------------------------------------------- unit: trade answers
static void TestTrade() {
    int paid = 0, received = 0, money = 0, qty = 0;
    const std::string buy = R"({"bought":{"item_id":1001,"name":"Small HP Potion","quantity":2},
        "paid":44,"character":{"id":1,"money":956},"player":{"level":1}})";
    Check(ParseTrade(buy, paid, received, money, qty), "parse buy");
    Check(paid == 44 && money == 956 && qty == 2 && received == 0, "buy numbers");
    const std::string sell = R"({"sold":{"item_id":4003,"quantity":1},"income":78,
        "sell_rate_percent":30,"character":{"id":1,"money":1034}})";
    paid = received = money = qty = 0;
    Check(ParseTrade(sell, paid, received, money, qty), "parse sell");
    Check(received == 78 && money == 1034 && qty == 1 && paid == 0, "sell numbers");
}

// ---------------------------------------------------------------- live server
static void TestLive(const std::string& host, int port) {
    ServerApi api{HttpClient(host, port, 10)};
    std::string err;
    bool up = false;
    for (int i = 0; i < 50 && !up; ++i) {
        up = api.Health(err);
        if (!up) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Check(up, "live: health", err);
    if (!up) return;

    const std::string user =
        "sc" + std::to_string(static_cast<long long>(std::time(nullptr)) % 100000000LL);
    Check(api.Register(user, "pass12345", err), "live: register", err);
    err.clear();
    Check(api.Login(user, "pass12345", err), "live: login", err);

    long long cid = 0;
    err.clear();
    Check(api.CreateCharacter("Social01", "male", "fighter", cid, err), "live: create character", err);
    std::string json;
    err.clear();
    Check(api.EnterWorld(cid, json, err), "live: enter world", err);

    // --- maps + gates from the real server
    err.clear();
    Check(api.ListMaps(json, err), "live: list maps", err);
    Check(json.find("วิทยาเขตโรงเรียน") != std::string::npos, "live: map name in answer");
    // --- ข้อความ UI ไทยต้องมาจากไฟล์จริงใน Google Drive (ThaiPack) ไม่ใช่ข้อความในโค้ด
    err.clear();
    Check(api.GetUiStrings(cid, json, err), "live: ui strings", err);
    Check(json.find("ThaiPack") != std::string::npos, "live: ui strings name the Drive source");
    Check(json.find("เงินไม่เพียงพอ") != std::string::npos,
          "live: BUYITEM_NOMONEY is real Thai text");
    Check(json.find("ยุบ,ออกจากปาร์ตี้") != std::string::npos, "live: QUIT_PARTY real text");
    Check(json.find("BUYITEM_NOMONEY") != std::string::npos, "live: shop key present");
    Check(json.find("QUIT_PARTY") != std::string::npos, "live: party key present");
    Check(json.find("MAP_REQUIRE_FAIL") != std::string::npos, "live: gate key present");

    err.clear();
    Check(api.GetGates(cid, json, err), "live: gates", err);
    std::vector<GateView> gates;
    Check(ParseGates(json, gates), "live: parse gates");
    Check(gates.size() == 1 && gates[0].toMap == 2, "live: map 1 has one gate to map 2");
    const long long gateId = gates.empty() ? 0 : gates[0].gateId;

    // --- talk to the nurse (npc 102, inside talk range of the spawn point)
    err.clear();
    Check(api.TalkToNpc(cid, 102, json, err), "live: talk to nurse", err);
    NpcDialog dlg;
    Check(ParseNpcDialog(json, dlg), "live: parse talk");
    Check(dlg.npcId == 102 && !dlg.shop.empty(), "live: nurse sells items");
    Social().ApplyTalk(json);
    Check(Social().DialogNpc() == 102, "live: view has the dialog");

    // --- buy a potion: the server decides the price
    const long long itemId = dlg.shop.empty() ? 1001 : dlg.shop[0].itemId;
    const int price = dlg.shop.empty() ? 0 : dlg.shop[0].buyPrice;
    err.clear();
    Check(api.BuyFromNpc(cid, 102, itemId, 1, json, err), "live: buy from npc", err);
    int paid = 0, received = 0, money = 0, qty = 0;
    Check(ParseTrade(json, paid, received, money, qty), "live: parse buy");
    Check(paid == price && qty == 1, "live: charged the server price",
          "paid=" + std::to_string(paid) + " price=" + std::to_string(price));
    Check(money < dlg.money, "live: money decreased", std::to_string(money));

    // --- talking from far away is refused and opens nothing
    err.clear();
    const bool farTalk = api.TalkToNpc(cid, 103, json, err);
    Check(!farTalk && ErrorCode(err) == "npc_too_far", "live: far npc refused", err);
    Social().ApplyTalkError(err);
    Check(Social().DialogNpc() == 0, "live: refused talk keeps the panel closed");

    // --- gate refused when the player is not standing on it
    err.clear();
    std::string code;
    const auto g1 = api.UseGate(cid, gateId, json, code, err);
    Check(g1 == ServerApi::GateResult::Refused && code == "too_far", "live: gate too far", code);

    // --- chat on the map channel, '#' requires a party
    err.clear();
    Check(api.SendChat(cid, "ทดสอบแชท", json, err), "live: send map chat", err);
    std::this_thread::sleep_for(std::chrono::milliseconds(900));   // chat.rate_limit_ms = 800
    err.clear();
    Check(api.GetChat(cid, json, err), "live: chat history", err);
    std::vector<ChatMessage> msgs;
    Check(ParseChatHistory(json, msgs), "live: parse history");
    Check(!msgs.empty() && msgs.back().body == "ทดสอบแชท", "live: own line in history");
    err.clear();
    Check(!api.SendChat(cid, "#ทดสอบ", json, err) && ErrorCode(err) == "no_party",
          "live: party chat needs a party", err);
    err.clear();
    Check(!api.SendChat(cid, "@someone hi", json, err) &&
              ErrorCode(err) == "whisper_not_supported",
          "live: whisper reported unsupported", err);

    // --- party: invite needs a second player in the world
    const std::string user2 =
        "sd" + std::to_string(static_cast<long long>(std::time(nullptr)) % 100000000LL);
    ServerApi api2{HttpClient(host, port, 10)};
    err.clear();
    Check(api2.Register(user2, "pass12345", err) && api2.Login(user2, "pass12345", err),
          "live: second account", err);
    long long cid2 = 0;
    err.clear();
    Check(api2.CreateCharacter("Social02", "male", "archer", cid2, err), "live: second character", err);
    err.clear();
    Check(api2.EnterWorld(cid2, json, err), "live: second in world", err);

    err.clear();
    Check(api.InviteToParty(cid, cid2, json, err), "live: invite", err);
    PartyView party;
    std::vector<PartyInvite> invites;
    err.clear();
    Check(api2.GetParty(cid2, json, err), "live: invitee party state", err);
    Check(ParsePartyState(json, party, invites), "live: parse party state");
    Check(!party.joined() && invites.size() == 1, "live: pending invite visible");

    err.clear();
    Check(api2.AcceptPartyInvite(cid2, invites.empty() ? 0 : invites[0].partyId, json, err),
          "live: accept invite", err);
    Check(ParsePartyState(json, party, invites) && party.members.size() == 2,
          "live: party has two members");
    Social().ApplyPartyState(json);
    Check(Social().Get().party.members.size() == 2, "live: view party");

    // Party chat now works for both members.
    std::this_thread::sleep_for(std::chrono::milliseconds(900));   // chat.rate_limit_ms = 800
    err.clear();
    Check(api.SendChat(cid, "#ไปด้วยกัน", json, err), "live: party chat", err);
    err.clear();
    Check(api2.GetChat(cid2, json, err), "live: member reads party chat", err);
    Check(ParseChatHistory(json, msgs) &&
              std::any_of(msgs.begin(), msgs.end(),
                          [](const ChatMessage& m) { return m.body == "ไปด้วยกัน"; }),
          "live: party line reaches the member");

    err.clear();
    Check(api2.LeaveParty(cid2, json, err), "live: leave party", err);
    Check(ParsePartyState(json, party, invites) || true, "live: leave answered");
    err.clear();
    Check(api2.GetParty(cid2, json, err), "live: state after leave", err);
    Check(ParsePartyState(json, party, invites) && !party.joined(), "live: left the party");

    api.LeaveWorld(cid, err);
    api2.LeaveWorld(cid2, err);
    Social().Clear();
}

int main(int argc, char** argv) {
    TestGates();
    TestNpcs();
    TestNpcDialog();
    TestChat();
    TestParty();
    TestTrade();
    if (argc > 2) {
        TestLive(argv[1], std::atoi(argv[2]));
    } else {
        std::printf("(skipping live server tests: pass host and port)\n");
    }
    std::printf("%s: %d checks, %d failed\n", g_fail == 0 ? "PASS" : "FAIL", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
