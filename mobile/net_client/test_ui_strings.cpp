// mobile/net_client/test_ui_strings.cpp
// เทสต์ข้อความ UI ไทยที่มาจากไฟล์จริงใน Google Drive (ThaiPack data/gui/game*_th.txt)
//
//   ./test_ui_strings                  → เทสต์ตัวแยกวิเคราะห์/แทนค่า (ไม่ต้องมีเซิร์ฟเวอร์)
//   ./test_ui_strings 127.0.0.1 8212   → เพิ่มการดึงจากเซิร์ฟเวอร์จริง (/api/ui/strings)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "api_client.h"
#include "http_client.h"
#include "ui_strings.h"

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

// ---------------------------------------------------------------- ตัวแยกวิเคราะห์
static void TestParse() {
    // รูปแบบเดียวกับที่ Api.h_ui_strings ส่งมา (ค่าเป็น UTF-8 ตรง ๆ)
    const std::string json =
        R"({"count":3,"source":"Google Drive RAN-EP7-ThaiPack data/gui/*_th.txt","strings":{)"
        R"("SENTENSE,BUYITEM_NOMONEY":"เงินไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้",)"
        R"("SENTENSE,BUY_ITEM_CONFIRM":"คุณต้องการซื้อ [%s] หรือไม่?",)"
        R"("SENTENSE,LEVELUP":"ยินดีด้วย! เลเวลอัปเป็นเลเวล '%d' แล้ว!"}})";
    UiStrings ui;
    Check(ui.Parse(json), "parse ui strings");
    Check(ui.Size() == 3, "three keys", std::to_string(ui.Size()));
    Check(ui.Raw("SENTENSE,BUYITEM_NOMONEY") == "เงินไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้",
          "buy-no-money text verbatim");
    Check(ui.Raw("SENTENSE,UNKNOWN").empty(), "missing key is empty");

    // %s / %d แทนค่าตามรูปแบบจริงของเกม
    Check(ui.Format("SENTENSE,BUY_ITEM_CONFIRM", {"น้ำยาฟื้นฟู"}) ==
              "คุณต้องการซื้อ [น้ำยาฟื้นฟู] หรือไม่?",
          "format %s");
    Check(ui.Format("SENTENSE,LEVELUP", {}, {12}) ==
              "ยินดีด้วย! เลเวลอัปเป็นเลเวล '12' แล้ว!",
          "format %d");
    // คีย์ที่ไม่มี ต้องคืนข้อความสำรอง ไม่ใช่ค่าว่าง
    Check(ui.Format("SENTENSE,NOPE", {}, {}, "สำรอง") == "สำรอง", "fallback for missing key");
    // %s ที่ไม่มีอาร์กิวเมนต์ต้องไม่ทำให้ค่าหาย
    Check(ui.Format("SENTENSE,BUY_ITEM_CONFIRM") == "คุณต้องการซื้อ [%s] หรือไม่?",
          "unfilled %s is kept");

    // escape แบบ \uXXXX (กรณี server serialize ด้วย ensure_ascii=True)
    UiStrings esc;
    Check(esc.Parse(R"({"strings":{"WORD,QUIT_PARTY":"\u0e22\u0e38\u0e1a,\u0e2d\u0e2d\u0e01"}})"),
          "parse escaped json");
    Check(esc.Raw("WORD,QUIT_PARTY") == "ยุบ,ออก",
          "\\u escapes become UTF-8 Thai", esc.Raw("WORD,QUIT_PARTY"));

    // ข้อความที่มีเครื่องหมาย """ และ \n ต้องไม่ทำให้ตัวแยกพัง
    UiStrings tricky;
    Check(tricky.Parse(R"({"strings":{"A":"พูด \"สวัสดี\"\nบรรทัดใหม่"}})"), "parse quoted value");
    Check(tricky.Raw("A") == "พูด \"สวัสดี\"\nบรรทัดใหม่", "quotes and newline restored");

    // JSON ที่ไม่มี "strings" ต้องไม่ถูกนับว่าสำเร็จ
    UiStrings bad;
    Check(!bad.Parse(R"({"error":{"code":"unauthorized"}})"), "answer without strings is rejected");
    Check(bad.Size() == 0, "nothing parsed from a bad answer");
}

// ---------------------------------------------------------------- เซิร์ฟเวอร์จริง
static void TestLive(const char* host, int port) {
    HttpClient http(host, port);
    ServerApi api(http);
    std::string err;
    const bool up = api.Health(err);
    Check(up, "live: health", err);
    if (!up) return;

    const std::string user =
        "ui" + std::to_string(static_cast<long long>(std::time(nullptr)) % 100000000LL);
    Check(api.Register(user, "pass12345", err), "live: register", err);
    err.clear();
    Check(api.Login(user, "pass12345", err), "live: login", err);

    long long cid = 0;
    err.clear();
    Check(api.CreateCharacter("UiText01", "male", "fighter", cid, err), "live: create", err);
    std::string json;
    err.clear();
    Check(api.EnterWorld(cid, json, err), "live: enter world", err);

    err.clear();
    Check(api.GetUiStrings(cid, json, err), "live: fetch ui strings", err);
    Check(json.find("ThaiPack") != std::string::npos, "live: answer names the Drive source");

    UiStrings ui;
    Check(ui.Parse(json), "live: parse server answer");
    Check(ui.Size() > 600, "live: hundreds of real strings", std::to_string(ui.Size()));

    // ค่าที่ตรวจกับไฟล์ Drive แล้วว่าตรง
    Check(ui.Raw("SENTENSE,BUYITEM_NOMONEY") == "เงินไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้",
          "live: BUYITEM_NOMONEY from Drive", ui.Raw("SENTENSE,BUYITEM_NOMONEY"));
    Check(ui.Raw("SENTENSE,BUYITEM_NOSPACE") == "ช่องเก็บของไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้",
          "live: BUYITEM_NOSPACE from Drive");
    Check(ui.Raw("WORD,QUIT_PARTY") == "ยุบ,ออกจากปาร์ตี้", "live: QUIT_PARTY from Drive");
    Check(ui.Raw("WORD,PARTY_MODAL_TITLE") == "ปาร์ตี้,ข้อมูลปาร์ตี้",
          "live: PARTY_MODAL_TITLE from Drive");
    Check(ui.Raw("WORD,LARGEMAP_WINDOW_STATIC") == "แผนที่", "live: LARGEMAP_WINDOW_STATIC");
    Check(ui.Raw("WORD,MAPMOVE_OKBUTTON") == "ย้าย", "live: MAPMOVE_OKBUTTON");
    Check(ui.Raw("SENTENSE,MAP_REQUIRE_FAIL").rfind("เงื่อนไขไม่ตรง", 0) == 0,
          "live: MAP_REQUIRE_FAIL", ui.Raw("SENTENSE,MAP_REQUIRE_FAIL"));
    Check(ui.Raw("SENTENSE,LEVELUP").rfind("ยินดีด้วย!", 0) == 0, "live: LEVELUP");
    Check(ui.Raw("SENTENSE,CHAT_PARTY_MODE").find("#") != std::string::npos,
          "live: CHAT_PARTY_MODE shows the party prefix", ui.Raw("SENTENSE,CHAT_PARTY_MODE"));
    Check(ui.Raw("SENTENSE,SELL_ITEM_CONFIRM") == "คุณต้องการขาย [%s] หรือไม่?",
          "live: SELL_ITEM_CONFIRM keeps the %s slot");

    // ทุกคีย์ที่ใช้ในร้านค้า/ปาร์ตี้ต้องเป็นข้อความไทยจริง ไม่ใช่คีย์หรือค่าว่าง
    const char* need[] = {"SENTENSE,BUY_ITEM_CONFIRM", "SENTENSE,SELL_ITEM_CONFIRM",
                          "SENTENSE,BUY_PILE_ITEM", "SENTENSE,PARTY_QUESTION",
                          "SENTENSE,CHAT_ERROR_NOPARTY", "SENTENSE,CHAT_PRIVATE_MODE",
                          "WORD,PARTY_LURE_FULL", "WORD,PARTY_LURE_REFUSE",
                          "WORD,PARTY_NAME_STATIC", "SENTENSE,EMREQUIRE_LEVEL"};
    for (const char* k : need) {
        const std::string& v = ui.Raw(k);
        bool thai = false;
        for (unsigned char c : v) {
            if (c >= 0xE0) { thai = true; break; }   // ไบต์นำของอักษรไทยใน UTF-8
        }
        Check(!v.empty() && thai, std::string("live: real Thai text for ") + k, v);
    }
}

int main(int argc, char** argv) {
    TestParse();
    if (argc > 2) {
        TestLive(argv[1], std::atoi(argv[2]));
    } else {
        std::printf("(skipping live server tests: pass host and port)\n");
    }
    std::printf("%s: %d checks, %d failed\n", g_fail == 0 ? "PASS" : "FAIL", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
