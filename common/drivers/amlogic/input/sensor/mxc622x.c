/*
 * Copyright (C) 2012 MEMSIC, Inc.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include	<linux/err.h>
#include	<linux/errno.h>
#include	<linux/delay.h>
#include	<linux/fs.h>
#include	<linux/i2c.h>
#include	<linux/input.h>
#include	<linux/input-polldev.h>
#include	<linux/miscdevice.h>
#include	<linux/uaccess.h>
#include	<linux/slab.h>

#include       <linux/module.h>

#include	<linux/workqueue.h>
#include	<linux/irq.h>
#include	<linux/gpio.h>
#include	<linux/interrupt.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include        <linux/earlysuspend.h>
#endif

#include <linux/sensor/mxc622x.h>
#include <linux/sensor/sensor_common.h>

#define WHOAMI_MXC622X_ACC	0x25	/*	Expctd content for WAI	*/

/*	CONTROL REGISTERS	*/
#define WHO_AM_I		0x08	/*	WhoAmI register		*/

#define	I2C_RETRY_DELAY		5
#define	I2C_RETRIES		5
#define	I2C_AUTO_INCREMENT	0x80

/* RESUME STATE INDICES */

#define	RESUME_ENTRIES		20
#define DEVICE_INFO         "Memsic, MXC622X"
#define DEVICE_INFO_LEN     32

/* end RESUME STATE INDICES */
/*
#define DEBUG
#define MXC622X_DEBUG
*/

#define	MIN_INTERVAL	10
#define	MAX_INTERVAL	100
#define	POLL_INTERVAL	20

struct mxc622x_acc_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
    
	struct mutex lock;
	struct delayed_work input_work;

       int poll_interval;
       int min_interval;
       int max_interval;
       
	int hw_initialized;/* hw_initialized =1 meas init succefull */
	/* hw_working=0 means not tested yet */
	int hw_working;
	atomic_t enabled;
	int on_before_suspend;

	u8 resume_state[RESUME_ENTRIES];

#ifdef CONFIG_HAS_EARLYSUSPEND
        struct early_suspend early_suspend;
#endif
};

/*
 * Because misc devices can not carry a pointer from driver register to
 * open, we keep this global.  This limits the driver to a single instance.
 */
struct mxc622x_acc_data *mxc622x_acc_misc_data;
struct i2c_client      *mxc622x_i2c_client;

static int mxc622x_acc_i2c_read(struct mxc622x_acc_data *acc, u8 * buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg	msgs[] = {
		{
			.addr = acc->client->addr,
			.flags = acc->client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf, },
		{
			.addr = acc->client->addr,
			.flags = (acc->client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = len,
			.buf = buf, },
	};

	do {
		err = i2c_transfer(acc->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&acc->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int mxc622x_acc_i2c_write(struct mxc622x_acc_data *acc, u8 * buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = { { .addr = acc->client->addr,
			.flags = acc->client->flags & I2C_M_TEN,
			.len = len + 1, .buf = buf, }, };
	do {
		err = i2c_transfer(acc->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&acc->client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int mxc622x_acc_hw_init(struct mxc622x_acc_data *acc)
{
	int err = -1;
	u8 buf[7];

	printk(KERN_INFO "%s: hw init start\n", MXC622X_ACC_DEV_NAME);

	buf[0] = WHO_AM_I;
	err = mxc622x_acc_i2c_read(acc, buf, 1);
	if (err < 0)
		goto error_firstread;
	else
		acc->hw_working = 1;
	if ((buf[0] & 0x3F) != WHOAMI_MXC622X_ACC) {
		err = -1; /* choose the right coded error */
		goto error_unknown_device;
	}

	acc->hw_initialized = 1;
	printk(KERN_INFO "%s: hw init done\n", MXC622X_ACC_DEV_NAME);
	return 0;

error_firstread:
	acc->hw_working = 0;
	dev_warn(&acc->client->dev, "Error reading WHO_AM_I: is device "
		"available/working?\n");
	goto error1;
error_unknown_device:
	dev_err(&acc->client->dev,
		"device unknown. Expected: 0x%x,"
		" Replies: 0x%x\n", WHOAMI_MXC622X_ACC, buf[0]);
error1:
	acc->hw_initialized = 0;
	dev_err(&acc->client->dev, "hw init error 0x%x,0x%x: %d\n", buf[0],
			buf[1], err);
	return err;
}

static void mxc622x_acc_device_power_off(struct mxc622x_acc_data *acc)
{
	int err;
	u8 buf[2] = { MXC622X_REG_CTRL, MXC622X_CTRL_PWRDN };

	err = mxc622x_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		dev_err(&acc->client->dev, "soft power off failed: %d\n", err);
}

static int mxc622x_acc_device_power_on(struct mxc622x_acc_data *acc)
{
	int err = -1;
	u8 buf[2] = { MXC622X_REG_CTRL, MXC622X_CTRL_PWRON };

	err = mxc622x_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		dev_err(&acc->client->dev, "soft power on failed: %d\n", err);

	if (!acc->hw_initialized) {
		err = mxc622x_acc_hw_init(acc);
		if (acc->hw_working == 1 && err < 0) {
			mxc622x_acc_device_power_off(acc);
			return err;
		}
	}

	return 0;
}



static int mxc622x_acc_register_read(struct mxc622x_acc_data *acc, u8 *buf,
		u8 reg_address)
{

	int err = -1;
	buf[0] = (reg_address);
	err = mxc622x_acc_i2c_read(acc, buf, 1);
	return err;
}



static int mxc622x_acc_get_acceleration_data(struct mxc622x_acc_data *acc,
		int *xyz)
{
	int err = -1;
	/* Data bytes from hardware x, y */
	u8 acc_data[2];

	acc_data[0] = MXC622X_REG_DATA;
	err = mxc622x_acc_i2c_read(acc, acc_data, 2);

	if (err < 0)
        {
                #ifdef DEBUG
                printk(KERN_INFO "%s I2C read error %d\n", MXC622X_ACC_I2C_NAME, err);
                #endif
		return err;
        }

	xyz[0] = (signed char)acc_data[0];
	xyz[1] = (signed char)acc_data[1];
	xyz[2] = 64;

      #ifdef MXC622X_DEBUG
      printk("x = %d, y = %d\n", xyz[0], xyz[1]);
      #endif

      #ifdef MXC622X_DEBUG

		printk(KERN_INFO "%s read x=%d, y=%d, z=%d\n",
			MXC622X_ACC_DEV_NAME, xyz[0], xyz[1], xyz[2]);
		printk(KERN_INFO "%s poll interval %d\n", MXC622X_ACC_DEV_NAME, acc->poll_interval);

	#endif
	return err;
}

static void mxc622x_acc_report_values(struct mxc622x_acc_data *acc, int *xyz)
{

	aml_sensor_report_acc(acc->client, acc->input_dev, xyz[0], xyz[1], xyz[2]);
}

static int mxc622x_acc_enable(struct mxc622x_acc_data *acc)
{
	int err;

	if (!atomic_cmpxchg(&acc->enabled, 0, 1)) {
		err = mxc622x_acc_device_power_on(acc);
		if (err < 0) {
			atomic_set(&acc->enabled, 0);
			return err;
		}

		schedule_delayed_work(&acc->input_work, msecs_to_jiffies(
				acc->poll_interval));
	}

	return 0;
}

static int mxc622x_acc_disable(struct mxc622x_acc_data *acc)
{
	if (atomic_cmpxchg(&acc->enabled, 1, 0)) {
		cancel_delayed_work_sync(&acc->input_work);
		mxc622x_acc_device_power_off(acc);
	}

	return 0;
}

static int mxc622x_acc_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = mxc622x_acc_misc_data;

	return 0;
}

static long mxc622x_acc_misc_ioctl(struct file *file,unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	//u8 buf[4];
	//u8 mask;
	//u8 reg_address;
	//u8 bit_values;
	int err;
	int interval;
        int xyz[3] = {0};
	struct mxc622x_acc_data *acc = file->private_data;

//	printk(KERN_INFO "%s: %s call with cmd 0x%x and arg 0x%x\n",
//			MXC622X_ACC_DEV_NAME, __func__, cmd, (unsigned int)arg);

	switch (cmd) {
	case MXC622X_ACC_IOCTL_GET_DELAY:
		interval = acc->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;

	case MXC622X_ACC_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1000)
			return -EINVAL;
		if(interval > acc->max_interval)
			interval = acc->max_interval;
		acc->poll_interval = max(interval,
				acc->min_interval);
		break;

	case MXC622X_ACC_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;
		if (interval)
			err = mxc622x_acc_enable(acc);
		else
			err = mxc622x_acc_disable(acc);
		return err;
		break;

	case MXC622X_ACC_IOCTL_GET_ENABLE:
		interval = atomic_read(&acc->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;
		break;
	case MXC622X_ACC_IOCTL_GET_COOR_XYZ:
		err = mxc622x_acc_get_acceleration_data(acc, xyz);
		if (err < 0)
			return err;
		#ifdef DEBUG
		//   printk(KERN_ALERT "%s Get coordinate xyz:[%d, %d, %d]\n",
		//      __func__, xyz[0], xyz[1], xyz[2]);
		#endif
		if (copy_to_user(argp, xyz, sizeof(xyz))) {
			printk(KERN_ERR " %s %d error in copy_to_user \n",
				__func__, __LINE__);
			return -EINVAL;
			}
		break;
	case MXC622X_ACC_IOCTL_GET_CHIP_ID:
	{
		u8 devid = 0;
		u8 devinfo[DEVICE_INFO_LEN] = {0};
		err = mxc622x_acc_register_read(acc, &devid, WHO_AM_I);
		if (err < 0) {
			printk("%s, error read register WHO_AM_I\n", __func__);
			return -EAGAIN;
		}
		sprintf(devinfo, "%s, %#x", DEVICE_INFO, devid);

		if (copy_to_user(argp, devinfo, sizeof(devinfo))) {
			printk("%s error in copy_to_user(IOCTL_GET_CHIP_ID)\n", __func__);
			return -EINVAL;
		}
	}
            break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations mxc622x_acc_misc_fops = {
        .owner = THIS_MODULE,
        .open = mxc622x_acc_misc_open,
        .unlocked_ioctl = mxc622x_acc_misc_ioctl,
};

static struct miscdevice mxc622x_acc_misc_device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = MXC622X_ACC_DEV_NAME,
        .fops = &mxc622x_acc_misc_fops,
};/* misc deive valite*/


static ssize_t mxc622x_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE,  "%d\n", atomic_read(&acc->enabled));
}

static ssize_t mxc622x_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long data;
	int error;

	struct i2c_client *client = to_i2c_client(dev);
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
    	if (error) {
            printk(KERN_ERR "%s: strict_strtoul failed, error=0x%x\n", __func__, error);
            return error;
	}
       data = !!data;
	if (data) 
            mxc622x_acc_enable(acc);
       else
            mxc622x_acc_disable(acc);

	return count;
}

static ssize_t mxc622x_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE,  "%d\n", acc->poll_interval);
}

static ssize_t mxc622x_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long delay;
	int error;

	struct i2c_client *client = to_i2c_client(dev);
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &delay);
	if (error) {
            printk(KERN_ERR "%s: strict_strtoul failed, error=0x%x\n", __func__, error);
            return error;
	}

	acc->poll_interval = (delay > acc->max_interval) ? acc->max_interval : delay;
       acc->poll_interval = max(acc->poll_interval, acc->min_interval);
    
	return count;
}

static DEVICE_ATTR(enable, 0666, mxc622x_enable_show, mxc622x_enable_store);
static DEVICE_ATTR(delay, 0666, mxc622x_delay_show, mxc622x_delay_store);

static struct attribute *mxc622x_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_delay.attr,
	NULL
};

static struct attribute_group mxc622x_attribute_group = {
	.attrs = mxc622x_attributes,
};

static void mxc622x_acc_input_work_func(struct work_struct *work)
{
	struct mxc622x_acc_data *acc;

	int xyz[3] = { 0 };
	int err;

	acc = container_of((struct delayed_work *)work,
			struct mxc622x_acc_data,	input_work);

	mutex_lock(&acc->lock);
	err = mxc622x_acc_get_acceleration_data(acc, xyz);
	if (err < 0)
		dev_err(&acc->client->dev, "get_acceleration_data failed\n");
	else
		mxc622x_acc_report_values(acc, xyz);

	schedule_delayed_work(&acc->input_work, msecs_to_jiffies(
			acc->poll_interval));
	mutex_unlock(&acc->lock);
}

#ifdef MXC622X_OPEN_ENABLE
int mxc622x_acc_input_open(struct input_dev *input)
{
	struct mxc622x_acc_data *acc = input_get_drvdata(input);

	return mxc622x_acc_enable(acc);
}

void mxc622x_acc_input_close(struct input_dev *dev)
{
	struct mxc622x_acc_data *acc = input_get_drvdata(dev);

	mxc622x_acc_disable(acc);
}
#endif


static int mxc622x_acc_input_init(struct mxc622x_acc_data *acc)
{
	int err;
    // Polling rx data when the interrupt is not used.
    if (1/*acc->irq1 == 0 && acc->irq1 == 0*/) {
		INIT_DELAYED_WORK(&acc->input_work, mxc622x_acc_input_work_func);
    }

	acc->input_dev = input_allocate_device();
	if (!acc->input_dev) {
		err = -ENOMEM;
		dev_err(&acc->client->dev, "input device allocate failed\n");
		goto err0;
	}

#ifdef MXC622X_ACC_OPEN_ENABLE
	acc->input_dev->open = mxc622x_acc_input_open;
	acc->input_dev->close = mxc622x_acc_input_close;
#endif

	input_set_drvdata(acc->input_dev, acc);

	set_bit(EV_ABS, acc->input_dev->evbit);

	input_set_abs_params(acc->input_dev, ABS_X, -32768*4, 32768*4, 0, 0);
	input_set_abs_params(acc->input_dev, ABS_Y, -32768*4, 32768*4, 0, 0);
	input_set_abs_params(acc->input_dev, ABS_Z, -32768*4, 32768*4, 0, 0);

	acc->input_dev->name = MXC622X_ACC_DEV_NAME;

	err = input_register_device(acc->input_dev);
	if (err) {
		dev_err(&acc->client->dev,
				"unable to register input polled device %s\n",
				acc->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(acc->input_dev);
err0:
	return err;
}

static void mxc622x_acc_input_cleanup(struct mxc622x_acc_data *acc)
{
	input_unregister_device(acc->input_dev);
	input_free_device(acc->input_dev);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mxc622x_early_suspend (struct early_suspend* es);
static void mxc622x_early_resume (struct early_suspend* es);
#endif

static int mxc622x_acc_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{

	struct mxc622x_acc_data *acc;

	int err = -1;
	int result = -1;
	int tempvalue;

	struct i2c_adapter *adapter;

	pr_info("%s: probe start.\n", MXC622X_ACC_DEV_NAME);

	adapter = to_i2c_adapter(client->dev.parent);
	result = i2c_check_functionality(adapter,
					 I2C_FUNC_SMBUS_BYTE |
					 I2C_FUNC_SMBUS_BYTE_DATA);
	if (!result)
		goto exit_check_functionality_failed;
	/*
	 * OK. From now, we presume we have a valid client. We now create the
	 * client structure, even though we cannot fill it completely yet.
	 */

	acc = kzalloc(sizeof(struct mxc622x_acc_data), GFP_KERNEL);
	if (acc == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto exit_alloc_data_failed;
	}

	mutex_init(&acc->lock);
	mutex_lock(&acc->lock);

	acc->client = client;
       mxc622x_i2c_client = client;
	i2c_set_clientdata(client, acc);

	/* read chip id */
	tempvalue = i2c_smbus_read_word_data(client, WHO_AM_I);

	if ((tempvalue & 0x003F) == WHOAMI_MXC622X_ACC) {
		printk(KERN_INFO "%s I2C driver registered!\n",
							MXC622X_ACC_DEV_NAME);
	} else {
		acc->client = NULL;
		printk(KERN_INFO "I2C driver not registered!"
				" Device unknown 0x%x\n", tempvalue);
		goto err_mutexunlockfreedata;
	}

       acc->min_interval = MIN_INTERVAL;
       acc->max_interval = MAX_INTERVAL;
       acc->poll_interval = POLL_INTERVAL;
	acc->hw_initialized = 0;
       acc->hw_working = 0;

	err = mxc622x_acc_device_power_on(acc);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err_mutexunlockfreedata;
	}

	atomic_set(&acc->enabled, 1);

	err = mxc622x_acc_input_init(acc);
	if (err < 0) {
		dev_err(&client->dev, "input init failed\n");
		goto err_power_off;
	}

	mxc622x_acc_misc_data = acc;
	err = misc_register(&mxc622x_acc_misc_device);/* regist misc*/
	if (err < 0) {
		dev_err(&client->dev,
				"misc MXC622X_ACC_DEV_NAME register failed\n");
		goto err_input_cleanup;
	}

	mxc622x_acc_device_power_off(acc);

	/* As default, do not report information */
	atomic_set(&acc->enabled, 0);
    
	err = sysfs_create_group(&acc->input_dev->dev.kobj, &mxc622x_attribute_group);
	if (err) {
		printk(KERN_ERR "%s: sysfs_create_group failed\n", __func__);
		goto err_misc_unregister;
	}

       acc->on_before_suspend = 0;

 #ifdef CONFIG_HAS_EARLYSUSPEND
       acc->early_suspend.suspend = mxc622x_early_suspend;
       acc->early_suspend.resume  = mxc622x_early_resume;
       acc->early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
       register_early_suspend(&acc->early_suspend);
#endif

	mutex_unlock(&acc->lock);

	dev_info(&client->dev, "%s: probed\n", MXC622X_ACC_DEV_NAME);

	return 0;

err_misc_unregister:
        misc_deregister(&mxc622x_acc_misc_device);
err_input_cleanup:
	mxc622x_acc_input_cleanup(acc);
err_power_off:
	mxc622x_acc_device_power_off(acc);
err_mutexunlockfreedata:
	kfree(acc);
	mutex_unlock(&acc->lock);
	i2c_set_clientdata(client, NULL);
       mxc622x_acc_misc_data = NULL;
exit_alloc_data_failed:
exit_check_functionality_failed:
	printk(KERN_ERR "%s: Driver Init failed\n", MXC622X_ACC_DEV_NAME);
	return err;
}

static int mxc622x_acc_remove(struct i2c_client *client)
{
	/* TODO: revisit ordering here once _probe order is finalized */
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);

       misc_deregister(&mxc622x_acc_misc_device);/* unregist misc */
       mxc622x_acc_input_cleanup(acc);
       mxc622x_acc_device_power_off(acc);
       kfree(acc);

	return 0;
}

static int mxc622x_acc_resume(struct i2c_client *client)
{
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);
#ifdef MXC622X_DEBUG
    printk("%s.\n", __func__);
#endif

	if (acc != NULL && acc->on_before_suspend) {
		acc->on_before_suspend = 0;
		return mxc622x_acc_enable(acc);
	}

	return 0;
}

static int mxc622x_acc_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct mxc622x_acc_data *acc = i2c_get_clientdata(client);
#ifdef MXC622X_DEBUG
    printk("%s.\n", __func__);
#endif
    if (acc != NULL) {
        if (atomic_read(&acc->enabled)) {
		acc->on_before_suspend = 1;
		return mxc622x_acc_disable(acc);
        }
    }
    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

static void mxc622x_early_suspend (struct early_suspend* es)
{
#ifdef MXC622X_DEBUG
    printk("%s.\n", __func__);
#endif
    mxc622x_acc_suspend(mxc622x_i2c_client,
         (pm_message_t){.event=0});
}

static void mxc622x_early_resume (struct early_suspend* es)
{
#ifdef MXC622X_DEBUG
    printk("%s.\n", __func__);
#endif
    mxc622x_acc_resume(mxc622x_i2c_client);
}

#endif /* CONFIG_HAS_EARLYSUSPEND */

static const struct i2c_device_id mxc622x_acc_id[]
				= { { MXC622X_ACC_DEV_NAME, 0 }, { }, };

MODULE_DEVICE_TABLE(i2c, mxc622x_acc_id);

static struct i2c_driver mxc622x_acc_driver = {
	.driver = {
            .owner = THIS_MODULE,
            .name = MXC622X_ACC_I2C_NAME,
        },
	.probe = mxc622x_acc_probe,
	.remove = mxc622x_acc_remove,
	.resume = mxc622x_acc_resume,
	.suspend = mxc622x_acc_suspend,
	.id_table = mxc622x_acc_id,
};



static int __init mxc622x_acc_init(void)
{
	int  ret = 0;

	printk(KERN_INFO "%s accelerometer driver: init\n", MXC622X_ACC_I2C_NAME);

	return i2c_add_driver(&mxc622x_acc_driver);

	return ret;
}

static void __exit mxc622x_acc_exit(void)
{
	printk(KERN_INFO "%s accelerometer driver exit\n", MXC622X_ACC_DEV_NAME);
	i2c_del_driver(&mxc622x_acc_driver);
}

MODULE_DESCRIPTION("mxc622x accelerometer misc driver");
MODULE_AUTHOR("Memsic");
MODULE_LICENSE("GPL");

module_init(mxc622x_acc_init);
module_exit(mxc622x_acc_exit);
