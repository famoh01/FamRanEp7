// ran_mobile/Client/Scripts/ApiModels.cs
// โมเดลข้อมูลที่ตรงกับ JSON ของเซิร์ฟเวอร์ (ran_mobile/Server/ranserver/api.py)
// JsonUtility ใช้ชื่อฟิลด์ตรงกับ JSON จึงใช้ @class และ @int สำหรับคำสงวนของ C#
using System;

[Serializable] public class ErrorDetail { public string code; public string message; }
[Serializable] public class ErrorEnvelope { public ErrorDetail error; }
[Serializable] public class OkResponse { public bool ok; }

// ---- บัญชี
[Serializable] public class RegisterResponse { public int account_id; public string username; }
[Serializable] public class LoginResponse { public string token; public int account_id; }

// ---- ตัวละคร
[Serializable] public class CharacterSummary
{
    public int id;
    public string name;
    public string gender;
    public string @class;
    public int level;
}

[Serializable] public class CharacterListResponse
{
    public CharacterSummary[] characters;
    public int limit;
}

[Serializable] public class StatsDto
{
    public int str;
    public int dex;
    public int @int;
    public int con;
}

[Serializable] public class InventoryItemDto
{
    public int slot;
    public int item_id;
    public string name;
    public string type;
    public int quantity;
    public bool stackable;
    public int max_stack;
    public int price;
}

[Serializable] public class SkillDto
{
    public int skill_id;
    public string name;
    public int damage;
    public int mana_cost;
    public int cooldown_ms;
    public float range;
    public int level;
    public int cooldown_remaining_ms;
}

[Serializable] public class QuestDto
{
    public int quest_id;
    public string name;
    public int progress;
    public int required_count;
    public string status;
}

[Serializable] public class CharacterView
{
    public int id;
    public string name;
    public string gender;
    public string @class;
    public int level;
    public int exp;
    public int exp_required;
    public int money;
    public int hp;
    public int max_hp;
    public int mp;
    public int max_mp;
    public int sp;
    public int max_sp;
    public StatsDto stats;
    public bool dead;
    public InventoryItemDto[] inventory;
    public SkillDto[] skills;
    public QuestDto[] quests;
}

// ---- มอนสเตอร์และการต่อสู้
[Serializable] public class MonsterDto
{
    public int monster_id;
    public string name;
    public int hp;
    public int max_hp;
    public bool dead;
    public int respawn_in_ms;
}

[Serializable] public class MonsterListResponse { public MonsterDto[] monsters; }

[Serializable] public class MonsterStateDto
{
    public int monster_id;
    public string name;
    public int hp;
    public int max_hp;
    public bool dead;
}

[Serializable] public class RewardsDto
{
    public int exp;
    public int money;
    public int levels_gained;
}

[Serializable] public class PlayerStateDto
{
    public int hp;
    public int max_hp;
    public bool dead;
}

[Serializable] public class SkillInfoDto
{
    public int skill_id;
    public string name;
}

[Serializable] public class CombatResult
{
    public bool hit;
    public bool crit;
    public int damage;
    public MonsterStateDto monster;
    public RewardsDto rewards;          // null เมื่อยังไม่ตาย
    public QuestDto[] quest_progress;
    public int retaliation_damage;
    public PlayerStateDto player;
    public SkillInfoDto skill;          // มีเฉพาะการใช้สกิล
}

// ---- ร้านค้า
[Serializable] public class ShopItemDto
{
    public int item_id;
    public string name;
    public string type;
    public int price;
    public bool stackable;
    public int max_stack;
}

[Serializable] public class ShopItemsResponse { public ShopItemDto[] items; }

// ---- คำขอที่ส่งไปเซิร์ฟเวอร์ (ฝั่ง Client ส่งเฉพาะ "เจตนา" ไม่ส่งค่าที่ต้องตัดสิน)
[Serializable] public class LoginBody { public string username; public string password; }
[Serializable] public class RegisterBody { public string username; public string password; }
[Serializable] public class CreateCharacterBody { public string name; public string gender; public string @class; }
[Serializable] public class MonsterIdBody { public int monster_id; }
[Serializable] public class SkillUseBody { public int skill_id; public int monster_id; }
[Serializable] public class BuyBody { public int item_id; public int quantity; }
[Serializable] public class SellBody { public int slot; public int quantity; }
[Serializable] public class SlotBody { public int slot; }
[Serializable] public class EmptyBody { }
