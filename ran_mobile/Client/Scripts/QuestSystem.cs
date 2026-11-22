// ran_mobile/Client/Scripts/QuestSystem.cs
// เควสต์: รับเควสต์และแสดงความคืบหน้าจากเซิร์ฟเวอร์ รางวัลจ่ายโดยเซิร์ฟเวอร์เมื่อครบเงื่อนไข
using System;
using System.Collections;
using UnityEngine;

public class QuestSystem : MonoBehaviour
{
    public void Accept(int questId)
    {
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/quests/" + questId + "/accept",
            null,
            ApiClient.NewIdempotencyKey(),
            v =>
            {
                GameSession.Instance.SetView(v);
                UiMessage.Show("รับเควสต์แล้ว");
            },
            e => UiMessage.Show(e.message)));
    }

    public QuestDto Find(int questId)
    {
        var view = GameSession.Instance != null ? GameSession.Instance.Current : null;
        if (view == null || view.quests == null) return null;
        foreach (var q in view.quests)
            if (q.quest_id == questId) return q;
        return null;
    }
}
