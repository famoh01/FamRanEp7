// AI EP7 - entry activity. It is a NativeActivity (the game runs in C++), with Java dialogs on top
// for the login, account and character screens. Every server call goes through mobile/net_client
// (native), which sends its requests via NetBridge.
//
// Debug-only test hook: launching with --es ci_user <name> --es ci_pass <pw> logs in without
// dialogs (creates the account and a character if missing). The smoke test uses it. Release builds
// ignore the extras.
package com.aiep7.app;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends NativeActivity {
    private static final String TAG = "AiEp7";

    static {
        // NativeActivity loads the library too. Loading it here as well lets the JVM bind the
        // native methods below.
        System.loadLibrary("aiep7");
    }

    // Native calls (net_bridge.cpp). They run one at a time on `net`, never on the UI thread.
    private native boolean nativeRegister(String user, String pass);
    private native boolean nativeLogin(String user, String pass);
    private native String nativeListCharacters();                 // raw JSON body, or null
    private native long nativeCreateCharacter(String name, String gender, String cls); // id, or -1
    private native void nativeSelectCharacter(long id, String name);
    private native String nativeLastError();

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService net = Executors.newSingleThreadExecutor();
    private AlertDialog current;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (BuildConfig.API_BASE_URL.isEmpty()) {
            showMessage("ยังไม่ได้ตั้งค่าเซิร์ฟเวอร์", "แอปนี้ถูกสร้างโดยไม่มีที่อยู่เซิร์ฟเวอร์ (ranApiBaseUrl)");
            return;
        }
        if (BuildConfig.DEBUG && getIntent() != null && getIntent().hasExtra("ci_user")) {
            autoLogin(getIntent().getStringExtra("ci_user"), getIntent().getStringExtra("ci_pass"));
        } else {
            showLogin("");
        }
    }

    @Override
    protected void onDestroy() {
        net.shutdownNow();
        super.onDestroy();
    }

    // ---- Debug-only automated login (no dialogs) ----

    private void autoLogin(String user, String pass) {
        net.execute(() -> {
            nativeRegister(user, pass);                       // "already exists" is fine here
            if (!nativeLogin(user, pass)) {
                Log.i(TAG, "ci login failed: " + nativeLastError());
                return;
            }
            Log.i(TAG, "net login ok");
            String json = nativeListCharacters();
            if (json == null) {
                Log.i(TAG, "ci characters failed: " + nativeLastError());
                return;
            }
            JSONArray list = parseCharacters(json);
            Log.i(TAG, "net characters count=" + list.length());
            long id;
            String name;
            if (list.length() > 0) {
                JSONObject first = list.optJSONObject(0);
                id = first.optLong("id");
                name = first.optString("name");
            } else {
                name = "CiHero";
                id = nativeCreateCharacter(name, "male", "fighter");
                if (id < 0) {
                    Log.i(TAG, "ci create failed: " + nativeLastError());
                    return;
                }
            }
            nativeSelectCharacter(id, name);
            Log.i(TAG, "character selected id=" + id + " name=" + name);
        });
    }

    // ---- Login screen ----

    private void showLogin(String error) {
        LinearLayout box = column();
        if (!error.isEmpty()) box.addView(text(error, 14));
        EditText user = field("ชื่อผู้ใช้ (3–20 ตัว: a–z, 0–9, _)", false);
        EditText pass = field("รหัสผ่าน", true);
        box.addView(user);
        box.addView(pass);

        current = new AlertDialog.Builder(this)
                .setTitle("เข้าสู่ระบบ")
                .setView(box)
                .setCancelable(false)
                .setPositiveButton("เข้าสู่ระบบ", null)
                .setNeutralButton("สมัครบัญชี", null)
                .create();
        current.setOnShowListener(d -> {
            current.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v ->
                    login(user.getText().toString().trim(), pass.getText().toString(), false));
            current.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v ->
                    login(user.getText().toString().trim(), pass.getText().toString(), true));
        });
        current.show();
    }

    private void login(String user, String pass, boolean register) {
        if (user.isEmpty() || pass.isEmpty()) {
            toast("กรอกชื่อผู้ใช้และรหัสผ่าน");
            return;
        }
        closeCurrent();
        net.execute(() -> {
            if (register && !nativeRegister(user, pass)) {
                String err = nativeLastError();
                Log.i(TAG, "register failed: " + err);
                ui.post(() -> showLogin("สมัครไม่สำเร็จ: " + err));
                return;
            }
            if (!nativeLogin(user, pass)) {
                String err = nativeLastError();
                Log.i(TAG, "login failed: " + err);
                ui.post(() -> showLogin("เข้าสู่ระบบไม่สำเร็จ: " + err));
                return;
            }
            Log.i(TAG, "net login ok");
            loadCharacters();
        });
    }

    // ---- Character screen (runs on `net`, posts UI work) ----

    private void loadCharacters() {
        String json = nativeListCharacters();
        if (json == null) {
            String err = nativeLastError();
            ui.post(() -> showLogin("โหลดตัวละครไม่สำเร็จ: " + err));
            return;
        }
        JSONArray list = parseCharacters(json);
        Log.i(TAG, "net characters count=" + list.length());
        List<JSONObject> rows = new ArrayList<>();
        for (int i = 0; i < list.length(); i++) rows.add(list.optJSONObject(i));
        ui.post(() -> showCharacters(rows));
    }

    private void showCharacters(List<JSONObject> rows) {
        closeCurrent();
        if (rows.isEmpty()) {
            showCreate("");
            return;
        }
        String[] names = new String[rows.size()];
        for (int i = 0; i < rows.size(); i++) {
            JSONObject c = rows.get(i);
            names[i] = c.optString("name") + "  (" + classLabel(c.optString("class")) + " Lv " + c.optInt("level") + ")";
        }
        current = new AlertDialog.Builder(this)
                .setTitle("เลือกตัวละคร")
                .setCancelable(false)
                .setItems(names, (d, which) -> {
                    JSONObject c = rows.get(which);
                    select(c.optLong("id"), c.optString("name"));
                })
                .setNeutralButton("สร้างตัวละคร", (d, w) -> showCreate(""))
                .create();
        current.show();
    }

    private void showCreate(String error) {
        LinearLayout box = column();
        if (!error.isEmpty()) box.addView(text(error, 14));
        EditText name = field("ชื่อตัวละคร (2–16 ตัว)", false);
        Spinner gender = spinner(new String[]{"ชาย", "หญิง"});
        Spinner cls = spinner(new String[]{"นักรบ", "จอมเวทย์", "นักธนู"});
        box.addView(name);
        box.addView(text("เพศ", 14));
        box.addView(gender);
        box.addView(text("อาชีพ", 14));
        box.addView(cls);

        closeCurrent();
        current = new AlertDialog.Builder(this)
                .setTitle("สร้างตัวละคร")
                .setView(box)
                .setCancelable(false)
                .setPositiveButton("สร้าง", null)
                .create();
        current.setOnShowListener(d -> current.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String n = name.getText().toString().trim();
            String g = gender.getSelectedItemPosition() == 0 ? "male" : "female";
            String c = new String[]{"fighter", "mage", "archer"}[cls.getSelectedItemPosition()];
            closeCurrent();
            net.execute(() -> {
                long id = nativeCreateCharacter(n, g, c);
                if (id < 0) {
                    String err = nativeLastError();
                    Log.i(TAG, "create failed: " + err);
                    ui.post(() -> showCreate("สร้างตัวละครไม่สำเร็จ: " + err));
                    return;
                }
                Log.i(TAG, "character created id=" + id);
                loadCharacters();
            });
        }));
        current.show();
    }

    private void select(long id, String name) {
        closeCurrent();
        net.execute(() -> {
            nativeSelectCharacter(id, name);
            Log.i(TAG, "character selected id=" + id + " name=" + name);
        });
    }

    // ---- helpers ----

    private static JSONArray parseCharacters(String json) {
        try {
            JSONArray list = new JSONObject(json).optJSONArray("characters");
            return list == null ? new JSONArray() : list;
        } catch (Exception e) {
            return new JSONArray();
        }
    }

    private static String classLabel(String cls) {
        switch (cls) {
            case "fighter": return "นักรบ";
            case "mage": return "จอมเวทย์";
            case "archer": return "นักธนู";
            default: return cls;
        }
    }

    private void closeCurrent() {
        if (current != null) {
            current.dismiss();
            current = null;
        }
    }

    private void showMessage(String title, String body) {
        current = new AlertDialog.Builder(this).setTitle(title).setMessage(body)
                .setCancelable(false).setPositiveButton("ปิด", null).create();
        current.show();
    }

    private void toast(String s) {
        ui.post(() -> Toast.makeText(this, s, Toast.LENGTH_SHORT).show());
    }

    private LinearLayout column() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);
        return box;
    }

    private TextView text(String s, int sp) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setGravity(Gravity.START);
        return t;
    }

    private EditText field(String hint, boolean password) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setSingleLine(true);
        e.setInputType(password
                ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD
                : InputType.TYPE_CLASS_TEXT);
        return e;
    }

    private Spinner spinner(String[] items) {
        Spinner s = new Spinner(this);
        s.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, items));
        return s;
    }
}
