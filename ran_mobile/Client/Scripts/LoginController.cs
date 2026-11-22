// ran_mobile/Client/Scripts/LoginController.cs
// หน้าล็อกอินและสมัครสมาชิก (ฉาก "Login")
// เมื่อสำเร็จจะเก็บ token ไว้ใน ApiClient และไปหน้าเลือกตัวละคร
using System.Collections;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class LoginController : MonoBehaviour
{
    public InputField usernameField;
    public InputField passwordField;
    public Button loginButton;
    public Button registerButton;
    public Text statusText;

    public string characterSelectScene = "CharacterSelect";

    bool busy;

    void Start()
    {
        if (passwordField != null)
            passwordField.contentType = InputField.ContentType.Password;
        if (loginButton != null) loginButton.onClick.AddListener(OnLoginClicked);
        if (registerButton != null) registerButton.onClick.AddListener(OnRegisterClicked);
    }

    public void OnLoginClicked()
    {
        if (busy || !Validate()) return;
        SetBusy(true, "กำลังเข้าสู่ระบบ...");
        StartCoroutine(ApiClient.Instance.Post<LoginResponse>(
            "/api/auth/login",
            new LoginBody { username = usernameField.text, password = passwordField.text },
            null,
            r =>
            {
                ApiClient.Instance.Token = r.token;
                SetBusy(false, "");
                SceneManager.LoadScene(characterSelectScene);
            },
            e => SetBusy(false, e.message)));
    }

    public void OnRegisterClicked()
    {
        if (busy || !Validate()) return;
        SetBusy(true, "กำลังสมัครสมาชิก...");
        StartCoroutine(ApiClient.Instance.Post<RegisterResponse>(
            "/api/auth/register",
            new RegisterBody { username = usernameField.text, password = passwordField.text },
            ApiClient.NewIdempotencyKey(),
            r =>
            {
                // สมัครแล้วให้ล็อกอินต่อทันที
                SetBusy(false, "สมัครสำเร็จ กำลังเข้าสู่ระบบ...");
                OnLoginClicked();
            },
            e => SetBusy(false, e.message)));
    }

    bool Validate()
    {
        if (usernameField == null || passwordField == null) return false;
        if (string.IsNullOrWhiteSpace(usernameField.text) || string.IsNullOrEmpty(passwordField.text))
        {
            SetStatus("กรอกชื่อผู้ใช้และรหัสผ่าน");
            return false;
        }
        return true;
    }

    void SetBusy(bool value, string message)
    {
        busy = value;
        if (loginButton != null) loginButton.interactable = !value;
        if (registerButton != null) registerButton.interactable = !value;
        SetStatus(message);
    }

    void SetStatus(string message)
    {
        if (statusText != null) statusText.text = message;
    }
}
