// include/wifi_manager.h — first-boot WiFi provisioning (AP + captive
// portal) and normal STA reconnection. This is the piece that lets someone
// with zero technical knowledge get the device online: power it up, connect
// their phone to "TurboUSD-Setup-XXXX", pick their home WiFi, done.

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "lwip/dns.h"   // secondary DNS fallback — see _installFallbackDns()
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
    // (fallback DNS helper defined below, installed on every successful connect)

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
            // (Re)install the DNS fallback once per connection — DHCP renews
            // and reconnects reset the server list.
            if (isConnected() && !_dnsFallbackSet) { _installFallbackDns(); _dnsFallbackSet = true; }
            return;
        }
        _dnsFallbackSet = false;   // dropped — reinstall after the next connect
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
    bool _dnsFallbackSet = false;
    uint32_t _lastConnectedAt = 0;
    uint32_t _lastReconnectAt = 0;
    String _scanOptions;   // cached <option> list, scanned once before the AP is up
    String _scanButtons;   // cached tappable network rows (mobile-friendly picker)

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
            _installFallbackDns();
        }
    }

    // Secondary DNS fallback. The firmware bursts requests at half a dozen
    // different hosts (DexScreener, its CDN, GeckoTerminal, Supabase, OpenSea,
    // ensideas…) and lwIP's tiny DNS cache + some router resolvers buckle,
    // producing intermittent "DNS Failed for <host>" (surfacing as HTTP -1 /
    // "connection refused"). Registering 8.8.8.8 as the SECONDARY server
    // keeps the router's DNS as primary but gives every lookup a solid
    // second chance.
    void _installFallbackDns() {
        ip_addr_t d;
        IP_ADDR4(&d, 8, 8, 8, 8);
        dns_setserver(1, &d);
        Serial.println("DNS fallback (8.8.8.8) installed as secondary.");
    }

    void startProvisioningPortal() {
        portalActive = true;

        // Scan for nearby networks FIRST, in STA mode, and cache the result.
        // Scanning is a blocking, channel-hopping operation; running it later
        // from inside an HTTP handler (while the AP is up and a phone is mid-
        // request) drops the AP and leaves the setup page blank. Doing it once
        // up front, before softAP(), avoids that entirely.
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        int n = WiFi.scanNetworks();
        _scanOptions = "";
        _scanButtons = "";
        for (int i = 0; i < n; i++) {
            String safe = escapeHtml(WiFi.SSID(i));
            if (!safe.length()) continue;
            // Skip duplicate SSIDs (mesh / multi-band APs show up several times).
            if (_scanOptions.indexOf("\"" + safe + "\"") >= 0) continue;
            _scanOptions += "<option value=\"" + safe + "\"></option>";
            _scanButtons += "<div class='net' onclick=\"pick('" + safe + "')\">" + safe + "</div>";
        }
        WiFi.scanDelete();

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
        // Uses the network list cached at portal start (see startProvisioningPortal).
        // The SSID is an editable text input with the scan results offered as an
        // autocomplete datalist — so the user can pick a nearby network OR type a
        // hidden/5 GHz one we couldn't see. No blocking scan happens here.
        // maximum-scale=1 + 16px inputs stop iOS Safari from auto-zooming when a
        // field is focused (it only zooms when the input font is < 16px). The
        // tappable network list means the user picks their SSID instead of having
        // to type (and remember) it; the text field stays for hidden networks.
        String netList = _scanButtons.length()
            ? _scanButtons
            : String("<div class='net muted'>No networks found &mdash; type yours below</div>");

        String html =
            "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
            "<title>TurboUSD Node Setup</title>"
            "<style>"
            "*{box-sizing:border-box;}"
            "body{font-family:-apple-system,system-ui,Segoe UI,Roboto,sans-serif;background:#000;color:#e8e8ea;margin:0 auto;padding:22px;max-width:520px;}"
            "h2{color:#3aff7a;font-size:21px;margin:6px 0 4px;}"
            "p.sub{color:#9a9a9e;font-size:14px;margin:0 0 14px;}"
            "label{display:block;color:#9a9a9e;font-size:12px;letter-spacing:1px;text-transform:uppercase;margin:16px 0 6px;}"
            "input{width:100%;padding:14px;font-size:16px;background:#111;color:#fff;border:1px solid #2eaa50;border-radius:10px;}"
            "input:focus{outline:none;border-color:#3aff7a;}"
            ".nets{max-height:210px;overflow-y:auto;border:1px solid #262626;border-radius:10px;}"
            ".net{padding:14px;font-size:16px;color:#fff;border-bottom:1px solid #191919;cursor:pointer;}"
            ".net:last-child{border-bottom:none;}"
            ".net:active{background:#10331e;}"
            ".net.muted{color:#6e7280;cursor:default;}"
            "button{width:100%;padding:16px;font-size:17px;font-weight:700;margin-top:22px;background:#3aff7a;color:#000;border:none;border-radius:10px;}"
            "</style></head><body>"
            "<h2>Connect your TurboUSD Node</h2>"
            "<p class='sub'>Tap your WiFi network below, or type it in.<br>"
            "<span style='color:#ffcf72'>2.4 GHz networks only &mdash; 5 GHz won't work.</span></p>"
            "<form action='/connect' method='POST'>"
            "<label>Nearby networks</label>"
            "<div class='nets'>" + netList + "</div>"
            "<label>WiFi name</label>"
            "<input id='ssid' name='ssid' list='nets' placeholder='Your WiFi name' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false'>"
            "<datalist id='nets'>" + _scanOptions + "</datalist>"
            "<label>Password</label>"
            "<input id='pw' type='password' name='password' autocomplete='off'>"
            "<button type='submit'>Connect</button>"
            "</form>"
            "<script>function pick(n){var s=document.getElementById('ssid');s.value=n;"
            "document.getElementById('pw').focus();}</script>"
            "</body></html>";

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
