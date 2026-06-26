// include/wifi_manager.h — first-boot WiFi provisioning (AP + captive
// portal) and normal STA reconnection. This is the piece that lets someone
// with zero technical knowledge get the device online: power it up, connect
// their phone to "TurboUSD-Setup-XXXX", pick their home WiFi, done.

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "storage.h"

class WifiManager {
public:
    void begin() {
        if (storage.hasWifiCredentials()) {
            connectToSavedNetwork();
        } else {
            startProvisioningPortal();
        }
    }

    bool isConnected() { return WiFi.status() == WL_CONNECTED; }

    // Call from the main loop while the portal is active so DNS/HTTP keep
    // responding to the captive portal page.
    void loopPortal() {
        if (portalActive) {
            dnsServer.processNextRequest();
            server.handleClient();
        }
    }

    bool isPortalActive() { return portalActive; }

    // Call from the main loop when not in portal mode. Transparently
    // reconnects if the WiFi connection drops (e.g. router restart, RSSI
    // fade). We attempt reconnect every 30s without blocking the UI loop.
    void checkConnection() {
        if (portalActive || isConnected()) {
            _lastConnectedAt = millis();
            return;
        }
        uint32_t now = millis();
        if (now - _lastReconnectAt < 30000) return;
        _lastReconnectAt = now;
        Serial.println("WiFi disconnected -- attempting reconnect...");
        WiFi.reconnect();
    }

private:
    WebServer server{80};
    DNSServer dnsServer;
    bool portalActive = false;
    uint32_t _lastConnectedAt = 0;
    uint32_t _lastReconnectAt = 0;

    // Escape characters that would break the HTML value attribute or display.
    static String escapeHtml(const String& s) {
        String out;
        out.reserve(s.length() + 16);
        for (unsigned int i = 0; i < s.length(); i++) {
            char c = s[i];
            if      (c == '&')  out += "&amp;";
            else if (c == '<')  out += "&lt;";
            else if (c == '>')  out += "&gt;";
            else if (c == '"')  out += "&quot;";
            else if (c == '\'') out += "&#39;";
            else                out += c;
        }
        return out;
    }

    void connectToSavedNetwork() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(storage.getWifiSsid().c_str(), storage.getWifiPass().c_str());

        Serial.print("Connecting to saved WiFi");
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(300);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Could not connect with saved credentials -- falling back to provisioning portal.");
            startProvisioningPortal();
        } else {
            Serial.print("Connected, IP: ");
            Serial.println(WiFi.localIP());
        }
    }

    void startProvisioningPortal() {
        portalActive = true;
        WiFi.mode(WIFI_AP);

        // Short suffix from the chip's own MAC so each unit's AP name is
        // unique without needing to know the node_code yet (that only
        // exists after the backend has registered the device).
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char apName[32];
        snprintf(apName, sizeof(apName), "TurboUSD-Setup-%02X%02X", mac[4], mac[5]);

        WiFi.softAP(apName);
        Serial.print("Provisioning AP started: ");
        Serial.println(apName);

        // Captive portal: redirect every DNS query to ourselves so phones
        // auto-open the setup page instead of the user having to type an IP.
        dnsServer.start(53, "*", WiFi.softAPIP());

        server.on("/", HTTP_GET, [this]() { handlePortalRoot(); });
        server.on("/connect", HTTP_POST, [this]() { handlePortalConnect(); });
        server.onNotFound([this]() { handlePortalRoot(); }); // catch-all for captive portal probes
        server.begin();
    }

    void handlePortalRoot() {
        int n = WiFi.scanNetworks();
        String options = "";
        for (int i = 0; i < n; i++) {
            String safe = escapeHtml(WiFi.SSID(i));
            options += "<option value=\"" + safe + "\">" + safe + "</option>";
        }

        String html =
            "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>TurboUSD Node Setup</title>"
            "<style>body{font-family:sans-serif;background:#000;color:#3aff7a;padding:24px;}"
            "input,select{width:100%;padding:10px;margin:8px 0;background:#111;color:#fff;border:1px solid #2eaa50;border-radius:6px;}"
            "button{width:100%;padding:12px;background:#3aff7a;color:#000;border:none;border-radius:6px;font-weight:bold;}</style>"
            "</head><body>"
            "<h2>Connect your TurboUSD Node</h2>"
            "<form action='/connect' method='POST'>"
            "<label>WiFi network</label><select name='ssid'>" + options + "</select>"
            "<label>Password</label><input type='password' name='password'>"
            "<button type='submit'>Connect</button>"
            "</form></body></html>";

        server.send(200, "text/html", html);
    }

    void handlePortalConnect() {
        String ssid = server.arg("ssid");
        String password = server.arg("password");

        if (ssid.length() == 0) {
            server.send(400, "text/plain", "SSID is required");
            return;
        }

        storage.setWifiCredentials(ssid, password);
        server.send(200, "text/html",
            "<html><body style='font-family:sans-serif;background:#000;color:#3aff7a;padding:24px;'>"
            "<h2>Saved. Restarting...</h2></body></html>");

        delay(1500);
        ESP.restart(); // simplest, most reliable way to drop the AP and join the real network
    }
};

extern WifiManager wifiManager;
