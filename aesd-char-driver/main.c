/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

MODULE_AUTHOR("Samuel Gfrörer");
MODULE_LICENSE("Dual BSD/GPL");

int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(
	struct file *filp,
	char __user *buf,
	size_t count,
	loff_t *f_pos
);
ssize_t aesd_write(
		struct file *filp,
		const char __user *buf,
		size_t count,
		loff_t *f_pos
);
static int aesd_setup_cdev(struct aesd_dev *dev);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

struct aesd_dev aesd_device;

struct file_operations aesd_fops = {
	.owner =    THIS_MODULE,
	.read =     aesd_read,
	.write =    aesd_write,
	.open =     aesd_open,
	.release =  aesd_release,
};

int aesd_open(struct inode *inode, struct file *filp)
{
	PDEBUG("open");
	/**
	 * TODO: handle open
	 */
	return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
	PDEBUG("release");
	/**
	 * TODO: handle release
	 */
	return 0;
}

ssize_t aesd_read(
	struct file* filp,
	char __user* buf,
	size_t count,
	loff_t* f_pos
)
{
	ssize_t ret = 0;
	mutex_lock( &aesd_device.lock );
	PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
	size_t offset = 0;
	struct aesd_buffer_entry* entry = aesd_circular_buffer_find_entry_offset_for_fpos(
			&aesd_device.buffer,
			*f_pos,
			&offset
	);
	if( !entry ) {
		ret = 0;
		goto end;
	}
	if( count == 0 ) {
		ret = 0;
		goto end;
	}
	size_t bytes_to_copy = count;
	if( entry->size - offset < bytes_to_copy ) {
		bytes_to_copy = entry->size;
	}
	if( copy_to_user(
			buf,
			&entry->buffptr[offset],
			bytes_to_copy
	) ) {
		ret = -EFAULT;
		goto end;
	}
	(*f_pos) += bytes_to_copy;
	ret = bytes_to_copy;
	goto end;

end:
	PDEBUG("read returns: %ld", ret );
	mutex_unlock( &aesd_device.lock );
	return ret;
}

ssize_t aesd_write(
		struct file *filp,
		const char __user *buf,
		size_t count,
		loff_t *f_pos
)
{
	mutex_lock( &aesd_device.lock );
	PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
	if( count == 0 ) {
		mutex_unlock( &aesd_device.lock );
		return 0;
	}
	// allocate/reallocate buffer entry:
	size_t insert_pos = 0;
	if( aesd_device.current_entry.buffptr == NULL ) {

		aesd_device.current_entry.buffptr = kmalloc(count, GFP_KERNEL );
		aesd_device.current_entry.size = count;
	}
	else {
		insert_pos = aesd_device.current_entry.size;
		aesd_device.current_entry.buffptr = krealloc(
				aesd_device.current_entry.buffptr,
				aesd_device.current_entry.size + count,
				GFP_KERNEL
		);
		aesd_device.current_entry.size += count;
	}
	if( aesd_device.current_entry.buffptr == NULL ) {
		mutex_unlock( &aesd_device.lock );
		return -ENOMEM;
	}
	PDEBUG("write copying to local...");
	// copy to buffer entry:
	if( copy_from_user(
			&aesd_device.current_entry.buffptr[insert_pos],
			buf,
			count
	) ) {
		mutex_unlock( &aesd_device.lock );
		return -EFAULT;
	}
	// copy entry to ringbuffer:
	if( aesd_device.current_entry.buffptr[insert_pos + count - 1] == '\n' ) {
		PDEBUG("write to ringbuffer...");
		if(
				aesd_circular_buffer_get_count( &aesd_device.buffer ) == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
		) {
			struct aesd_buffer_entry* last_entry = &aesd_device.buffer.entry[ aesd_device.buffer.in_offs];
			kfree( last_entry->buffptr );
			last_entry->buffptr = NULL;
			last_entry->size = 0;
		}
		aesd_circular_buffer_add_entry(
				&aesd_device.buffer,
				&aesd_device.current_entry
		);
		aesd_device.current_entry = (struct aesd_buffer_entry){
			.buffptr = NULL,
			.size = 0,
		};
		PDEBUG("write update pos...");
		(*f_pos) += count;
		PDEBUG( "write: f_pos=%lld", *f_pos );
	}
	mutex_unlock( &aesd_device.lock );
	return count;
}

static int aesd_setup_cdev(struct aesd_dev *dev)
{
	int err, devno = MKDEV(aesd_major, aesd_minor);

	cdev_init(&dev->cdev, &aesd_fops);
		dev->cdev.owner = THIS_MODULE;
		dev->cdev.ops = &aesd_fops;
	err = cdev_add (&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "Error %d adding aesd cdev", err);
	}
	return err;
}

int aesd_init_module(void)
{
	dev_t dev = 0;
	int result;
	result = alloc_chrdev_region(
			&dev,
			aesd_minor, 1,
			"aesdchar"
	);
	aesd_major = MAJOR(dev);
	if (result < 0) {
		printk(KERN_WARNING "Can't get major %d\n", aesd_major);
		return result;
	}
	memset(&aesd_device,0,sizeof(struct aesd_dev));

	mutex_init( &aesd_device.lock );
	/**
	 * initialize the AESD specific portion of the device
	 */
	aesd_circular_buffer_init( &aesd_device.buffer );
	aesd_device.current_entry = (struct aesd_buffer_entry ){
		.buffptr = NULL,
		.size = 0,
	};

	result = aesd_setup_cdev(&aesd_device);

	if( result ) {
		unregister_chrdev_region(dev, 1);
	}
	return result;
}

void aesd_cleanup_module(void)
{
	dev_t devno = MKDEV(aesd_major, aesd_minor);

	// cleanup write buffer:
	if( aesd_device.current_entry.buffptr != NULL ) {
		kfree( aesd_device.current_entry.buffptr );
	}
	// cleanup ring buffer:
	for( uint8_t i=0; i<aesd_circular_buffer_get_count( &aesd_device.buffer ); i++ ) {
		struct aesd_buffer_entry* entry = &aesd_device.buffer.entry[
			aesd_device.buffer.out_offs + i
		];
		kfree( entry->buffptr );
		entry->buffptr = NULL;
		entry->size = 0;
	}

	cdev_del(&aesd_device.cdev);

	unregister_chrdev_region(devno, 1);
	mutex_destroy( &aesd_device.lock );
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
