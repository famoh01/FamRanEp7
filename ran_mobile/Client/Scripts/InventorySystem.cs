// ran_mobile/Client/Scripts/InventorySystem.cs
// แสดงกระเป๋าและส่งคำสั่งใช้ไอเทม
// Client ไม่มี AddItem เองแล้ว: ไอเทมเข้าได้ทางการซื้อหรือรางวัลจากเซิร์ฟเวอร์เท่านั้น
using System;
using System.Collections;
using UnityEngine;

public class InventorySystem : MonoBehaviour
{
    public const int SlotCount = 30;

    public event Action Changed;

    void OnEnable()
    {
        if (GameSession.Instance != null) GameSession.Instance.Changed += OnSession;
    }

    void OnDisable()
    {
        if (GameSession.Instance != null) GameSession.Instance.Changed -= OnSession;
    }

    void OnSession(CharacterView v)
    {
        if (Changed != null) Changed();
    }

    public InventoryItemDto GetSlot(int slot)
    {
        var view = GameSession.Instance != null ? GameSession.Instance.Current : null;
        if (view == null || view.inventory == null) return null;
        foreach (var it in view.inventory)
            if (it.slot == slot) return it;
        return null;
    }

    public int CountItem(int itemId)
    {
        var view = GameSession.Instance != null ? GameSession.Instance.Current : null;
        if (view == null || view.inventory == null) return 0;
        int total = 0;
        foreach (var it in view.inventory)
            if (it.item_id == itemId) total += it.quantity;
        return total;
    }

    public void UseSlot(int slot)
    {
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/inventory/use",
            new SlotBody { slot = slot },
            ApiClient.NewIdempotencyKey(),
            v => GameSession.Instance.SetView(v),
            e => UiMessage.Show(e.message)));
    }

    // สวมอาวุธ/เกราะจากช่องกระเป๋า เซิร์ฟเวอร์ตรวจว่าเป็นอุปกรณ์จริงและย้ายของเดิมกลับเข้ากระเป๋า
    public void EquipSlot(int slot)
    {
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/equip",
            new SlotBody { slot = slot },
            ApiClient.NewIdempotencyKey(),
            v => GameSession.Instance.SetView(v),
            e => UiMessage.Show(e.message)));
    }

    // ถอดอุปกรณ์ออก ค่า equipSlot ต้องเป็น "weapon" หรือ "armor"
    public void UnequipSlot(string equipSlot)
    {
        if (equipSlot != "weapon" && equipSlot != "armor")
        {
            UiMessage.Show("ช่องอุปกรณ์ไม่ถูกต้อง");
            return;
        }
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/unequip",
            new UnequipBody { equip_slot = equipSlot },
            ApiClient.NewIdempotencyKey(),
            v => GameSession.Instance.SetView(v),
            e => UiMessage.Show(e.message)));
    }

    // ค่าโบนัสรวมที่แสดงผลเท่านั้น การคำนวณจริงอยู่ที่เซิร์ฟเวอร์
    public int EquippedAttackBonus()
    {
        var view = GameSession.Instance != null ? GameSession.Instance.Current : null;
        if (view == null || view.equipment == null) return 0;
        int total = 0;
        if (view.equipment.weapon != null) total += view.equipment.weapon.attack_bonus;
        if (view.equipment.armor != null) total += view.equipment.armor.attack_bonus;
        return total;
    }
}
