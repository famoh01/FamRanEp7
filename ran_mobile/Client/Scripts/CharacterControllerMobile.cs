// ran_mobile/Client/Scripts/CharacterControllerMobile.cs
// การเคลื่อนที่ของตัวละครตามจอยสติ๊ก (ทิศทางอิงกล้อง)
// หมายเหตุ: ตำแหน่งยังไม่ได้ซิงค์กับเซิร์ฟเวอร์ (เซิร์ฟเวอร์ยังไม่มีระบบแผนที่)
using UnityEngine;

public class CharacterControllerMobile : MonoBehaviour
{
    public VirtualJoystick joystick;
    public float moveSpeed = 4f;
    public Camera mainCamera;

    void Start()
    {
        if (mainCamera == null)
            mainCamera = Camera.main;
    }

    void Update()
    {
        MoveCharacter();
    }

    void MoveCharacter()
    {
        if (joystick == null || mainCamera == null)
            return;

        Vector2 input = joystick.InputVector;
        if (input.sqrMagnitude < 0.0001f)
            return;

        Vector3 forward = mainCamera.transform.forward;
        Vector3 right = mainCamera.transform.right;
        forward.y = 0f;
        right.y = 0f;
        forward.Normalize();
        right.Normalize();

        Vector3 movement = forward * input.y + right * input.x;
        transform.position += movement * moveSpeed * Time.deltaTime;

        Quaternion target = Quaternion.LookRotation(movement);
        transform.rotation = Quaternion.Slerp(transform.rotation, target, 10f * Time.deltaTime);
    }
}
