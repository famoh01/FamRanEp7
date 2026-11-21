// ran_mobile/Client/Scripts/SkillSystem.cs
// ปุ่มสกิล: ส่งคำขอให้เซิร์ฟเวอร์ตรวจ MP, คูลดาวน์ และคำนวณดาเมจ
// คูลดาวน์ที่แสดงบนปุ่มมาจากเซิร์ฟเวอร์ (cooldown_remaining_ms) ไม่ได้นับเอง
using System;
using System.Collections;
using UnityEngine;

public class SkillSystem : MonoBehaviour
{
    public event Action<CombatResult> Resolved;

    public bool IsReady(int skillId)
    {
        var s = FindSkill(skillId);
        return s != null && s.cooldown_remaining_ms <= 0
               && GameSession.Instance.Current != null
               && GameSession.Instance.Current.mp >= s.mana_cost;
    }

    public int CooldownRemainingMs(int skillId)
    {
        var s = FindSkill(skillId);
        return s == null ? 0 : s.cooldown_remaining_ms;
    }

    public void UseSkill(int skillId, int monsterId)
    {
        if (GameSession.Instance == null || GameSession.Instance.Current == null) return;
        int cid = GameSession.Instance.CharacterId;
        StartCoroutine(ApiClient.Instance.Post<CombatResult>(
            "/api/characters/" + cid + "/combat/skill",
            new SkillUseBody { skill_id = skillId, monster_id = monsterId },
            ApiClient.NewIdempotencyKey(),
            r =>
            {
                if (Resolved != null) Resolved(r);
                GameSession.Instance.Refresh();
            },
            e =>
            {
                UiMessage.Show(e.message);
                if (e.code == "skill_cooldown") GameSession.Instance.Refresh();
            }));
    }

    SkillDto FindSkill(int skillId)
    {
        var view = GameSession.Instance != null ? GameSession.Instance.Current : null;
        if (view == null || view.skills == null) return null;
        foreach (var s in view.skills)
            if (s.skill_id == skillId) return s;
        return null;
    }
}
