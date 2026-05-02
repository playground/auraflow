/**
 * AP + captive-DNS for first-boot provisioning. See captive_portal.h.
 *
 * The HTTP routes themselves live in http_config_esp.c so the same
 * POST /config handler serves both the portal and the post-provisioning
 * STA-mode editor — one validator, one NVS write path.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "captive_portal.h"
#include "http_config.h"

static const char *TAG = "captive_portal";

#define AP_CHANNEL          1
#define AP_MAX_CONN         4
#define DNS_PORT            53
#define DNS_TASK_STACK      4096
#define DNS_TASK_PRIO       4
#define DNS_BUF_SIZE        512

static bool s_started = false;

/* The default soft-AP IP that esp_netif assigns. We hard-code it here
 * because the DNS responder needs to embed the IP literally in every
 * answer record. If we ever change the AP netif's IP this must move
 * in lockstep — keep them adjacent. */
#define AP_IP_OCTET_0  192
#define AP_IP_OCTET_1  168
#define AP_IP_OCTET_2  4
#define AP_IP_OCTET_3  1

/* ── Soft-AP setup ────────────────────────────────────────────── */

/* Build "AuraFlow-Setup-XXXX" from the last two MAC bytes — same
 * scheme other consumer IoT setup-APs use. */
static void build_ap_ssid(char *out, size_t out_size)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, out_size, "AuraFlow-Setup-%02X%02X", mac[4], mac[5]);
}

static void start_softap(void)
{
    /* esp_netif + event loop are normally created by wifi_mgr_start in
     * the provisioned path. In the unprovisioned path we own them. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    /* esp_netif_create_default_wifi_ap() creates the AP netif but we
     * also need the STA netif so esp_wifi_scan_start can run from the
     * portal's GET /scan handler. APSTA mode = both interfaces alive,
     * AP serving the phone, STA reusable for the scan. */
    esp_netif_create_default_wifi_sta();

    wifi_config_t ap_cfg = { 0 };
    build_ap_ssid((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = strlen((char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel        = AP_CHANNEL;
    ap_cfg.ap.max_connection = AP_MAX_CONN;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;     /* see provisioning-scenarios.md "AP password" */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP up: SSID=\"%s\" open auth, IP %d.%d.%d.%d",
             ap_cfg.ap.ssid,
             AP_IP_OCTET_0, AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3);
}

/* ── Captive DNS responder ────────────────────────────────────── */

/* Walk past a QNAME (sequence of length-prefixed labels terminated by
 * a zero byte). Returns the offset of the first byte after QNAME +
 * QTYPE (2) + QCLASS (2), or 0 on malformed input. */
static int skip_qname(const uint8_t *buf, int len, int offset)
{
    while (offset < len) {
        uint8_t l = buf[offset];
        if (l == 0) {
            offset += 1 + 4;            /* terminator + QTYPE + QCLASS */
            return offset <= len ? offset : 0;
        }
        if ((l & 0xC0) != 0) return 0;  /* compressed pointer in question — bail */
        offset += 1 + l;
    }
    return 0;
}

/* Build a minimal DNS response: echo the question, append one A
 * record pointing at the AP IP, TTL 0 (so phones don't cache). Returns
 * the response length, or 0 on malformed input. */
static int build_dns_response(const uint8_t *q, int q_len,
                              uint8_t *out, int out_size)
{
    if (q_len < 12 || q_len > out_size) return 0;
    int qname_end = skip_qname(q, q_len, 12);
    if (qname_end == 0) return 0;

    /* Copy header + question verbatim. */
    memcpy(out, q, qname_end);

    /* Patch flags: response (QR=1), opcode 0, AA=1, RA=1, RCODE=0. */
    out[2] = 0x84;
    out[3] = 0x80;
    /* ANCOUNT = 1; NSCOUNT/ARCOUNT = 0. */
    out[6] = 0; out[7] = 1;
    out[8] = 0; out[9] = 0;
    out[10] = 0; out[11] = 0;

    int n = qname_end;
    if (n + 16 > out_size) return 0;

    /* Answer: NAME = pointer to QNAME at offset 12. */
    out[n++] = 0xC0; out[n++] = 0x0C;
    /* TYPE = A (1), CLASS = IN (1). */
    out[n++] = 0x00; out[n++] = 0x01;
    out[n++] = 0x00; out[n++] = 0x01;
    /* TTL = 0. */
    out[n++] = 0x00; out[n++] = 0x00;
    out[n++] = 0x00; out[n++] = 0x00;
    /* RDLENGTH = 4. */
    out[n++] = 0x00; out[n++] = 0x04;
    /* RDATA = AP IP. */
    out[n++] = AP_IP_OCTET_0;
    out[n++] = AP_IP_OCTET_1;
    out[n++] = AP_IP_OCTET_2;
    out[n++] = AP_IP_OCTET_3;

    return n;
}

static void dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket() failed: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(DNS_PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind() failed: errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS hijack listening on UDP/%d", DNS_PORT);

    uint8_t  rx[DNS_BUF_SIZE];
    uint8_t  tx[DNS_BUF_SIZE];
    while (1) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx), 0,
                         (struct sockaddr *)&src, &src_len);
        if (n <= 0) continue;

        int reply_len = build_dns_response(rx, n, tx, sizeof(tx));
        if (reply_len <= 0) continue;

        sendto(sock, tx, reply_len, 0, (struct sockaddr *)&src, src_len);
    }
}

/* ── Public API ───────────────────────────────────────────────── */

void captive_portal_start(void)
{
    if (s_started) return;
    s_started = true;

    start_softap();
    http_config_start_portal();

    xTaskCreate(dns_task, "captive_dns",
                DNS_TASK_STACK, NULL, DNS_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "captive portal up — open AuraFlow-Setup-XXXX on a phone "
                  "and the OS should pop the setup page automatically");
}

#endif  /* ESP_PLATFORM */
