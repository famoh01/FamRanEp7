// ran_mobile/Client/Scripts/ItemData.cs
// ข้อมูลไอเทมสำหรับแสดงผลในเกม (ไอคอนและชื่อ) โดย itemID ต้องตรงกับ items.item_id ในฐานข้อมูล
// ค่าราคาและการเพิ่มเลือดจริงอยู่ที่เซิร์ฟเวอร์ ค่าในไฟล์นี้ใช้แสดงผลเท่านั้น
using UnityEngine;

[CreateAssetMenu(fileName = "NewItem", menuName = "RAN/Item")]
public class ItemData : ScriptableObject
{
    public int itemID;
    public string itemName;
    public Sprite icon;
    public ItemType type;
    public bool stackable = true;
    public int maxStack = 99;
}

public enum ItemType
{
    Consumable,
    Equipment,
    Quest,
    Material
}
