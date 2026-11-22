// ran_mobile/Client/Scripts/UiMessage.cs
// แสดงข้อความแจ้งเตือนสั้น ๆ บนหน้าจอ (เช่น "เงินไม่พอ", "กระเป๋าเต็ม")
using UnityEngine;
using UnityEngine.UI;

public class UiMessage : MonoBehaviour
{
    public static UiMessage Instance { get; private set; }

    public Text text;
    public float seconds = 3f;

    void Awake()
    {
        Instance = this;
        if (text != null) text.enabled = false;
    }

    public static void Show(string message)
    {
        if (Instance == null || Instance.text == null)
        {
            Debug.Log("[UI] " + message);
            return;
        }
        Instance.text.text = message;
        Instance.text.enabled = true;
        Instance.CancelInvoke(nameof(Hide));
        Instance.Invoke(nameof(Hide), Instance.seconds);
    }

    void Hide()
    {
        if (text != null) text.enabled = false;
    }
}
