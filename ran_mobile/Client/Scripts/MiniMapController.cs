// ran_mobile/Client/Scripts/MiniMapController.cs
// มินิแมพแบบรายการเรดาร์: แสดงมอนสเตอร์ทั้งหมดพร้อม HP และเวลาเกิดใหม่จากเซิร์ฟเวอร์
// หมายเหตุ: เซิร์ฟเวอร์ยังไม่มีพิกัดบนแผนที่ จึงแสดงเป็นรายการ ไม่ได้วางจุดตามตำแหน่งจริง
using System.Collections;
using System.Text;
using UnityEngine;
using UnityEngine.UI;

public class MiniMapController : MonoBehaviour
{
    public Text listText;
    public float refreshSeconds = 5f;

    void OnEnable()
    {
        StartCoroutine(RefreshLoop());
    }

    IEnumerator RefreshLoop()
    {
        while (true)
        {
            bool done = false;
            StartCoroutine(ApiClient.Instance.Get<MonsterListResponse>(
                "/api/world/monsters",
                r => { Render(r); done = true; },
                e => { if (listText != null) listText.text = e.message; done = true; }));
            while (!done) yield return null;
            yield return new WaitForSeconds(refreshSeconds);
        }
    }

    void Render(MonsterListResponse r)
    {
        if (listText == null || r == null || r.monsters == null) return;
        var sb = new StringBuilder();
        foreach (var m in r.monsters)
        {
            if (m.dead)
                sb.AppendLine(m.name + "  เกิดใหม่ใน " + (m.respawn_in_ms / 1000) + " วิ");
            else
                sb.AppendLine(m.name + "  HP " + m.hp + "/" + m.max_hp);
        }
        listText.text = sb.ToString();
    }
}
