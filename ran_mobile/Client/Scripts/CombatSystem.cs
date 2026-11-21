// ran_mobile/Client/Scripts/CombatSystem.cs
// ปุ่มโจมตี: ส่งคำขอไปที่เซิร์ฟเวอร์เท่านั้น ดาเมจและรางวัลมาจากเซิร์ฟเวอร์
// ตัวจำกัดการกดรัวฝั่ง Client เป็นแค่ UI (เซิร์ฟเวอร์เป็นผู้บังคับใช้จริง)
using System;
using System.Collections;
using UnityEngine;

public class CombatSystem : MonoBehaviour
{
    [Tooltip("กันการกดรัวบนเครื่อง (ไม่ใช่กติกาจริง)")]
    public float clientThrottleSeconds = 0.3f;

    public event Action<CombatResult> Resolved;

    bool busy;
    float nextAllowed;

    public bool CanAttack()
    {
        return !busy && Time.time >= nextAllowed && GameSession.Instance != null
               && GameSession.Instance.Current != null && !GameSession.Instance.Current.dead;
    }

    public void BasicAttack(int monsterId)
    {
        if (!CanAttack()) return;
        busy = true;
        nextAllowed = Time.time + clientThrottleSeconds;

        int cid = GameSession.Instance.CharacterId;
        string key = ApiClient.NewIdempotencyKey();
        StartCoroutine(ApiClient.Instance.Post<CombatResult>(
            "/api/characters/" + cid + "/combat/attack",
            new MonsterIdBody { monster_id = monsterId },
            key,
            r => OnResolved(r),
            e => { busy = false; UiMessage.Show(e.message); }));
    }

    /// <summary>ฟื้นคืนชีพ (เซิร์ฟเวอร์ตั้ง HP ใหม่เป็น 50% ของสูงสุด)</summary>
    public void Revive()
    {
        if (GameSession.Instance == null || GameSession.Instance.Current == null) return;
        if (!GameSession.Instance.Current.dead)
        {
            UiMessage.Show("ตัวละครยังไม่ตาย");
            return;
        }
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters/" + cid + "/revive",
            null,
            ApiClient.NewIdempotencyKey(),
            v =>
            {
                GameSession.Instance.SetView(v);
                UiMessage.Show("ฟื้นคืนชีพแล้ว");
            },
            e => UiMessage.Show(e.message)));
    }

    void OnResolved(CombatResult r)
    {
        busy = false;
        if (Resolved != null) Resolved(r);

        if (r.rewards != null && r.rewards.levels_gained > 0)
            UiMessage.Show("เลเวลอัป! Lv " + GameSession.Instance.Current.level);

        if (r.player != null && r.player.dead)
            UiMessage.Show("ตัวละครตาย กดฟื้นคืนชีพ");

        GameSession.Instance.Refresh();
    }
}
