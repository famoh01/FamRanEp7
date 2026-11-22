// ran_mobile/Client/Scripts/ShopSystem.cs
// ร้านค้า: ซื้อและขายผ่านเซิร์ฟเวอร์ทั้งหมด (ราคา เงิน และที่ว่างในกระเป๋าตรวจที่เซิร์ฟเวอร์)
using System;
using System.Collections;
using UnityEngine;

public class ShopSystem : MonoBehaviour
{
    public event Action<ShopItemsResponse> CatalogLoaded;

    public void LoadCatalog()
    {
        StartCoroutine(ApiClient.Instance.Get<ShopItemsResponse>(
            "/api/shop/items",
            r => { if (CatalogLoaded != null) CatalogLoaded(r); },
            e => UiMessage.Show(e.message)));
    }

    public void Buy(int itemId, int quantity)
    {
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/shop/buy",
            new BuyBody { item_id = itemId, quantity = quantity },
            ApiClient.NewIdempotencyKey(),
            v =>
            {
                GameSession.Instance.SetView(v);
                UiMessage.Show("ซื้อสำเร็จ");
            },
            e => UiMessage.Show(e.message)));
    }

    public void Sell(int slot, int quantity)
    {
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/shop/sell",
            new SellBody { slot = slot, quantity = quantity },
            ApiClient.NewIdempotencyKey(),
            v =>
            {
                GameSession.Instance.SetView(v);
                UiMessage.Show("ขายสำเร็จ");
            },
            e => UiMessage.Show(e.message)));
    }
}
