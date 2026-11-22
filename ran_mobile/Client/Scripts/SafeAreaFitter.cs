// ran_mobile/Client/Scripts/SafeAreaFitter.cs
// ปรับ RectTransform ให้อยู่ในพื้นที่ปลอดภัย (ไม่ทับรอยบาก/ขอบจอ) สำหรับจอ 16:9, 18:9, 19.5:9, 20:9 และแท็บเล็ต
// ใส่ไว้ที่ Panel หลักของ HUD แล้วปุ่มทุกตัวจะเป็นลูกของ Panel นั้น
using UnityEngine;

[RequireComponent(typeof(RectTransform))]
public class SafeAreaFitter : MonoBehaviour
{
    RectTransform rt;
    Rect lastSafe;
    Vector2Int lastScreen;

    void Awake()
    {
        rt = GetComponent<RectTransform>();
        Apply();
    }

    void Update()
    {
        if (Screen.safeArea != lastSafe ||
            lastScreen.x != Screen.width || lastScreen.y != Screen.height)
        {
            Apply();
        }
    }

    void Apply()
    {
        Rect safe = Screen.safeArea;
        lastSafe = safe;
        lastScreen = new Vector2Int(Screen.width, Screen.height);

        Vector2 min = safe.position;
        Vector2 max = safe.position + safe.size;
        min.x /= Screen.width;
        min.y /= Screen.height;
        max.x /= Screen.width;
        max.y /= Screen.height;

        rt.anchorMin = min;
        rt.anchorMax = max;
        rt.offsetMin = Vector2.zero;
        rt.offsetMax = Vector2.zero;
    }
}
