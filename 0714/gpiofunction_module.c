#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YANG");
MODULE_DESCRIPTION("RaspberryPi led on off dev module");

#define GPIO_MAJOR		200
#define GPIO_MINOR		0
#define GPIO_DEVICE		"gpioLed"
#define GPIO_LED		530

volatile unsigned * gpio;
static char msg[BLOCK_SIZE] = {0};

static int gpio_open(struct inode *, struct file *);
static ssize_t gpio_read(struct file*, char*, size_t, loff_t*);
static ssize_t gpio_write(struct file*, const char*, size_t, loff_t*);
static int gpio_close(struct inode *, struct file*);

static struct file_operations gpio_fops = {
	.owner=THIS_MODULE,
	.read=gpio_read,
	.write=gpio_write,
	.open=gpio_open,
	.release=gpio_close
};

struct cdev gpio_cdev;

int init_module(void){
	dev_t devno;
	unsigned int count;
	int err;

	printk(KERN_INFO "HELLO module!\n");


	devno = MKDEV(GPIO_MAJOR,GPIO_MINOR);
	register_chrdev_region(devno,1,GPIO_DEVICE);

	cdev_init(&gpio_cdev, &gpio_fops);

	gpio_cdev.owner = THIS_MODULE;
	count = 1;
	err = cdev_add(&gpio_cdev, devno,count);
	if(err<0){
		printk("err:dev add");
		return-1;
	}
	
	printk("'mknod /dev/%s c %d 0'\n",GPIO_DEVICE,GPIO_MAJOR);
	printk("'chmod 666 /dev/%s'\n",GPIO_DEVICE);

	gpio_request(GPIO_LED,"LED");
	gpio_direction_output(GPIO_LED,0);

	return 0;
}

void cleanup_module(void){
	dev_t devno = MKDEV(GPIO_MAJOR, GPIO_MINOR);
	unregister_chrdev_region(devno,1);

	cdev_del(&gpio_cdev);

	gpio_free(GPIO_LED);
	gpio_direction_output(GPIO_LED,0);


	printk(KERN_INFO "GoodBye!\n");
}


static int gpio_open(struct inode *inod, struct file *fil){
	printk("gpioDevice opened %d/%d\n",imajor(inod),iminor(inod));
	try_module_get(THIS_MODULE);

	return 0;
}
static int gpio_close(struct inode *inod, struct file*fil){
	printk("GPIO_DEVICE closed(%d)\n",MAJOR(fil->f_path.dentry->d_inode->i_rdev));
	module_put(THIS_MODULE);

	return 0;
}
static ssize_t gpio_read(struct file *inode, char *buff, size_t len, loff_t *off){
	int count;

	strcat(msg,"from kernel");
	count = copy_to_user(buff,msg,strlen(msg)+1);

	printk("gpio %d read %s(%d)\n",MAJOR(inode->f_path.dentry->d_inode->i_rdev),msg,count);

	return count;
}
static ssize_t gpio_write(struct file* inode, const char* buff, size_t len, loff_t*off){
	short count;
	memset(msg,0,BLOCK_SIZE);
	count = copy_from_user(msg,buff,len);

	gpio_set_value(GPIO_LED,(!strcmp(msg,"0"))?0:1);
	printk("gpio %d write %s(%zd)\n",MAJOR(inode->f_path.dentry->d_inode->i_rdev),msg,len);

	return count;
}