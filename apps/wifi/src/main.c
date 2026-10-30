/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi bring-up on the AP6212: scan and log the networks, then join
 * CONFIG_INTUITIV_WIFI_SSID (if set) and wait for a DHCP address.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static K_SEM_DEFINE(connected, 0, 1);
static int found;

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *r = cb->info;

		LOG_INF("%-32.*s ch %2u %4d dBm %s", r->ssid_length, r->ssid, r->channel, r->rssi,
			wifi_security_txt(r->security));
		found++;
	} else if (event == NET_EVENT_WIFI_SCAN_DONE) {
		k_sem_give(&scan_done);
	} else if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		LOG_INF("connect result %d", s->status);
		if (!s->status) {
			k_sem_give(&connected);
		}
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr *a = &iface->config.ip.ipv4->unicast[i].ipv4;

		if (a->is_used && a->addr_type == NET_ADDR_DHCP) {
			LOG_INF("DHCP address %s",
				net_addr_ntop(AF_INET, &a->address.in_addr, buf, sizeof(buf)));
		}
	}
}

int main(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int err;

	if (!iface) {
		LOG_ERR("no Wi-Fi interface");
		return 0;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE |
					     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (err) {
		LOG_ERR("scan request failed (%d)", err);
		return 0;
	}
	k_sem_take(&scan_done, K_SECONDS(30));
	LOG_INF("scan done, %d networks", found);

	if (sizeof(CONFIG_INTUITIV_WIFI_SSID) == 1) {
		return 0;
	}

	struct wifi_connect_req_params p = {
		.ssid = CONFIG_INTUITIV_WIFI_SSID,
		.ssid_length = sizeof(CONFIG_INTUITIV_WIFI_SSID) - 1,
		.psk = CONFIG_INTUITIV_WIFI_PSK,
		.psk_length = sizeof(CONFIG_INTUITIV_WIFI_PSK) - 1,
		.security = sizeof(CONFIG_INTUITIV_WIFI_PSK) > 1 ? WIFI_SECURITY_TYPE_PSK
								 : WIFI_SECURITY_TYPE_NONE,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = WIFI_CHANNEL_ANY,
		.mfp = WIFI_MFP_OPTIONAL,
	};

	LOG_INF("joining %s", CONFIG_INTUITIV_WIFI_SSID);
	err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
	if (err) {
		LOG_ERR("connect request failed (%d)", err);
		return 0;
	}
	if (k_sem_take(&connected, K_SECONDS(30))) {
		LOG_ERR("not connected after 30 s");
		return 0;
	}
	net_dhcpv4_start(iface);
	return 0;
}
