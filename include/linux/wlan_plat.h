/* include/linux/wlan_plat.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _LINUX_WLAN_PLAT_H_
#define _LINUX_WLAN_PLAT_H_

struct wifi_platform_data {
	int (*set_power)(int val);
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
	void *(*get_country_code)(char *ccode);
#ifdef CONFIG_PARTIALRESUME
#define WIFI_PR_INIT			0
#define WIFI_PR_NOTIFY_RESUME		1
#define WIFI_PR_VOTE_FOR_RESUME		2
#define WIFI_PR_VOTE_FOR_SUSPEND	3
#define WIFI_PR_WAIT_FOR_READY		4
#define WIFI_PR_WD_INIT			5
#define WIFI_PR_WD_COMPLETE		6
	bool (*partial_resume)(int action);
#endif
};

#endif
