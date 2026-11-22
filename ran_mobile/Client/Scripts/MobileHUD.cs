// ran_mobile/Client/Scripts/MobileHUD.cs
// แสดง HUD ด้านบน: ชื่อ เลเวล HP MP SP EXP และเงิน
// อ่านค่าจาก GameSession (ซึ่งรับค่าจากเซิร์ฟเวอร์) ไม่คำนวณเอง
using UnityEngine;
using UnityEngine.UI;

public class MobileHUD : MonoBehaviour
{
    public CharacterStats stats;

    public Slider hpBar;
    public Slider mpBar;
    public Slider spBar;
    public Slider expBar;

    public Text nameText;
    public Text levelText;
    public Text hpText;
    public Text mpText;
    public Text spText;
    public Text moneyText;

    void OnEnable()
    {
        if (GameSession.Instance != null)
            GameSession.Instance.Changed += OnSessionChanged;
    }

    void OnDisable()
    {
        if (GameSession.Instance != null)
            GameSession.Instance.Changed -= OnSessionChanged;
    }

    void Start()
    {
        if (GameSession.Instance != null && GameSession.Instance.Current != null)
            OnSessionChanged(GameSession.Instance.Current);
    }

    void OnSessionChanged(CharacterView view)
    {
        if (stats == null) return;
        stats.ApplyView(view);
        if (nameText != null) nameText.text = view.name;
        Refresh();
    }

    void Refresh()
    {
        SetBar(hpBar, stats.hp, stats.maxHP);
        SetBar(mpBar, stats.mp, stats.maxMP);
        SetBar(spBar, stats.sp, stats.maxSP);
        SetBar(expBar, stats.exp, Mathf.Max(1, stats.expRequired));

        if (levelText != null) levelText.text = "Lv " + stats.level;
        if (hpText != null) hpText.text = stats.hp + "/" + stats.maxHP;
        if (mpText != null) mpText.text = stats.mp + "/" + stats.maxMP;
        if (spText != null) spText.text = stats.sp + "/" + stats.maxSP;
        if (moneyText != null) moneyText.text = stats.money.ToString("N0");
    }

    static void SetBar(Slider bar, int value, int max)
    {
        if (bar == null) return;
        bar.maxValue = Mathf.Max(1, max);
        bar.value = Mathf.Clamp(value, 0, Mathf.Max(1, max));
    }
}
