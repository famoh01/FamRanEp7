// ran_mobile/Client/Scripts/CharacterSelectController.cs
// หน้าเลือก/สร้าง/ลบตัวละคร (ฉาก "CharacterSelect")
// ชื่อ เพศ และสายอาชีพ ถูกตรวจซ้ำที่เซิร์ฟเวอร์ ฝั่ง Client ตรวจเฉพาะรูปแบบเบื้องต้น
using System.Collections;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class CharacterSelectController : MonoBehaviour
{
    public Text listText;
    public InputField nameField;
    public Dropdown genderDropdown;     // ตัวเลือก: male, female
    public Dropdown classDropdown;      // ตัวเลือก: fighter, mage, archer
    public Button createButton;
    public Button deleteButton;
    public Button enterButton;
    public InputField selectIdField;    // ใส่ id ของตัวละครที่ต้องการเข้าเล่น/ลบ (ตัวอย่าง UI แบบง่าย)
    public Text statusText;

    public string gameScene = "Game";
    public string loginScene = "Login";

    static readonly string[] Genders = { "male", "female" };
    static readonly string[] Classes = { "fighter", "mage", "archer" };

    void Start()
    {
        if (createButton != null) createButton.onClick.AddListener(OnCreate);
        if (deleteButton != null) deleteButton.onClick.AddListener(OnDelete);
        if (enterButton != null) enterButton.onClick.AddListener(OnEnter);
        if (ApiClient.Instance == null || string.IsNullOrEmpty(ApiClient.Instance.Token))
        {
            SceneManager.LoadScene(loginScene);
            return;
        }
        Reload();
    }

    public void Reload()
    {
        StartCoroutine(ApiClient.Instance.Get<CharacterListResponse>(
            "/api/characters",
            r =>
            {
                var sb = new System.Text.StringBuilder();
                foreach (var c in r.characters)
                    sb.AppendLine("#" + c.id + "  " + c.name + "  Lv " + c.level + "  (" + c.@class + ")");
                if (r.characters.Length == 0) sb.AppendLine("ยังไม่มีตัวละคร");
                sb.AppendLine("สร้างได้สูงสุด " + r.limit + " ตัว");
                if (listText != null) listText.text = sb.ToString();
            },
            e => SetStatus(e.message)));
    }

    public void OnCreate()
    {
        if (nameField == null || string.IsNullOrWhiteSpace(nameField.text))
        {
            SetStatus("กรอกชื่อตัวละคร");
            return;
        }
        var body = new CreateCharacterBody
        {
            name = nameField.text.Trim(),
            gender = Genders[genderDropdown != null ? genderDropdown.value : 0],
            @class = Classes[classDropdown != null ? classDropdown.value : 0],
        };
        StartCoroutine(ApiClient.Instance.Post<CharacterView>(
            "/api/characters",
            body,
            ApiClient.NewIdempotencyKey(),
            v =>
            {
                SetStatus("สร้างตัวละคร " + v.name + " แล้ว");
                Reload();
            },
            e => SetStatus(e.message)));
    }

    public void OnDelete()
    {
        int id;
        if (!TryReadId(out id)) return;
        StartCoroutine(ApiClient.Instance.Delete<OkResponse>(
            "/api/characters/" + id,
            r => { SetStatus("ลบตัวละครแล้ว"); Reload(); },
            e => SetStatus(e.message)));
    }

    public void OnEnter()
    {
        int id;
        if (!TryReadId(out id)) return;
        GameSession.Instance.CharacterId = id;
        GameSession.Instance.Refresh(
            v => SceneManager.LoadScene(gameScene),
            e => SetStatus(e.message));
    }

    bool TryReadId(out int id)
    {
        if (selectIdField == null || !int.TryParse(selectIdField.text, out id) || id <= 0)
        {
            SetStatus("ใส่ id ตัวละครให้ถูกต้อง");
            id = 0;
            return false;
        }
        return true;
    }

    void SetStatus(string message)
    {
        if (statusText != null) statusText.text = message;
    }
}
