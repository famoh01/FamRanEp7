// AI EP7 - entry activity. A NativeActivity (the game runs in C++) with Java dialogs on top for the
// account and character screens. Every server call goes through mobile/net_client (native), which
// sends its requests via NetBridge.
//
// Screens:  LOGIN -> CHARACTERS (or CREATE when the account has no character) -> GAME (Enter World)
//           GAME --Back--> menu: back to CHARACTERS, or LOGOUT -> LOGIN
//
// Debug-only hooks (ignored in release builds):
//   --es ci_user <name> --es ci_pass <pw>  log in without dialogs (smoke test)
//   --ez ci_ui_test true                   run CiUiTest: drives the real dialogs and logs CI_TEST lines
package com.aiep7.app;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
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
import java.util.regex.Pattern;

public class MainActivity extends NativeActivity {
    static final String TAG = "AiEp7";

    // Same rules as the server (ran_mobile/Server/ranserver/security.py); the server still checks them.
    static final Pattern USERNAME_RE = Pattern.compile("^[A-Za-z0-9_]{3,20}$");
    static final Pattern CHARACTER_NAME_RE = Pattern.compile("^[A-Za-z0-9_\\u0E00-\\u0E7F]{2,16}$");
    static final int PASSWORD_MIN = 8, PASSWORD_MAX = 128;
    static final int MAX_CHARACTERS = 3;  // game.MAX_CHARACTERS_PER_ACCOUNT on the server
    static final String[] CLASS_CODES = {"fighter", "mage", "archer"};
    static final String[] CLASS_LABELS = {"นักรบ", "จอมเวทย์", "นักธนู"};

    static {
        // NativeActivity loads the library too. Loading it here lets the JVM bind the native methods.
        System.loadLibrary("aiep7");
    }

    enum Screen { LOGIN, CHARACTERS, CREATE, GAME }

    // Native calls (net_bridge.cpp). They run one at a time on `net`, never on the UI thread.
    native boolean nativeRegister(String user, String pass);
    native boolean nativeLogin(String user, String pass);
    native boolean nativeLogout();
    native String nativeListCharacters();                           // raw JSON body, or null
    native long nativeCreateCharacter(String name, String gender, String cls); // id, or -1
    native boolean nativeEnterWorld(long id);
    native boolean nativeDeleteCharacter(long id);
    native String nativeLastError();

    final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService net = Executors.newSingleThreadExecutor();

    // UI state: touched on the UI thread only (written by posted runnables).
    volatile Screen screen = Screen.LOGIN;
    volatile String lastMessage = "";
    volatile int characterCount = -1;
    volatile long enteredId = 0;
    List<JSONObject> rows = new ArrayList<>();
    AlertDialog current;
    EditText loginUser, loginPass, createName;
    Spinner createGender, createClass;
    private TextView loginError, createError;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (BuildConfig.SERVER_URL.isEmpty()) {
            showMessage("ยังไม่ได้ตั้งค่าเซิร์ฟเวอร์", "แอปนี้ถูกสร้างโดยไม่มีที่อยู่เซิร์ฟเวอร์ (ranServerUrl)");
            return;
        }
        Intent in = getIntent();
        if (BuildConfig.DEBUG && in != null && in.hasExtra("ci_user")) {
            autoLogin(in.getStringExtra("ci_user"), in.getStringExtra("ci_pass"));
            return;
        }
        showLogin("");
        if (BuildConfig.DEBUG && in != null && in.getBooleanExtra("ci_ui_test", false)) {
            new CiUiTest(this).start();
        }
    }

    @Override
    protected void onDestroy() {
        net.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (screen == Screen.GAME) showGameMenu();
        else super.onBackPressed();
    }

    // ---- Debug-only automated login (no dialogs), used by the smoke test ----

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
            List<JSONObject> list = parseCharacters(json);
            Log.i(TAG, "net characters count=" + list.size());
            long id;
            String name;
            if (!list.isEmpty()) {
                id = list.get(0).optLong("id");
                name = list.get(0).optString("name");
            } else {
                name = "CiHero";
                id = nativeCreateCharacter(name, "male", "fighter");
                if (id < 0) {
                    Log.i(TAG, "ci create failed: " + nativeLastError());
                    return;
                }
            }
            if (!nativeEnterWorld(id)) {
                Log.i(TAG, "ci enter world failed: " + nativeLastError());
                return;
            }
            ui.post(() -> {
                screen = Screen.GAME;
                enteredId = id;
            });
            Log.i(TAG, "character selected id=" + id + " name=" + name);
        });
    }

    // ---- Login screen ----

    void showLogin(String msg) {
        closeCurrent();
        screen = Screen.LOGIN;
        lastMessage = msg;
        LinearLayout box = column();
        loginError = text(msg, 14);
        box.addView(loginError);
        loginUser = field("ชื่อผู้ใช้ (3–20 ตัว: a–z, 0–9, _)", false);
        loginPass = field("รหัสผ่าน (8–128 ตัว)", true);
        box.addView(loginUser);
        box.addView(loginPass);

        current = new AlertDialog.Builder(this)
                .setTitle("เข้าสู่ระบบ")
                .setView(box)
                .setCancelable(false)
                .setPositiveButton("เข้าสู่ระบบ", null)
                .setNeutralButton("สมัครบัญชี", null)
                .create();
        current.setOnShowListener(d -> {
            current.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> submitLogin(false));
            current.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> submitLogin(true));
        });
        current.show();
    }

    void submitLogin(boolean register) {
        final String user = loginUser.getText().toString().trim();
        final String pass = loginPass.getText().toString();
        String bad = validateCredentials(user, pass);
        if (bad != null) {
            setLoginMessage(bad);
            return;
        }
        setLoginMessage("กำลังเชื่อมต่อเซิร์ฟเวอร์...");
        net.execute(() -> {
            if (register && !nativeRegister(user, pass)) {
                String err = describe(nativeLastError());
                ui.post(() -> setLoginMessage("สมัครไม่สำเร็จ: " + err));
                return;
            }
            if (!nativeLogin(user, pass)) {
                String err = describe(nativeLastError());
                ui.post(() -> setLoginMessage("เข้าสู่ระบบไม่สำเร็จ: " + err));
                return;
            }
            Log.i(TAG, "net login ok");
            loadCharacters();
        });
    }

    private void setLoginMessage(String msg) {
        lastMessage = msg;
        if (loginError != null) loginError.setText(msg);
    }

    static String validateCredentials(String user, String pass) {
        if (user.isEmpty() || pass.isEmpty()) return "กรอกชื่อผู้ใช้และรหัสผ่าน";
        if (!USERNAME_RE.matcher(user).matches()) return "ชื่อผู้ใช้ต้องมี 3–20 ตัว (a–z, 0–9, _)";
        if (pass.length() < PASSWORD_MIN || pass.length() > PASSWORD_MAX) return "รหัสผ่านต้องมี 8–128 ตัว";
        return null;
    }

    // ---- Character list (runs on `net`, posts the UI work) ----

    void loadCharacters() {
        String json = nativeListCharacters();
        if (json == null) {
            String err = nativeLastError();
            ui.post(() -> onNetFailure(err));
            return;
        }
        List<JSONObject> list = parseCharacters(json);
        Log.i(TAG, "net characters count=" + list.size());
        ui.post(() -> showCharacters(list));
    }

    private void onNetFailure(String err) {
        if (err.contains("unauthorized")) {
            showLogin(describe(err));           // the session is gone: log in again
        } else if (screen == Screen.LOGIN) {
            setLoginMessage(describe(err));
        } else {
            toast(describe(err));
        }
    }

    void showCharacters(List<JSONObject> list) {
        closeCurrent();
        rows = list;
        characterCount = list.size();
        if (list.isEmpty()) {
            showCreate("ยังไม่มีตัวละคร กรุณาสร้างตัวละคร");
            return;
        }
        screen = Screen.CHARACTERS;
        String[] items = new String[list.size()];
        for (int i = 0; i < list.size(); i++) {
            JSONObject c = list.get(i);
            items[i] = c.optString("name") + "  (" + classLabel(c.optString("class")) + " Lv " + c.optInt("level") + ")";
        }
        current = new AlertDialog.Builder(this)
                .setTitle("เลือกตัวละคร")
                .setCancelable(false)
                .setItems(items, (d, which) -> showCharacterAction(which))
                .setPositiveButton("สร้างตัวละคร", null)
                .setNegativeButton("ออกจากระบบ", (d, w) -> logout())
                .create();
        current.setOnShowListener(d -> current.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            if (rows.size() >= MAX_CHARACTERS) toast("ครบ " + MAX_CHARACTERS + " ตัวแล้ว");
            else showCreate("");
        }));
        current.show();
    }

    void showCharacterAction(int index) {
        final JSONObject c = rows.get(index);
        final long id = c.optLong("id");
        final String name = c.optString("name");
        closeCurrent();
        current = new AlertDialog.Builder(this)
                .setTitle(name)
                .setMessage("อาชีพ: " + classLabel(c.optString("class")) + "\nเลเวล: " + c.optInt("level"))
                .setCancelable(false)
                .setPositiveButton("เข้าเล่น", (d, w) -> enterWorld(id, name))
                .setNeutralButton("กลับ", (d, w) -> showCharacters(rows))
                .setNegativeButton("ลบตัวละคร", (d, w) -> confirmDelete(id, name))
                .create();
        current.show();
    }

    private void confirmDelete(long id, String name) {
        closeCurrent();
        current = new AlertDialog.Builder(this)
                .setTitle("ลบตัวละคร")
                .setMessage("ลบ \"" + name + "\" ใช่หรือไม่ การลบไม่สามารถกู้คืนได้")
                .setCancelable(false)
                .setPositiveButton("ลบ", (d, w) -> net.execute(() -> {
                    if (!nativeDeleteCharacter(id)) {
                        String err = describe(nativeLastError());
                        ui.post(() -> toast("ลบไม่สำเร็จ: " + err));
                    } else {
                        Log.i(TAG, "character deleted id=" + id);
                    }
                    loadCharacters();
                }))
                .setNegativeButton("ยกเลิก", (d, w) -> showCharacters(rows))
                .create();
        current.show();
    }

    // ---- Create character ----

    void showCreate(String msg) {
        closeCurrent();
        screen = Screen.CREATE;
        LinearLayout box = column();
        createError = text(msg, 14);
        box.addView(createError);
        createName = field("ชื่อตัวละคร (2–16 ตัว)", false);
        createGender = spinner(new String[]{"ชาย", "หญิง"});
        createClass = spinner(CLASS_LABELS);
        box.addView(createName);
        box.addView(text("เพศ", 14));
        box.addView(createGender);
        box.addView(text("อาชีพ", 14));
        box.addView(createClass);

        AlertDialog.Builder b = new AlertDialog.Builder(this)
                .setTitle("สร้างตัวละคร")
                .setView(box)
                .setCancelable(false)
                .setPositiveButton("สร้าง", null);
        if (!rows.isEmpty()) b.setNegativeButton("ยกเลิก", (d, w) -> net.execute(this::loadCharacters));
        current = b.create();
        current.setOnShowListener(d -> current.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> submitCreate()));
        current.show();
    }

    void submitCreate() {
        final String name = createName.getText().toString().trim();
        final String gender = createGender.getSelectedItemPosition() == 0 ? "male" : "female";
        final String cls = CLASS_CODES[createClass.getSelectedItemPosition()];
        if (!CHARACTER_NAME_RE.matcher(name).matches()) {
            createError.setText("ชื่อตัวละครต้องมี 2–16 ตัว (a–z, 0–9, _ หรือภาษาไทย)");
            return;
        }
        createError.setText("กำลังสร้าง...");
        net.execute(() -> {
            long id = nativeCreateCharacter(name, gender, cls);
            if (id < 0) {
                String err = describe(nativeLastError());
                ui.post(() -> createError.setText("สร้างไม่สำเร็จ: " + err));
                return;
            }
            Log.i(TAG, "character created id=" + id);
            loadCharacters();
        });
    }

    // ---- Enter World, game menu, logout ----

    void enterWorld(long id, String name) {
        closeCurrent();
        net.execute(() -> {
            if (!nativeEnterWorld(id)) {
                String err = describe(nativeLastError());
                ui.post(() -> toast("เข้าเล่นไม่สำเร็จ: " + err));
                loadCharacters();
                return;
            }
            ui.post(() -> {
                screen = Screen.GAME;
                enteredId = id;
                toast("เข้าสู่โลก: " + name);
            });
            Log.i(TAG, "character selected id=" + id + " name=" + name);
        });
    }

    void showGameMenu() {
        closeCurrent();
        current = new AlertDialog.Builder(this)
                .setTitle("เมนู")
                .setCancelable(true)
                .setPositiveButton("เล่นต่อ", null)
                .setNeutralButton("กลับหน้าตัวละคร", (d, w) -> net.execute(this::loadCharacters))
                .setNegativeButton("ออกจากระบบ", (d, w) -> logout())
                .create();
        current.show();
    }

    void logout() {
        closeCurrent();
        net.execute(() -> {
            nativeLogout();
            Log.i(TAG, "logged out");
            ui.post(() -> {
                rows = new ArrayList<>();
                characterCount = -1;
                enteredId = 0;
                showLogin("ออกจากระบบแล้ว");
            });
        });
    }

    // ---- helpers ----

    static List<JSONObject> parseCharacters(String json) {
        List<JSONObject> out = new ArrayList<>();
        try {
            JSONArray list = new JSONObject(json).optJSONArray("characters");
            if (list != null) {
                for (int i = 0; i < list.length(); i++) out.add(list.getJSONObject(i));
            }
        } catch (Exception e) {
            Log.i(TAG, "bad character list: " + e);
        }
        return out;
    }

    /** Turns a net_client error (e.g. "HTTP 401 invalid_credentials") into a message for the user. */
    static String describe(String err) {
        if (err == null || err.isEmpty()) return "ไม่ทราบสาเหตุ";
        if (err.startsWith("network:") || err.startsWith("bridge") || err.startsWith("no response")
                || err.startsWith("bad bridge")) return "เชื่อมต่อเซิร์ฟเวอร์ไม่ได้";
        if (err.contains("invalid_credentials")) return "ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง";
        if (err.contains("username_taken")) return "ชื่อผู้ใช้นี้ถูกใช้แล้ว";
        if (err.contains("too_many_attempts")) return "ล็อกอินผิดหลายครั้ง กรุณารอสักครู่";
        if (err.contains("character_limit")) return "ครบจำนวนตัวละครสูงสุดแล้ว";
        if (err.contains("name_taken")) return "ชื่อตัวละครนี้ถูกใช้แล้ว";
        if (err.contains("character_not_found")) return "ไม่พบตัวละครนี้บนเซิร์ฟเวอร์";
        if (err.contains("unauthorized")) return "หมดเวลาเข้าสู่ระบบ กรุณาเข้าสู่ระบบใหม่";
        return "เกิดข้อผิดพลาด (" + err + ")";
    }

    static String classLabel(String cls) {
        for (int i = 0; i < CLASS_CODES.length; i++) {
            if (CLASS_CODES[i].equals(cls)) return CLASS_LABELS[i];
        }
        return cls;
    }

    void closeCurrent() {
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

    void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
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
