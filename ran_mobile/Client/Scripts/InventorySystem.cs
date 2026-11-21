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
}
