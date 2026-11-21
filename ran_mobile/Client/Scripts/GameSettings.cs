// ran_mobile/Client/Scripts/GameSettings.cs
// การตั้งค่าของผู้เล่น เก็บในเครื่องด้วย PlayerPrefs (ไม่ส่งไปเซิร์ฟเวอร์)
using UnityEngine;

public class GameSettings : MonoBehaviour
{
    public bool showNPCName = true;
    public bool showItemName = true;
    public bool showDamageNumber = true;
    public bool postProcessing = true;
    public float uiScale = 1f;

    void Awake()
    {
        Load();
    }

    public void SetUIScale(float value)
    {
        uiScale = Mathf.Clamp(value, 0.5f, 2f);
        PlayerPrefs.SetFloat("UI_SCALE", uiScale);
    }

    public void SetPostProcessing(bool enabled)
    {
        postProcessing = enabled;
        PlayerPrefs.SetInt("POST_PROCESS", enabled ? 1 : 0);
    }

    public void SetNPCName(bool enabled)
    {
        showNPCName = enabled;
        PlayerPrefs.SetInt("NPC_NAME", enabled ? 1 : 0);
    }

    public void SetItemName(bool enabled)
    {
        showItemName = enabled;
        PlayerPrefs.SetInt("ITEM_NAME", enabled ? 1 : 0);
    }

    public void SetDamageNumber(bool enabled)
    {
        showDamageNumber = enabled;
        PlayerPrefs.SetInt("DAMAGE_NUMBER", enabled ? 1 : 0);
    }

    public void ResetDefaults()
    {
        SetUIScale(1f);
        SetPostProcessing(true);
        SetNPCName(true);
        SetItemName(true);
        SetDamageNumber(true);
        Save();
    }

    public void Save()
    {
        PlayerPrefs.Save();
    }

    public void Load()
    {
        uiScale = PlayerPrefs.GetFloat("UI_SCALE", 1f);
        showNPCName = PlayerPrefs.GetInt("NPC_NAME", 1) == 1;
        showItemName = PlayerPrefs.GetInt("ITEM_NAME", 1) == 1;
        showDamageNumber = PlayerPrefs.GetInt("DAMAGE_NUMBER", 1) == 1;
        postProcessing = PlayerPrefs.GetInt("POST_PROCESS", 1) == 1;
    }
}
