/*
 *  HID driver for some sony "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2006-2008 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/hid.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define VAIO_RDESC_CONSTANT     (1 << 0)
#define SIXAXIS_CONTROLLER_USB  (1 << 1)
#define SIXAXIS_CONTROLLER_BT   (1 << 2)
#define DUALSHOCK4_CONTROLLER_USB (1 << 3)

static const u8 sixaxis_rdesc_fixup[] = {
	0x95, 0x13, 0x09, 0x01, 0x81, 0x02, 0x95, 0x0C,
	0x81, 0x01, 0x75, 0x10, 0x95, 0x04, 0x26, 0xFF,
	0x03, 0x46, 0xFF, 0x03, 0x09, 0x01, 0x81, 0x02
};


/*
 * The default descriptor doesn't provide mapping for the accelerometers
 * or orientation sensors.  This fixed descriptor maps the accelerometers
 * to usage values 0x40, 0x41 and 0x42 and maps the orientation sensors
 * to usage values 0x43, 0x44 and 0x45.
 */
static u8 dualshock4_usb_rdesc[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x05,         /*  Usage (Gamepad),                    */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x01,         /*      Report ID (1),                  */
	0x09, 0x30,         /*      Usage (X),                      */
	0x09, 0x31,         /*      Usage (Y),                      */
	0x09, 0x32,         /*      Usage (Z),                      */
	0x09, 0x35,         /*      Usage (Rz),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x04,         /*      Report Count (4),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x39,         /*      Usage (Hat Switch),             */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x07,         /*      Logical Maximum (7),            */
	0x35, 0x00,         /*      Physical Minimum (0),           */
	0x46, 0x3B, 0x01,   /*      Physical Maximum (315),         */
	0x65, 0x14,         /*      Unit (Degrees),                 */
	0x75, 0x04,         /*      Report Size (4),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x81, 0x42,         /*      Input (Variable, Null State),   */
	0x65, 0x00,         /*      Unit,                           */
	0x05, 0x09,         /*      Usage Page (Button),            */
	0x19, 0x01,         /*      Usage Minimum (01h),            */
	0x29, 0x0E,         /*      Usage Maximum (0Eh),            */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x75, 0x01,         /*      Report Size (1),                */
	0x95, 0x0E,         /*      Report Count (14),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x75, 0x06,         /*      Report Size (6),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x7F,         /*      Logical Maximum (127),          */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x09, 0x33,         /*      Usage (Rx),                     */
	0x09, 0x34,         /*      Usage (Ry),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x02,         /*      Report Count (2),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x19, 0x40,         /*      Usage Minimum (40h),            */
	0x29, 0x42,         /*      Usage Maximum (42h),            */
	0x16, 0x00, 0x80,   /*      Logical Minimum (-32768),       */
	0x26, 0x00, 0x7F,   /*      Logical Maximum (32767),        */
	0x75, 0x10,         /*      Report Size (16),               */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x19, 0x43,         /*      Usage Minimum (43h),            */
	0x29, 0x45,         /*      Usage Maximum (45h),            */
	0x16, 0xFF, 0xBF,   /*      Logical Minimum (-16385),       */
	0x26, 0x00, 0x40,   /*      Logical Maximum (16384),        */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0xFF,         /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x27,         /*      Report Count (39),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x85, 0x05,         /*      Report ID (5),                  */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x1F,         /*      Report Count (31),              */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x04,         /*      Report ID (4),                  */
	0x09, 0x23,         /*      Usage (23h),                    */
	0x95, 0x24,         /*      Report Count (36),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x02,         /*      Report ID (2),                  */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0x24,         /*      Report Count (36),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x95, 0x04,         /*      Report Count (4),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x11,         /*      Report ID (17),                 */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x12,         /*      Report ID (18),                 */
	0x06, 0x02, 0xFF,   /*      Usage Page (FF02h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x0F,         /*      Report Count (15),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x13,         /*      Report ID (19),                 */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x16,         /*      Report Count (22),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x14,         /*      Report ID (20),                 */
	0x06, 0x05, 0xFF,   /*      Usage Page (FF05h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x10,         /*      Report Count (16),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x15,         /*      Report ID (21),                 */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x2C,         /*      Report Count (44),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x06, 0x80, 0xFF,   /*      Usage Page (FF80h),             */
	0x85, 0x80,         /*      Report ID (128),                */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x81,         /*      Report ID (129),                */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x82,         /*      Report ID (130),                */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x05,         /*      Report Count (5),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x83,         /*      Report ID (131),                */
	0x09, 0x23,         /*      Usage (23h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x84,         /*      Report ID (132),                */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0x04,         /*      Report Count (4),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x85,         /*      Report ID (133),                */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x86,         /*      Report ID (134),                */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x87,         /*      Report ID (135),                */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x95, 0x23,         /*      Report Count (35),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x88,         /*      Report ID (136),                */
	0x09, 0x28,         /*      Usage (28h),                    */
	0x95, 0x22,         /*      Report Count (34),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x89,         /*      Report ID (137),                */
	0x09, 0x29,         /*      Usage (29h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x90,         /*      Report ID (144),                */
	0x09, 0x30,         /*      Usage (30h),                    */
	0x95, 0x05,         /*      Report Count (5),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x91,         /*      Report ID (145),                */
	0x09, 0x31,         /*      Usage (31h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x92,         /*      Report ID (146),                */
	0x09, 0x32,         /*      Usage (32h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x93,         /*      Report ID (147),                */
	0x09, 0x33,         /*      Usage (33h),                    */
	0x95, 0x0C,         /*      Report Count (12),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA0,         /*      Report ID (160),                */
	0x09, 0x40,         /*      Usage (40h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA1,         /*      Report ID (161),                */
	0x09, 0x41,         /*      Usage (41h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA2,         /*      Report ID (162),                */
	0x09, 0x42,         /*      Usage (42h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA3,         /*      Report ID (163),                */
	0x09, 0x43,         /*      Usage (43h),                    */
	0x95, 0x30,         /*      Report Count (48),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA4,         /*      Report ID (164),                */
	0x09, 0x44,         /*      Usage (44h),                    */
	0x95, 0x0D,         /*      Report Count (13),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA5,         /*      Report ID (165),                */
	0x09, 0x45,         /*      Usage (45h),                    */
	0x95, 0x15,         /*      Report Count (21),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA6,         /*      Report ID (166),                */
	0x09, 0x46,         /*      Usage (46h),                    */
	0x95, 0x15,         /*      Report Count (21),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF0,         /*      Report ID (240),                */
	0x09, 0x47,         /*      Usage (47h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF1,         /*      Report ID (241),                */
	0x09, 0x48,         /*      Usage (48h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF2,         /*      Report ID (242),                */
	0x09, 0x49,         /*      Usage (49h),                    */
	0x95, 0x0F,         /*      Report Count (15),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA7,         /*      Report ID (167),                */
	0x09, 0x4A,         /*      Usage (4Ah),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA8,         /*      Report ID (168),                */
	0x09, 0x4B,         /*      Usage (4Bh),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA9,         /*      Report ID (169),                */
	0x09, 0x4C,         /*      Usage (4Ch),                    */
	0x95, 0x08,         /*      Report Count (8),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAA,         /*      Report ID (170),                */
	0x09, 0x4E,         /*      Usage (4Eh),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAB,         /*      Report ID (171),                */
	0x09, 0x4F,         /*      Usage (4Fh),                    */
	0x95, 0x39,         /*      Report Count (57),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAC,         /*      Report ID (172),                */
	0x09, 0x50,         /*      Usage (50h),                    */
	0x95, 0x39,         /*      Report Count (57),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAD,         /*      Report ID (173),                */
	0x09, 0x51,         /*      Usage (51h),                    */
	0x95, 0x0B,         /*      Report Count (11),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAE,         /*      Report ID (174),                */
	0x09, 0x52,         /*      Usage (52h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAF,         /*      Report ID (175),                */
	0x09, 0x53,         /*      Usage (53h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xB0,         /*      Report ID (176),                */
	0x09, 0x54,         /*      Usage (54h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0xC0                /*  End Collection                      */
};
#define DS4_LED_COUNT 3
#define DS4_MAX_PADS 4
#define DS4_OUTPUT_REPORT_SIZE 32

struct sony_sc {
	unsigned long quirks;
	struct hid_device *hdev;
	struct work_struct led_work;
	struct led_classdev led[DS4_LED_COUNT];
	char led_name[DS4_LED_COUNT][48];
	u8 color[DS4_LED_COUNT];
	int pad_index;
};

static const u8 ds4_default_colors[DS4_MAX_PADS][DS4_LED_COUNT] = {
	{ 0x00, 0x00, 0x40 },
	{ 0x40, 0x00, 0x00 },
	{ 0x00, 0x40, 0x00 },
	{ 0x40, 0x00, 0x40 },
};
static const char * const ds4_led_names[DS4_LED_COUNT] = { "red", "green", "blue" };

static DEFINE_SPINLOCK(ds4_pad_lock);
static unsigned long ds4_pads_in_use;

static void ds4_led_work(struct work_struct *work)
{
	struct sony_sc *sc = container_of(work, struct sony_sc, led_work);
	struct hid_device *hdev = sc->hdev;
	u8 *buf = kzalloc(DS4_OUTPUT_REPORT_SIZE, GFP_KERNEL);

	if (!buf)
		return;
	buf[0] = 0x05;
	buf[1] = 0xff;
	buf[6] = sc->color[0];
	buf[7] = sc->color[1];
	buf[8] = sc->color[2];
	hdev->hid_output_raw_report(hdev, buf, DS4_OUTPUT_REPORT_SIZE, HID_OUTPUT_REPORT);
	kfree(buf);
}

static void ds4_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct device *dev = led->dev->parent;
	struct sony_sc *sc = hid_get_drvdata(container_of(dev, struct hid_device, dev));
	int i;

	for (i = 0; i < DS4_LED_COUNT; i++)
		if (led == &sc->led[i])
			sc->color[i] = value;
	schedule_work(&sc->led_work);
}

static enum led_brightness ds4_led_get(struct led_classdev *led)
{
	struct device *dev = led->dev->parent;
	struct sony_sc *sc = hid_get_drvdata(container_of(dev, struct hid_device, dev));
	int i;

	for (i = 0; i < DS4_LED_COUNT; i++)
		if (led == &sc->led[i])
			return sc->color[i];
	return 0;
}

static int ds4_leds_init(struct sony_sc *sc)
{
	struct hid_device *hdev = sc->hdev;
	int i;
	int ret;

	spin_lock(&ds4_pad_lock);
	sc->pad_index = find_first_zero_bit(&ds4_pads_in_use, DS4_MAX_PADS);
	if (sc->pad_index < DS4_MAX_PADS)
		set_bit(sc->pad_index, &ds4_pads_in_use);
	spin_unlock(&ds4_pad_lock);

	INIT_WORK(&sc->led_work, ds4_led_work);
	for (i = 0; i < DS4_LED_COUNT; i++)
		sc->color[i] = ds4_default_colors[sc->pad_index < DS4_MAX_PADS ? sc->pad_index : 0][i];

	for (i = 0; i < DS4_LED_COUNT; i++) {
		snprintf(sc->led_name[i], sizeof(sc->led_name[i]), "%s:%s", dev_name(&hdev->dev), ds4_led_names[i]);
		sc->led[i].name = sc->led_name[i];
		sc->led[i].max_brightness = 255;
		sc->led[i].brightness_set = ds4_led_set;
		sc->led[i].brightness_get = ds4_led_get;
		ret = led_classdev_register(&hdev->dev, &sc->led[i]);
		if (ret) {
			while (--i >= 0)
				led_classdev_unregister(&sc->led[i]);
			goto err_release;
		}
	}
	schedule_work(&sc->led_work);
	return 0;

err_release:
	if (sc->pad_index < DS4_MAX_PADS)
		clear_bit(sc->pad_index, &ds4_pads_in_use);
	return ret;
}

static void ds4_leds_remove(struct sony_sc *sc)
{
	int i;

	for (i = 0; i < DS4_LED_COUNT; i++)
		led_classdev_unregister(&sc->led[i]);
	cancel_work_sync(&sc->led_work);
	if (sc->pad_index < DS4_MAX_PADS)
		clear_bit(sc->pad_index, &ds4_pads_in_use);
}

/* Sony Vaio VGX has wrongly mouse pointer declared as constant */
static __u8 *sony_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/*
	 * Some Sony RF receivers wrongly declare the mouse pointer as a
	 * a constant non-data variable.
	 */
	if ((sc->quirks & VAIO_RDESC_CONSTANT) && *rsize >= 56 &&
	    /* usage page: generic desktop controls */
	    /* rdesc[0] == 0x05 && rdesc[1] == 0x01 && */
	    /* usage: mouse */
	    rdesc[2] == 0x09 && rdesc[3] == 0x02 &&
	    /* input (usage page for x,y axes): constant, variable, relative */
	    rdesc[54] == 0x81 && rdesc[55] == 0x07) {
		hid_info(hdev, "Fixing up Sony RF Receiver report descriptor\n");
		/* input: data, variable, relative */
		rdesc[55] = 0x06;
	}

	if ((sc->quirks & DUALSHOCK4_CONTROLLER_USB) && *rsize == 467) {
		hid_info(hdev, "Using modified Dualshock 4 report descriptor with gyroscope axes\n");
		rdesc = dualshock4_usb_rdesc;
		*rsize = sizeof(dualshock4_usb_rdesc);
	} else if (sc->quirks & DUALSHOCK4_CONTROLLER_USB) {
		hid_info(hdev, "Dualshock 4 report descriptor of %u bytes left as is\n", *rsize);
	}

	/* The HID descriptor exposed over BT has a trailing zero byte */
	if ((((sc->quirks & SIXAXIS_CONTROLLER_USB) && *rsize == 148) ||
			((sc->quirks & SIXAXIS_CONTROLLER_BT) && *rsize == 149)) &&
			rdesc[83] == 0x75) {
		hid_info(hdev, "Fixing up Sony Sixaxis report descriptor\n");
		memcpy((void *)&rdesc[83], (void *)&sixaxis_rdesc_fixup,
			sizeof(sixaxis_rdesc_fixup));
	}
	return rdesc;
}

static int sony_raw_event(struct hid_device *hdev, struct hid_report *report,
		__u8 *rd, int size)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/* Sixaxis HID report has acclerometers/gyro with MSByte first, this
	 * has to be BYTE_SWAPPED before passing up to joystick interface
	 */
	if ((sc->quirks & (SIXAXIS_CONTROLLER_USB | SIXAXIS_CONTROLLER_BT)) &&
			rd[0] == 0x01 && size == 49) {
		swap(rd[41], rd[42]);
		swap(rd[43], rd[44]);
		swap(rd[45], rd[46]);
		swap(rd[47], rd[48]);
	}

	return 0;
}

/*
 * The Sony Sixaxis does not handle HID Output Reports on the Interrupt EP
 * like it should according to usbhid/hid-core.c::usbhid_output_raw_report()
 * so we need to override that forcing HID Output Reports on the Control EP.
 *
 * There is also another issue about HID Output Reports via USB, the Sixaxis
 * does not want the report_id as part of the data packet, so we have to
 * discard buf[0] when sending the actual control message, even for numbered
 * reports, humpf!
 */
static int sixaxis_usb_output_raw_report(struct hid_device *hid, __u8 *buf,
		size_t count, unsigned char report_type)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface = intf->cur_altsetting;
	int report_id = buf[0];
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		/* Don't send the Report ID */
		buf++;
		count--;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_REPORT,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		((report_type + 1) << 8) | report_id,
		interface->desc.bInterfaceNumber, buf, count,
		USB_CTRL_SET_TIMEOUT);

	/* Count also the Report ID, in case of an Output report. */
	if (ret > 0 && report_type == HID_OUTPUT_REPORT)
		ret++;

	return ret;
}

/*
 * Sending HID_REQ_GET_REPORT changes the operation mode of the ps3 controller
 * to "operational".  Without this, the ps3 controller will not report any
 * events.
 */
static int sixaxis_set_operational_usb(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	__u16 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	int ret;
	char *buf = kmalloc(18, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				 HID_REQ_GET_REPORT,
				 USB_DIR_IN | USB_TYPE_CLASS |
				 USB_RECIP_INTERFACE,
				 (3 << 8) | 0xf2, ifnum, buf, 17,
				 USB_CTRL_GET_TIMEOUT);
	if (ret < 0)
		hid_err(hdev, "can't set operational mode\n");

	kfree(buf);

	return ret;
}

static int sixaxis_set_operational_bt(struct hid_device *hdev)
{
	unsigned char buf[] = { 0xf4,  0x42, 0x03, 0x00, 0x00 };
	return hdev->hid_output_raw_report(hdev, buf, sizeof(buf), HID_FEATURE_REPORT);
}

static int sony_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct sony_sc *sc;

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		hid_err(hdev, "can't alloc sony descriptor\n");
		return -ENOMEM;
	}

	sc->quirks = quirks;
	sc->hdev = hdev;
	hid_set_drvdata(hdev, sc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT |
			HID_CONNECT_HIDDEV_FORCE);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	if (sc->quirks & SIXAXIS_CONTROLLER_USB) {
		hdev->hid_output_raw_report = sixaxis_usb_output_raw_report;
		ret = sixaxis_set_operational_usb(hdev);
	}
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		ret = sixaxis_set_operational_bt(hdev);
	else if (sc->quirks & DUALSHOCK4_CONTROLLER_USB)
		ret = ds4_leds_init(sc);
	else
		ret = 0;

	if (ret < 0)
		goto err_stop;

	return 0;
err_stop:
	hid_hw_stop(hdev);
err_free:
	kfree(sc);
	return ret;
}

static void sony_remove(struct hid_device *hdev)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & DUALSHOCK4_CONTROLLER_USB)
		ds4_leds_remove(sc);
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
}

static const struct hid_device_id sony_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_NAVIGATION_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
		.driver_data = DUALSHOCK4_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER_2),
		.driver_data = DUALSHOCK4_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_BT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGX_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGP_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	{ }
};
MODULE_DEVICE_TABLE(hid, sony_devices);

static struct hid_driver sony_driver = {
	.name = "sony",
	.id_table = sony_devices,
	.probe = sony_probe,
	.remove = sony_remove,
	.report_fixup = sony_report_fixup,
	.raw_event = sony_raw_event
};

static int __init sony_init(void)
{
	return hid_register_driver(&sony_driver);
}

static void __exit sony_exit(void)
{
	hid_unregister_driver(&sony_driver);
}

module_init(sony_init);
module_exit(sony_exit);
MODULE_LICENSE("GPL");
