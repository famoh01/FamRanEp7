// ran_mobile/Client/Scripts/VirtualJoystick.cs
// จอยสติ๊กเสมือนด้านซ้าย รับทัชแล้วคืนค่าทิศทางในช่วง -1..1
// เพิ่ม dead zone 0.15 เพื่อกันการขยับเมื่อนิ้วแตะเบา ๆ
using UnityEngine;
using UnityEngine.EventSystems;

public class VirtualJoystick : MonoBehaviour, IPointerDownHandler, IDragHandler, IPointerUpHandler
{
    public RectTransform joystick;
    public RectTransform handle;
    public float radius = 100f;
    [Range(0f, 0.5f)] public float deadZone = 0.15f;

    public Vector2 InputVector { get; private set; }

    public void OnPointerDown(PointerEventData eventData)
    {
        OnDrag(eventData);
    }

    public void OnDrag(PointerEventData eventData)
    {
        if (joystick == null || handle == null) return;

        Vector2 localPoint;
        RectTransformUtility.ScreenPointToLocalPointInRectangle(
            joystick, eventData.position, eventData.pressEventCamera, out localPoint);

        Vector2 v = Vector2.ClampMagnitude(localPoint / radius, 1f);
        InputVector = v.magnitude < deadZone ? Vector2.zero : v;
        handle.anchoredPosition = InputVector * radius;
    }

    public void OnPointerUp(PointerEventData eventData)
    {
        InputVector = Vector2.zero;
        if (handle != null) handle.anchoredPosition = Vector2.zero;
    }
}
