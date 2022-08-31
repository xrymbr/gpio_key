#include <linux/module.h>
#include <linux/poll.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/current.h>




#define GPIO_COUNT 10


static DECLARE_WAIT_QUEUE_HEAD(gpio_key_wait);
static int major = 0;
struct gpio_key{
    int gpio;
    struct gpio_desc*gpiod;
    int flag;
    int irq;
    int minor;
};
struct gpio_key led_desc[GPIO_COUNT];
struct gpio_key key_desc[GPIO_COUNT];
struct class *led_key_class;


/* 环形缓冲区 */
#define BUF_LEN 128
static int g_keys[BUF_LEN];
static int r, w;

#define NEXT_POS(x) ((x+1) % BUF_LEN)



static int is_key_buf_empty(void)
{
	return (r == w);
}

static int is_key_buf_full(void)
{
	return (r == NEXT_POS(w));
}

static void put_key(int key)
{
	if (!is_key_buf_full())
	{
		g_keys[w] = key;
		w = NEXT_POS(w);
	}
}

static int get_key(void)
{
	int key = 0;
	if (!is_key_buf_empty())
	{
		key = g_keys[r];
		r = NEXT_POS(r);
	}
	return key;
}




static int led_key_open(struct inode *inode, struct file *file)
{
    
    int minor = iminor(inode);
    
    gpiod_direction_output(led_desc[minor].gpiod,0);
    gpiod_direction_output(key_desc[0].gpiod,0);
    gpiod_direction_output(key_desc[1].gpiod,0);
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    
    return 0;
}

static ssize_t led_key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
    int err;
    int val,key;
    struct inode*inode = file_inode(file);
    int minor = iminor(inode);

    val = gpiod_get_value(key_desc[0].gpiod);
    
	printk("led_desc key %d %d\n", key_desc[0].minor, val);
    
    wait_event_interruptible(gpio_key_wait,!is_key_buf_empty());
    
    key = get_key();
    err = copy_to_user(buf,&key,4);
    return 4;
}

static ssize_t led_key_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
    int err;
    char status;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    


    err = copy_from_user(&status,buf,1);

    gpiod_set_value(led_desc[minor].gpiod,status);   
    
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    
    return 0;
}

                        
static long led_key_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

    return 0;

}


static const struct file_operations led_key_opr = {
    .owner = THIS_MODULE,
	.open  = led_key_open,
	.read  = led_key_read,
	.write = led_key_write,
	.unlocked_ioctl = led_key_ioctl,
};

static irqreturn_t gpio_key_isr(int irq, void *dev_id)
{
    struct gpio_key *key_desc = dev_id;
    static int sum = 0;
    int val,key;

    
    val = gpiod_get_value(key_desc->gpiod);
	printk("led_desc key %d %d\n", key_desc->minor, val);
    
    if(val == 1)
    {
        sum++;
    }
	printk("led_desc key %d %d\n", key_desc->minor, val);
    switch(sum%2)
    {
        case 0:gpiod_set_value(led_desc[0].gpiod,1);
        break;
        case 1:gpiod_set_value(led_desc[0].gpiod,0);
        break;
    }
    
    key = (key_desc->gpio << 8) | val;
    put_key(key);    
    wake_up_interruptible(&gpio_key_wait);
	printk("led_desc key %d %d\n", key_desc->minor, val);
    return IRQ_HANDLED;
}



static int led_key_probe(struct platform_device *pdev)
{
    int err;
    int minor = 0;
    int led_cnt,key_cnt;
    led_cnt = gpiod_count(&pdev->dev,"led");
    key_cnt = gpiod_count(&pdev->dev,"key");

    major = register_chrdev(0,"lcledkey",&led_key_opr);
	led_key_class = class_create(THIS_MODULE, "led_key_class");
    
    /* led资源获取 */
    for(minor = 0;minor < led_cnt ; minor++)
    {
        led_desc[minor].minor = minor;
        led_desc[minor].gpiod = devm_gpiod_get_index(&pdev->dev,"led",0,GPIOD_ASIS);
      
	    device_create(led_key_class, NULL, MKDEV(major, minor), NULL, "lc_led%d", minor);
        printk("gpio_key_probe gpiod_get %d \n",led_desc[minor].minor);
        
    }

    /* key资源获取 */
    for(minor = 0;minor < key_cnt ; minor++)
    {
        key_desc[minor].minor = minor;
        key_desc[minor].gpiod = devm_gpiod_get_index(&pdev->dev,"key",minor,GPIOD_ASIS);
        key_desc[minor].irq   = gpiod_to_irq(key_desc[minor].gpiod);
        
        printk("gpio_key_probe gpiod_get %d \n",key_desc[minor].minor);
    }

    for(minor = 0;minor < key_cnt ; minor++)
    {
        err = request_irq(key_desc[minor].irq, gpio_key_isr,  IRQF_TRIGGER_RISING , 
                                        "lc_gpio_key", &key_desc[minor]);                                     
    }

    return 0;

}

static int led_key_remove(struct platform_device *pdev)
{

    int minor;
    int led_cnt,key_cnt;
    led_cnt = gpiod_count(&pdev->dev,"led");
    key_cnt = gpiod_count(&pdev->dev,"key");
    
    device_destroy(led_key_class,MKDEV(major, 0));
    class_destroy(led_key_class);
    unregister_chrdev(major, "lcledkey");

    //for(minor = 0;minor < led_cnt ; minor++)
    //{
        gpiod_put(led_desc[0].gpiod);
    //}

    for(minor = 0;minor < key_cnt ; minor++)
    {
        free_irq(key_desc[minor].irq,&key_desc[minor]);        
        gpiod_put(key_desc[minor].gpiod);
    }
    

    return 0;

}


static const struct of_device_id led_key_match[] = {
	{ .compatible = "lcledkey", },
	{}
};

static struct platform_driver led_key_driver = {
	.driver = {
		.name = "lcledkey",
		.of_match_table = led_key_match,
	},
	.probe  = led_key_probe,
	.remove = led_key_remove,
};


static int __init led_key_init(void)
{
    printk("%s %s line %d, there isn't any gpio available\n", __FILE__, __FUNCTION__, __LINE__);    

	return platform_driver_register(&led_key_driver);
} 



static void __exit led_key_exit(void)
{
	platform_driver_unregister(&led_key_driver);
} 



module_init(led_key_init);
module_exit(led_key_exit);
MODULE_LICENSE("GPL");




