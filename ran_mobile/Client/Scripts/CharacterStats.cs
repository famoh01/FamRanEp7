// ran_mobile/Client/Scripts/CharacterStats.cs
// สำเนาค่าสถานะตัวละครสำหรับแสดงผล ค่าทั้งหมดรับจากเซิร์ฟเวอร์ผ่าน ApplyView()
// Client ไม่มีการลดเลือดหรือเพิ่ม EXP เอง (กติกาอยู่ที่เซิร์ฟเวอร์)
using UnityEngine;

public class CharacterStats : MonoBehaviour
{
    public int level = 1;
    public int exp;
    public int expRequired = 100;
    public int money;

    public int maxHP = 100;
    public int hp = 100;
    public int maxMP = 50;
    public int mp = 50;
    public int maxSP = 100;
    public int sp = 100;

    public int STR;
    public int DEX;
    public int INT;
    public int CON;

    public bool dead;

    public void ApplyView(CharacterView v)
    {
        if (v == null) return;
        level = v.level;
        exp = v.exp;
        expRequired = v.exp_required;
        money = v.money;
        maxHP = v.max_hp; hp = v.hp;
        maxMP = v.max_mp; mp = v.mp;
        maxSP = v.max_sp; sp = v.sp;
        if (v.stats != null)
        {
            STR = v.stats.str;
            DEX = v.stats.dex;
            INT = v.stats.@int;
            CON = v.stats.con;
        }
        dead = v.dead;
    }

    public bool IsDead()
    {
        return hp <= 0;
    }
}
