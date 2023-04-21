// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fuda Hisi FD6551 LED controller
 *
 * Copyright (c) 2022 Heiner Kallweit
 */


#include <linux/ctype.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <uapi/linux/map_to_7segment.h>

#define NUM_LED_SEGS	7

enum {
	FD6551_GRID_0,
	FD6551_GRID_1,
	FD6551_GRID_2,
	FD6551_GRID_3,
	FD6551_SYMBOLS,
	FD6551_GRID_SIZE
};

struct fd6551_led {
	struct led_classdev	leddev;
	struct i2c_client	*client;
	u8			segment;
};

struct fd6551_priv {
	struct i2c_client	*grid_clients[FD6551_GRID_SIZE];
	struct mutex		lock;
	int			grid_size;
	char			display_data[FD6551_GRID_SIZE];
	char			text[FD6551_GRID_SIZE];
	u8			segment_mapping[NUM_LED_SEGS];
	struct fd6551_led	leds[];
};

static int fd6551_send(struct i2c_client *client, char data)
{
	int ret = i2c_transfer_buffer_flags(client, &data, 1, I2C_M_IGNORE_NAK);

	return ret < 0 ? ret : 0;
}

static int fd6551_display_on(struct i2c_client *client, bool enable)
{
	char cmd = enable ? 1 : 0;

	return fd6551_send(client, cmd);
}

static int fd6551_write_display_data(struct i2c_client *client)
{
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	int i, ret;

	for (i = 0; i < FD6551_GRID_SIZE; i++) {
		if (!priv->grid_clients[i])
			continue;

		ret = fd6551_send(priv->grid_clients[i], priv->display_data[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int fd6551_create_grid(struct i2c_client *client)
{
	static const char *grid_names[FD6551_GRID_SIZE] = {
		"grid_0",
		"grid_1",
		"grid_2",
		"grid_3",
		"symbols",
	};
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	int i;

	for (i = 0; i < FD6551_GRID_SIZE; i++) {
		struct i2c_client *cl;
		int idx, ret;
		u32 addr;

		idx = of_property_match_string(np, "reg-names", grid_names[i]);
		if (idx < 1)
			continue;

		ret = of_property_read_u32_index(np, "reg", idx, &addr);
		if (ret)
			return ret;

		cl = devm_i2c_new_dummy_device(&client->dev, client->adapter, addr);
		if (IS_ERR(cl))
			return PTR_ERR(cl);

		priv->grid_clients[i] = cl;
	}

	return 0;
}

static int fd6551_get_grid_size(struct i2c_client *client)
{
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	int i, num_digits = 0;

	for (i = FD6551_GRID_0; i <= FD6551_GRID_3; i++) {
		if (priv->grid_clients[i])
			num_digits++;
		else
			break;
	}

	return num_digits;
}

static int fd6551_show_text(struct i2c_client *client)
{
	static SEG7_CONVERSION_MAP(map_seg7, MAP_ASCII7SEG_ALPHANUM);
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	int msg_len, i, ret;

	msg_len = strlen(priv->text);

	mutex_lock(&priv->lock);

	for (i = 0; i < priv->grid_size; i++) {
		char char7_raw, char7;
		int j;

		if (i >= msg_len) {
			priv->display_data[i] = 0;
			continue;
		}

		char7_raw = map_to_seg7(&map_seg7, priv->text[i]);

		for (j = 0, char7 = 0; j < NUM_LED_SEGS; j++) {
			if (char7_raw & BIT(j))
				char7 |= BIT(priv->segment_mapping[j]);
		}

		priv->display_data[i] = char7;
	}

	ret = fd6551_write_display_data(client);

	mutex_unlock(&priv->lock);

	return ret;
}

static ssize_t display_text_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct fd6551_priv *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", priv->text);
}

static ssize_t display_text_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct fd6551_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	int ret, i;

	for (i = 0; i < count && i < priv->grid_size && isprint(buf[i]); i++)
		priv->text[i] = buf[i];

	priv->text[i] = '\0';

	ret = fd6551_show_text(client);
	if (ret < 0)
		return ret;

	return count;
}

static const DEVICE_ATTR_RW(display_text);

static int fd6551_led_set_brightness(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	struct fd6551_led *led = container_of(led_cdev, struct fd6551_led, leddev);
	struct i2c_client *client = led->client;
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	u8 bit = BIT(led->segment);
	int ret;

	mutex_lock(&priv->lock);

	if (brightness == LED_OFF)
		priv->display_data[FD6551_SYMBOLS] &= ~bit;
	else
		priv->display_data[FD6551_SYMBOLS] |= bit;

	ret = fd6551_write_display_data(client);

	mutex_unlock(&priv->lock);

	return ret;
}

static enum led_brightness fd6551_led_get_brightness(struct led_classdev *led_cdev)
{
	struct fd6551_led *led = container_of(led_cdev, struct fd6551_led, leddev);
	struct i2c_client *client = led->client;
	struct fd6551_priv *priv = i2c_get_clientdata(client);
	u8 bit = BIT(led->segment);
	bool on;

	mutex_lock(&priv->lock);
	on = priv->display_data[FD6551_SYMBOLS] & bit;
	mutex_unlock(&priv->lock);

	return on ? LED_ON : LED_OFF;
}

static int fd6551_register_led(struct i2c_client *client, struct fwnode_handle *node,
			       u8 segment, struct fd6551_led *led)
{
	struct led_init_data init_data = { .fwnode = node };

	led->client = client;
	led->segment = segment;
	led->leddev.max_brightness = LED_ON;
	led->leddev.brightness_set_blocking = fd6551_led_set_brightness;
	led->leddev.brightness_get = fd6551_led_get_brightness;

	return devm_led_classdev_register_ext(&client->dev, &led->leddev, &init_data);
}

static int fd6551_probe(struct i2c_client *client)
{
	struct fwnode_handle *child;
	struct fd6551_priv *priv;
	int i, num_leds, ret;

	num_leds = device_get_child_node_count(&client->dev);

	priv = devm_kzalloc(&client->dev, struct_size(priv, leds, num_leds), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	i2c_set_clientdata(client, priv);

	ret = fd6551_create_grid(client);
	if (ret)
		return ret;

	priv->grid_size = fd6551_get_grid_size(client);

	ret = device_property_read_u8_array(&client->dev, "fudahisi,segment-mapping",
					    priv->segment_mapping, NUM_LED_SEGS);
	if (ret < 0)
		return ret;

	for (i = 0; i < NUM_LED_SEGS; i++) {
		if (priv->segment_mapping[i] >= NUM_LED_SEGS)
			return -EINVAL;
	}

	priv->display_data[0] = 0x66;
	fd6551_write_display_data(client);

	num_leds = 0;

	if (!IS_REACHABLE(CONFIG_LEDS_CLASS))
		goto no_leds;

	device_for_each_child_node(&client->dev, child) {
		u32 reg;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(&client->dev, "Reading %s reg property failed (%d)\n",
				fwnode_get_name(child), ret);
			continue;
		}

		if (reg >= sizeof(u8) * 8) {
			dev_err(&client->dev, "Invalid segment %u at %s\n",
				reg, fwnode_get_name(child));
			continue;
		}

		ret = fd6551_register_led(client, child, reg, priv->leds + num_leds);
		if (ret) {
			dev_err(&client->dev, "Failed to register LED %s (%d)\n",
				fwnode_get_name(child), ret);
			continue;
		}
		num_leds++;
	}

no_leds:
	ret = device_create_file(&client->dev, &dev_attr_display_text);
	if (ret)
		return ret;

	ret = fd6551_display_on(client, true);
	if (ret) {
		device_remove_file(&client->dev, &dev_attr_display_text);
		return ret;
	}

	dev_info(&client->dev, "Set up FD6551 LED controller with %d digits and %d symbols.\n",
		 priv->grid_size, num_leds);

	return 0;
}

static int fd6551_remove(struct i2c_client *client)
{
	device_remove_file(&client->dev, &dev_attr_display_text);

	return fd6551_display_on(client, false);
}

static void fd6551_shutdown(struct i2c_client *client)
{
	fd6551_display_on(client, false);
}

static const struct i2c_device_id fd6551_i2c_ids[] = {
	{ "fd6551" },
	{}
};
MODULE_DEVICE_TABLE(i2c, fd6551_i2c_ids);

static const struct of_device_id fd6551_of_matches[] = {
	{ .compatible = "fudahisi,fd6551" },
	{}
};
MODULE_DEVICE_TABLE(of, fd6551_of_matches);

static struct i2c_driver fd6551_driver = {
	.driver = {
		.name   = "fd6551",
		.of_match_table = fd6551_of_matches,
	},
	.probe_new	= fd6551_probe,
	.remove		= fd6551_remove,
	.shutdown	= fd6551_shutdown,
	.id_table	= fd6551_i2c_ids,
};

module_i2c_driver(fd6551_driver);

MODULE_DESCRIPTION("FD6551 LED controller driver");
MODULE_AUTHOR("Heiner Kallweit <hkallweit1@gmail.com>");
MODULE_LICENSE("GPL");
