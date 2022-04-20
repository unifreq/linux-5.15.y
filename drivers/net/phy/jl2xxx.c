/*
 * drivers/net/phy/jlsemi.c
 *
 * Driver for JLSemi PHYs
 *
 * Author: Gangqiao Kuang <gqkuang@jlsemi.com>
 *
 * Copyright (c) 2021 JingLue Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include "jl2xxx-core.h"
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/netdevice.h>


MODULE_DESCRIPTION("JLSemi PHY driver");
MODULE_AUTHOR("Gangqiao Kuang");
MODULE_LICENSE("GPL");

static int jlsemi_probe(struct phy_device *phydev)
{
	int err;

	err = jl2xxx_pre_init(phydev);

	/* wait load complete*/
	msleep(10);

	return (err < 0) ? err : 0;
}

static int jlsemi_config_aneg(struct phy_device *phydev)
{
	return genphy_config_aneg(phydev);
}

static int jlsemi_config_init(struct phy_device *phydev)
{
	return 0;
}

static int jlsemi_read_status(struct phy_device *phydev)
{
	return genphy_read_status(phydev);
}

static int jlsemi_suspend(struct phy_device *phydev)
{
	return genphy_suspend(phydev);
}

static int jlsemi_resume(struct phy_device *phydev)
{
	return genphy_resume(phydev);
}

#if JLSEMI_WOL_EN
static void jlsemi_get_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	int wol_en;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	wol_en = jlsemi_get_bit(phydev, WOL_CTL_PAGE,
				WOL_CTL_REG, WOL_EN);

	if (wol_en)
		wol->wolopts |= WAKE_MAGIC;
}

static int jlsemi_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int err;

	if (wol->wolopts & WAKE_MAGIC) {
		err = enable_wol(phydev);
		if (err < 0)
			return err;

		err = clear_wol_event(phydev);
		if (err < 0)
			return err;

		err = setup_wol_high_polarity(phydev);
		if (err < 0)
			return err;

		err = store_mac_addr(phydev);
		if (err < 0)
			return err;
	} else {
		err = disable_wol(phydev);
		if (err < 0)
			return err;

		err = setup_wol_high_polarity(phydev);
		if (err < 0)
			return err;

		err = clear_wol_event(phydev);
		if (err < 0)
			return err;
	}

	return 0;
}
#endif

static void jlsemi_remove(struct phy_device *phydev)
{

}

static struct phy_driver jlsemi_driver[] = {
{
        .phy_id         = JL2XX1_PHY_ID,
        .name           = "JL2xx1 Gigabit Ethernet",
        .phy_id_mask    = JLSEMI_PHY_ID_MASK,

        /* PHY_BASIC_FEATURES */
	.features	= PHY_GBIT_FEATURES,
	.probe		= jlsemi_probe,
	.read_status	= jlsemi_read_status,
	.config_init    = jlsemi_config_init,
        .config_aneg    = jlsemi_config_aneg,
        .suspend        = jlsemi_suspend,
        .resume         = jlsemi_resume,
	.remove		= jlsemi_remove,

	#if JLSEMI_WOL_EN
	.get_wol	= jlsemi_get_wol,
	.set_wol	= jlsemi_set_wol,
	#endif
}};

module_jlsemi_driver(jlsemi_driver);

static struct mdio_device_id __maybe_unused jlsemi_tbl[] = {
        { JL2XX1_PHY_ID, JLSEMI_PHY_ID_MASK},
        { }
};

MODULE_DEVICE_TABLE(mdio, jlsemi_tbl);

