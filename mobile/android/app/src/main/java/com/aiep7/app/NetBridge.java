// AI EP7 - HTTP transport for mobile/net_client on Android.
// The native code calls request(); this class performs it with HttpURLConnection, which uses the
// system trust store, so HTTPS needs no bundled TLS library.
//
// Base URL: BuildConfig.SERVER_URL, set at build time (see app/build.gradle); never hard-coded here.
// Release builds only accept https://. Debug builds also accept plain http:// to loopback and to
// 10.0.2.2 (the Android emulator's alias for the host machine), and network_security_config.xml
// allows cleartext for those hosts in debug only.
package com.aiep7.app;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

final class NetBridge {
    private static final int TIMEOUT_MS = 10000;

    private NetBridge() {}

    /**
     * Returns "<status>\n<body>". On a transport failure returns "0\n<message>", so the native side
     * never has to handle a Java exception.
     */
    static String request(String method, String path, String token, String body) {
        try {
            String base = BuildConfig.SERVER_URL;
            if (base.isEmpty()) return "0\nAPI base URL is not set (build with -PranServerUrl)";
            if (!allowedBase(base)) return "0\nbase URL refused: " + base;
            if (!path.startsWith("/api/")) return "0\nbad path";

            HttpURLConnection c = (HttpURLConnection) new URL(base + path).openConnection();
            c.setConnectTimeout(TIMEOUT_MS);
            c.setReadTimeout(TIMEOUT_MS);
            c.setRequestMethod(method);
            c.setRequestProperty("Accept", "application/json");
            c.setUseCaches(false);
            if (token != null && !token.isEmpty()) c.setRequestProperty("Authorization", "Bearer " + token);
            if (body != null && !body.isEmpty()) {
                byte[] payload = body.getBytes(StandardCharsets.UTF_8);
                c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/json; charset=utf-8");
                c.setFixedLengthStreamingMode(payload.length);
                try (OutputStream out = c.getOutputStream()) {
                    out.write(payload);
                }
            }
            int status = c.getResponseCode();
            InputStream in = status >= 400 ? c.getErrorStream() : c.getInputStream();
            String text = in == null ? "" : readAll(in);
            c.disconnect();
            return status + "\n" + text;
        } catch (IOException e) {
            return "0\n" + e.getClass().getSimpleName() + ": " + e.getMessage();
        }
    }

    static boolean allowedBase(String base) {
        if (base.startsWith("https://")) return true;
        if (!BuildConfig.DEBUG || !base.startsWith("http://")) return false;
        return base.startsWith("http://10.0.2.2:") || base.startsWith("http://127.0.0.1:")
                || base.startsWith("http://localhost:");
    }

    private static String readAll(InputStream in) throws IOException {
        try (InputStream src = in) {
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] chunk = new byte[4096];
            int n;
            while ((n = src.read(chunk)) > 0) buf.write(chunk, 0, n);
            return new String(buf.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
