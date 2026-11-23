// mobile/net_client/ui_strings.h
// ข้อความ UI ไทยที่ดึงมาจากไฟล์จริงใน Google Drive (ThaiPack data/gui/game*_th.txt)
//
// Server ส่ง JSON มาแบบ:
//   {"count":707,"source":"Google Drive RAN-EP7-ThaiPack data/gui/*_th.txt",
//    "strings":{"SENTENSE,BUYITEM_NOMONEY":"เงินไม่เพียงพอ! ...","WORD,QUIT_PARTY":"ยุบ,ออกจากปาร์ตี้"}}
// คลาสนี้แยกวิเคราะห์เป็น map ของคีย์ -> ข้อความ และรองรับ %s/%d เพื่อแทนค่าในประโยค
// (รูปแบบ %s/%d มาจากไฟล์จริงของเกม ไม่ได้คิดขึ้นเอง)
#ifndef MOBILE_NET_UI_STRINGS_H
#define MOBILE_NET_UI_STRINGS_H

#include <map>
#include <string>
#include <vector>

namespace mobile {
namespace net {

class UiStrings {
public:
    // แยก JSON ของ /api/ui/strings; คืน false เมื่อรูปแบบไม่ตรง
    bool Parse(const std::string& json);

    // จำนวนคีย์ทั้งหมดที่โหลดได้
    std::size_t Size() const { return strings_.size(); }

    // ข้อความดิบของคีย์ ("SENTENSE,X" หรือ "WORD,X"); "" เมื่อไม่มี
    const std::string& Raw(const std::string& key) const;

    // ข้อความแทนค่า: %s ใช้ args ตามลำดับ, %d เติมจาก numbers ตามลำดับ
    // คืน fallback เมื่อไม่พบคีย์ (หรือ fallback ว่าง = คืน "")
    std::string Format(const std::string& key, const std::vector<std::string>& args = {},
                       const std::vector<long long>& numbers = {},
                       const std::string& fallback = "") const;

    void Clear() { strings_.clear(); }

    // สำหรับเทสต์: ใส่ค่าตรง ๆ (ไม่ใช้ในแอป)
    void Set(const std::string& key, const std::string& text) { strings_[key] = text; }

private:
    std::map<std::string, std::string> strings_;
};

}  // namespace net
}  // namespace mobile

#endif  // MOBILE_NET_UI_STRINGS_H
