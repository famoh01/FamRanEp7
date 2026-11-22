// AI EP7 - debug-only UI test driver (runs only when launched with --ez ci_ui_test true in a debug build).
//
// It works through the REAL dialogs: it types into the real EditText fields, presses the real dialog
// buttons, and selects rows in the real character list. Each step waits for the screen state it needs,
// runs one action, then waits for the result from the server. Results are logged as:
//   CI_TEST <name> PASS|FAIL <detail>        and at the end   CI_TEST_SUMMARY pass=N fail=M total=T
//
// The server is the real RAN MOBILE server (ran_mobile/Server). Nothing is faked: accounts and characters
// are created on the server, and every check reads the state the app got back from it.
package com.aiep7.app;

import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

final class CiUiTest {
    private static final long STEP_TIMEOUT_MS = 40000;
    private static final long TICK_MS = 250;

    private interface Cond { boolean ok(); }
    private interface Act { void run(); }

    private static final class Step {
        final String name;
        final Cond ready;
        final Act act;
        final Cond done;

        Step(String name, Cond ready, Act act, Cond done) {
            this.name = name;
            this.ready = ready;
            this.act = act;
            this.done = done;
        }
    }

    private final MainActivity a;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<Step> steps = new ArrayList<>();
    private final String tag = Long.toString(System.currentTimeMillis() % 100000000L);
    private final String user = "ciu" + tag;                       // 3-20 chars: a-z and digits
    private final String password = "CiUiPass-2026";
    private final String wrongPassword = "WrongPass-123";
    private final String charName = "Ci" + tag.substring(Math.max(0, tag.length() - 6));

    private int next = 0, passed = 0, failed = 0;
    private Step running;
    private boolean acted;
    private long stepStart;

    CiUiTest(MainActivity activity) {
        this.a = activity;
        buildSteps();
    }

    void start() {
        ui.postDelayed(this::tick, 1500);
    }

    private void buildSteps() {
        // 1. Login validation: empty fields are refused before any network call.
        steps.add(new Step("login_validation",
                () -> a.screen == MainActivity.Screen.LOGIN && a.loginUser != null,
                () -> {
                    a.loginUser.setText("");
                    a.loginPass.setText("");
                    positive(a.current);
                },
                () -> a.lastMessage.contains("กรอกชื่อผู้ใช้และรหัสผ่าน") && a.screen == MainActivity.Screen.LOGIN));

        // 2. Login failed: an unknown account gets invalid_credentials from the server.
        steps.add(new Step("login_failed",
                () -> a.screen == MainActivity.Screen.LOGIN && a.loginUser != null,
                () -> {
                    a.loginUser.setText(user);
                    a.loginPass.setText(wrongPassword);
                    positive(a.current);
                },
                () -> a.lastMessage.contains("ไม่ถูกต้อง") && a.screen == MainActivity.Screen.LOGIN));

        // 3. Login success: "สมัครบัญชี" registers the new account and logs it in. It has no characters yet.
        steps.add(new Step("login_success",
                () -> a.screen == MainActivity.Screen.LOGIN && a.loginUser != null,
                () -> {
                    a.loginUser.setText(user);
                    a.loginPass.setText(password);
                    neutral(a.current);
                },
                () -> a.screen == MainActivity.Screen.CREATE && a.characterCount == 0));

        // 4. Character empty: the create screen opens by itself when the account has no characters.
        steps.add(new Step("character_empty",
                () -> a.screen == MainActivity.Screen.CREATE,
                () -> { },
                () -> a.characterCount == 0 && a.rows.isEmpty() && isShowing(a.current)));

        // 5. Create character: the real create dialog, with name, gender and class.
        steps.add(new Step("create_character",
                () -> a.screen == MainActivity.Screen.CREATE && a.createName != null && isShowing(a.current),
                () -> {
                    a.createName.setText(charName);
                    a.createGender.setSelection(0);
                    a.createClass.setSelection(0);
                    positive(a.current);
                },
                () -> a.screen == MainActivity.Screen.CHARACTERS && a.characterCount == 1));

        // 6. Character list: the server returns the created character.
        steps.add(new Step("character_list",
                () -> a.screen == MainActivity.Screen.CHARACTERS,
                () -> { },
                () -> a.characterCount == 1 && charName.equals(a.rows.get(0).optString("name"))));

        // 7. Select character: tap the row, then "เข้าเล่น" (Enter World).
        steps.add(new Step("select_character",
                () -> a.screen == MainActivity.Screen.CHARACTERS && isShowing(a.current),
                () -> {
                    a.current.getListView().performItemClick(null, 0, 0);
                    positive(a.current);
                },
                () -> a.screen == MainActivity.Screen.GAME && a.enteredId > 0));

        // 8. Logout: Back opens the game menu, then "ออกจากระบบ".
        steps.add(new Step("logout",
                () -> a.screen == MainActivity.Screen.GAME,
                () -> {
                    a.onBackPressed();
                    negative(a.current);
                },
                () -> a.screen == MainActivity.Screen.LOGIN && a.lastMessage.contains("ออกจากระบบแล้ว")));

        // 9. Login success again (existing account, new session).
        steps.add(new Step("login_success_again",
                () -> a.screen == MainActivity.Screen.LOGIN && a.loginUser != null && isShowing(a.current),
                () -> {
                    a.loginUser.setText(user);
                    a.loginPass.setText(password);
                    positive(a.current);
                },
                () -> a.screen == MainActivity.Screen.CHARACTERS && a.characterCount == 1));

        // 10. Delete character: tap the row, "ลบตัวละคร", then confirm "ลบ". The list is empty again.
        steps.add(new Step("delete_character",
                () -> a.screen == MainActivity.Screen.CHARACTERS && isShowing(a.current),
                () -> {
                    a.current.getListView().performItemClick(null, 0, 0);
                    negative(a.current);
                    positive(a.current);
                },
                () -> a.screen == MainActivity.Screen.CREATE && a.characterCount == 0));
    }

    private void tick() {
        if (running == null) {
            if (next >= steps.size()) {
                Log.i(MainActivity.TAG, "CI_TEST_SUMMARY pass=" + passed + " fail=" + failed + " total=" + steps.size());
                return;
            }
            running = steps.get(next++);
            acted = false;
            stepStart = System.currentTimeMillis();
        }
        long waited = System.currentTimeMillis() - stepStart;
        try {
            if (!acted) {
                if (running.ready.ok()) {
                    running.act.run();
                    acted = true;
                } else if (waited > STEP_TIMEOUT_MS) {
                    finish(false, "not ready: " + state());
                    return;
                }
            } else if (running.done.ok()) {
                finish(true, state());
                return;
            } else if (waited > STEP_TIMEOUT_MS) {
                finish(false, "timeout: " + state());
                return;
            }
        } catch (RuntimeException e) {
            finish(false, "exception: " + e);
            return;
        }
        ui.postDelayed(this::tick, TICK_MS);
    }

    private void finish(boolean ok, String detail) {
        if (ok) passed++;
        else failed++;
        Log.i(MainActivity.TAG, "CI_TEST " + running.name + (ok ? " PASS " : " FAIL ") + detail);
        running = null;
        ui.post(this::tick);
    }

    private String state() {
        return "screen=" + a.screen + " count=" + a.characterCount + " entered=" + a.enteredId
                + " msg=" + a.lastMessage;
    }

    private static boolean isShowing(AlertDialog d) {
        return d != null && d.isShowing();
    }

    private static void positive(AlertDialog d) {
        d.getButton(AlertDialog.BUTTON_POSITIVE).performClick();
    }

    private static void neutral(AlertDialog d) {
        d.getButton(AlertDialog.BUTTON_NEUTRAL).performClick();
    }

    private static void negative(AlertDialog d) {
        d.getButton(AlertDialog.BUTTON_NEGATIVE).performClick();
    }
}
