/*
phy-aml-usb2.c
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/usb/phy_companion.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include "phy-aml-usb.h"

static int amlogic_usb2_init(struct usb_phy *x)
{
	int time_dly = 500; //usec
	struct amlogic_usb *phy = phy_to_amlusb(x);
	int i;

	u2p_aml_regs_t *u2p_aml_regs;
	u2p_r0_t reg0;

	amlogic_usbphy_reset();

	for(i=0;i<phy->portnum;i++)
	{
		u2p_aml_regs = (u2p_aml_regs_t *)((unsigned int)phy->regs+i*PHY_REGISTER_SIZE);

		reg0.d32 = u2p_aml_regs->u2p_r0;
		reg0.b.por = 1;
		reg0.b.dmpulldown = 1;
		reg0.b.dppulldown =1;
		u2p_aml_regs->u2p_r0 = reg0.d32;

		udelay(time_dly);

		reg0.d32 = u2p_aml_regs->u2p_r0;
		reg0.b.por = 0;
		u2p_aml_regs->u2p_r0 = reg0.d32;		
	}

	return 0;
}

static int amlogic_usb2_suspend(struct usb_phy *x, int suspend)
{
#if 0
	u32 ret;
	struct omap_usb *phy = phy_to_omapusb(x);

	if (suspend && !phy->is_suspended) {
		omap_control_usb_phy_power(phy->control_dev, 0);
		pm_runtime_put_sync(phy->dev);
		phy->is_suspended = 1;
	} else if (!suspend && phy->is_suspended) {
		ret = pm_runtime_get_sync(phy->dev);
		if (ret < 0) {
			dev_err(phy->dev, "get_sync failed with err %d\n",
									ret);
			return ret;
		}
		omap_control_usb_phy_power(phy->control_dev, 1);
		phy->is_suspended = 0;
	}
#endif
	return 0;
}

static int amlogic_usb2_probe(struct platform_device *pdev)
{
	struct amlogic_usb			*phy;
//	struct usb_otg			*otg;
	struct device *dev = &pdev->dev;
	struct resource *phy_mem;
	void __iomem	*phy_base;
	int portnum=0;
	const void *prop;

	prop = of_get_property(dev->of_node, "portnum", NULL);
	if(prop)
		portnum = of_read_ulong(prop,1);

	if(!portnum)
	{
		dev_err(&pdev->dev, "This phy has no usb port\n");
		return -ENOMEM;
	}
	
	phy_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy_base = devm_ioremap_resource(dev, phy_mem);
	if (IS_ERR(phy_base))
		return PTR_ERR(phy_base);
	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		dev_err(&pdev->dev, "unable to allocate memory for USB2 PHY\n");
		return -ENOMEM;
	}

	printk(KERN_INFO"USB2 phy probe:phy_mem:0x%8x, iomap phy_base:0x%8x\n",
		               (unsigned int)phy_mem->start,(unsigned int)phy_base);
	
//	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
//	if (!otg) {
//		dev_err(&pdev->dev, "unable to allocate memory for USB OTG\n");
//		return -ENOMEM;
//	}

	phy->dev		= dev;
	phy->regs		= phy_base;
	phy->portnum      = portnum;
	phy->phy.dev		= phy->dev;
	phy->phy.label		= "amlogic-usbphy2";
	phy->phy.init		= amlogic_usb2_init;
	phy->phy.set_suspend	= amlogic_usb2_suspend;
//	phy->phy.otg		= otg;
	phy->phy.type		= USB_PHY_TYPE_USB2;

//	otg->set_host		= omap_usb_set_host;
//	otg->set_peripheral	= omap_usb_set_peripheral;
//	otg->set_vbus		= omap_usb_set_vbus;
//	otg->start_srp		= omap_usb_start_srp;
//	otg->phy		= &phy->phy;

	usb_add_phy_dev(&phy->phy);

	platform_set_drvdata(pdev, phy);

	pm_runtime_enable(phy->dev);

	return 0;
}

static int amlogic_usb2_remove(struct platform_device *pdev)
{
#if 0
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_unprepare(phy->wkupclk);
	if (!IS_ERR(phy->optclk))
		clk_unprepare(phy->optclk);
	usb_remove_phy(&phy->phy);
#endif
	return 0;
}

#ifdef CONFIG_PM_RUNTIME

static int amlogic_usb2_runtime_suspend(struct device *dev)
{
#if 0
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_disable(phy->wkupclk);
	if (!IS_ERR(phy->optclk))
		clk_disable(phy->optclk);
#endif
	return 0;
}

static int amlogic_usb2_runtime_resume(struct device *dev)
{
	unsigned ret = 0;
#if 0
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	ret = clk_enable(phy->wkupclk);
	if (ret < 0) {
		dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);
		goto err0;
	}

	if (!IS_ERR(phy->optclk)) {
		ret = clk_enable(phy->optclk);
		if (ret < 0) {
			dev_err(phy->dev, "Failed to enable optclk %d\n", ret);
			goto err1;
		}
	}

	return 0;

err1:
	clk_disable(phy->wkupclk);

err0:
#endif
	return ret;
}

static const struct dev_pm_ops amlogic_usb2_pm_ops = {
	SET_RUNTIME_PM_OPS(amlogic_usb2_runtime_suspend, amlogic_usb2_runtime_resume,
		NULL)
};

#define DEV_PM_OPS     (&amlogic_usb2_pm_ops)
#else
#define DEV_PM_OPS     NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id amlogic_usb2_id_table[] = {
	{ .compatible = "amlogic,amlogic-usb2" },
	{}
};
MODULE_DEVICE_TABLE(of, amlogic_usb2_id_table);
#endif

static struct platform_driver amlogic_usb2_driver = {
	.probe		= amlogic_usb2_probe,
	.remove		= amlogic_usb2_remove,
	.driver		= {
		.name	= "amlogic-usb2",
		.owner	= THIS_MODULE,
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(amlogic_usb2_id_table),
	},
};

module_platform_driver(amlogic_usb2_driver);

MODULE_ALIAS("platform: amlogic_usb2");
MODULE_AUTHOR("Amlogic Inc.");
MODULE_DESCRIPTION("amlogic USB2 phy driver");
MODULE_LICENSE("GPL v2");
