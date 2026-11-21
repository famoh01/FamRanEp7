// ran_mobile/Client/Scripts/ApiClient.cs
// ตัวเรียก HTTP ไปยังเซิร์ฟเวอร์ RAN MOBILE ด้วย UnityWebRequest
// - แนบ Bearer token ทุกคำขอหลังล็อกอิน
// - คำขอที่เปลี่ยนข้อมูลส่ง Idempotency-Key ซ้ำเมื่อ retry (เซิร์ฟเวอร์จะไม่ทำซ้ำ)
// - แปลง error ของเซิร์ฟเวอร์เป็น ApiError เพื่อแสดงผลให้ผู้เล่น
using System;
using System.Collections;
using System.Text;
using UnityEngine;
using UnityEngine.Networking;

[Serializable]
public class ApiError
{
    public int status;
    public string code;
    public string message;
    public bool isNetwork;
}

public class ApiClient : MonoBehaviour
{
    public static ApiClient Instance { get; private set; }

    [Tooltip("Android emulator ใช้ http://10.0.2.2:8080 / มือถือจริงใช้ IP ของเครื่องที่รันเซิร์ฟเวอร์ เช่น http://192.168.1.20:8080")]
    public string baseUrl = "http://10.0.2.2:8080";

    [Range(0, 3)] public int networkRetries = 2;
    public int timeoutSeconds = 10;

    public string Token { get; set; }

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

    public static string NewIdempotencyKey()
    {
        // 32 ตัวอักษร hex ตรงตามกติกาของเซิร์ฟเวอร์ (8-64 ตัว)
        return Guid.NewGuid().ToString("N");
    }

    public IEnumerator Get<T>(string path, Action<T> onOk, Action<ApiError> onErr)
    {
        return Send<T>("GET", path, null, null, onOk, onErr);
    }

    public IEnumerator Post<T>(string path, object body, string idemKey, Action<T> onOk, Action<ApiError> onErr)
    {
        return Send<T>("POST", path, body, idemKey, onOk, onErr);
    }

    public IEnumerator Delete<T>(string path, Action<T> onOk, Action<ApiError> onErr)
    {
        return Send<T>("DELETE", path, null, null, onOk, onErr);
    }

    public IEnumerator Send<T>(string method, string path, object body, string idemKey,
                               Action<T> onOk, Action<ApiError> onErr)
    {
        string json = body == null ? "" : JsonUtility.ToJson(body);
        if (method != "GET" && string.IsNullOrEmpty(json))
            json = "{}";

        int attempt = 0;
        while (true)
        {
            using (UnityWebRequest req = BuildRequest(method, path, json, idemKey))
            {
                yield return req.SendWebRequest();

                if (req.result == UnityWebRequest.Result.ConnectionError)
                {
                    if (attempt < networkRetries)
                    {
                        attempt++;
                        // ใช้ Idempotency-Key เดิม ทำให้ retry ปลอดภัยต่อการซื้อหรือการโจมตี
                        yield return new WaitForSeconds(0.5f * attempt);
                        continue;
                    }
                    if (onErr != null)
                        onErr(new ApiError { status = 0, code = "network", message = "เชื่อมต่อเซิร์ฟเวอร์ไม่ได้", isNetwork = true });
                    yield break;
                }

                string text = req.downloadHandler != null ? req.downloadHandler.text : "";
                int status = (int)req.responseCode;

                if (status >= 200 && status < 300)
                {
                    T value;
                    try
                    {
                        value = JsonUtility.FromJson<T>(text);
                    }
                    catch (Exception e)
                    {
                        Debug.LogError("[ApiClient] parse failed for " + path + ": " + e.Message);
                        if (onErr != null)
                            onErr(new ApiError { status = status, code = "bad_response", message = "ข้อมูลตอบกลับไม่ถูกต้อง" });
                        yield break;
                    }
                    if (onOk != null)
                        onOk(value);
                }
                else
                {
                    if (onErr != null)
                        onErr(ParseError(status, text));
                }
                yield break;
            }
        }
    }

    UnityWebRequest BuildRequest(string method, string path, string json, string idemKey)
    {
        string url = baseUrl.TrimEnd('/') + path;
        var req = new UnityWebRequest(url, method);
        if (!string.IsNullOrEmpty(json) && method != "GET")
        {
            req.uploadHandler = new UploadHandlerRaw(Encoding.UTF8.GetBytes(json));
            req.SetRequestHeader("Content-Type", "application/json; charset=utf-8");
        }
        req.downloadHandler = new DownloadHandlerBuffer();
        req.timeout = timeoutSeconds;
        if (!string.IsNullOrEmpty(Token))
            req.SetRequestHeader("Authorization", "Bearer " + Token);
        if (!string.IsNullOrEmpty(idemKey))
            req.SetRequestHeader("Idempotency-Key", idemKey);
        return req;
    }

    static ApiError ParseError(int status, string text)
    {
        try
        {
            ErrorEnvelope env = JsonUtility.FromJson<ErrorEnvelope>(text);
            if (env != null && env.error != null)
                return new ApiError { status = status, code = env.error.code, message = env.error.message };
        }
        catch (Exception)
        {
            // ตอบกลับไม่ใช่ JSON ใช้ข้อความทั่วไปแทน
        }
        return new ApiError { status = status, code = "http_" + status, message = "เกิดข้อผิดพลาด (" + status + ")" };
    }
}
