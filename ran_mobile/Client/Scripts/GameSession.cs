// ran_mobile/Client/Scripts/GameSession.cs
// เก็บสถานะตัวละครที่กำลังเล่นอยู่ฝั่ง Client (เป็นสำเนาจากเซิร์ฟเวอร์เท่านั้น ไม่ใช่แหล่งความจริง)
// ทุกการเปลี่ยนแปลงต้องผ่าน ApiClient แล้วรับค่าใหม่จากเซิร์ฟเวอร์ผ่าน SetView/Refresh
using System;
using System.Collections;
using UnityEngine;

public class GameSession : MonoBehaviour
{
    public static GameSession Instance { get; private set; }

    public int CharacterId { get; set; }
    public CharacterView Current { get; private set; }

    public event Action<CharacterView> Changed;

    void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);
    }

    public void SetView(CharacterView view)
    {
        Current = view;
        if (Changed != null)
            Changed(view);
    }

    public void Clear()
    {
        CharacterId = 0;
        Current = null;
    }

    /// <summary>ดึงสถานะล่าสุดจากเซิร์ฟเวอร์</summary>
    public void Refresh(Action<CharacterView> onOk = null, Action<ApiError> onErr = null)
    {
        StartCoroutine(ApiClient.Instance.Get<CharacterView>(
            "/api/characters/" + CharacterId,
            v =>
            {
                SetView(v);
                if (onOk != null) onOk(v);
            },
            e =>
            {
                if (onErr != null) onErr(e);
            }));
    }

    public IEnumerator RefreshCoroutine()
    {
        bool done = false;
        Refresh(_ => done = true, _ => done = true);
        while (!done) yield return null;
    }
}
