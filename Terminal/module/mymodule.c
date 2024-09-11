#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/cdev.h>

#define DEVICE_NAME "mymodule"
static int device_major_id;
struct class *pclass;
struct device *pdev;
static dev_t dev_no;

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset);
static ssize_t device_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static int device_open(struct inode *inode, struct file *filp);
static int     device_release(struct inode *inode, struct file *filp);
static void cleanup(void);
static int my_uevent(struct device* dev, struct kobj_uevent_env* env);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ME");
MODULE_DESCRIPTION("A module that prints process tree from given PID");

char *name;
int age;

module_param(name, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(name, "Name of the caller");

module_param(age, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(age, "Age of the caller");

static int pid = 1;
module_param(pid, int, 0644);
MODULE_PARM_DESC(pid, "Process ID arg");




static struct kobject *kobj;
char process_tree_buffer[8192];
size_t buffer_offset = 0;

void print_process_tree(struct task_struct *ts, int depth) {
    struct list_head *list;
    struct task_struct *task;
    int len;
    char line[256];
    if (buffer_offset >= sizeof(process_tree_buffer)) {
        return; // Buffer full
    }
    unsigned long long start_time_sec = ts->start_time;
    for(int j = 0;j<9; ++j){
        start_time_sec /= 10;
    }
    //printk(KERN_INFO "StartTime: %llu\n",x);

    memset(line, ' ', depth * 4);
    len = depth * 4; // Initial offset into line for text
    if(depth == 0){
        printk(KERN_INFO"Base case");
        int printed = snprintf(line + len, sizeof(line) - len, "Children pid: %d, Command: %s, Start Time: %d\n",ts->pid, ts->comm, (int)start_time_sec);
        if (printed > 0) {
            size_t full_length = len + printed; // Total length includes both spaces and text
            if (full_length > sizeof(line)) {
                full_length = sizeof(line);
            }
            // Ensure that the whole line, including spaces, is copied to the buffer
            size_t to_copy = min(full_length, sizeof(process_tree_buffer) - buffer_offset);
            strncpy(process_tree_buffer + buffer_offset, line, to_copy);
            buffer_offset += to_copy;
        }
        print_process_tree(ts, depth + 1);
    }
    else{
        list_for_each(list, &ts->children) {
            task = list_entry(list, struct task_struct, sibling);
            if (task) {
                int printed = snprintf(line + len, sizeof(line) - len, "Children pid: %d, Command: %s, Start Time: %d\n",task->pid, task->comm, (int)start_time_sec);
                if (printed > 0) {
                    size_t full_length = len + printed; // Total length includes both spaces and text
                    if (full_length > sizeof(line)) {
                        full_length = sizeof(line);
                    }
                    // Ensure that the whole line, including spaces, is copied to the buffer
                    size_t to_copy = min(full_length, sizeof(process_tree_buffer) - buffer_offset);
                    strncpy(process_tree_buffer + buffer_offset, line, to_copy);
                    buffer_offset += to_copy;
                }
                print_process_tree(task, depth + 1);
            }
        }
    }

}



static int section_two_char_device(void){
    static struct file_operations fops = {
            .read = device_read,
            .write = device_write,
            .open = device_open,
            .release = device_release,
    };

    device_major_id = register_chrdev(0,DEVICE_NAME,&fops);
    if(device_major_id < 0){
        printk(KERN_ALERT "Failed to register char device");
        return 1;
    }
    dev_no = MKDEV(device_major_id, 0);  // Create a dev_t, 32 bit version of numbers
    pclass = class_create(DEVICE_NAME);

    if (IS_ERR(pclass)) {
        printk(KERN_WARNING "HelloWorld cannot create class\n");
        cleanup();
        return -1;
    }

    if (IS_ERR(pdev = device_create(pclass, NULL, dev_no, NULL, DEVICE_NAME))) {
        printk(KERN_WARNING "HelloWorld cannot create device: /dev/%s\n", DEVICE_NAME);
        cleanup();
        return -1;
    }
    printk(KERN_INFO "HelloWorld Successfully registered char device \"%s\" with major id (%d)\n", DEVICE_NAME, device_major_id);

    return 0;

}

static int simple_init(void) {
    struct task_struct *ts;
    struct pid* pid_struct;
    printk(KERN_INFO "Module loaded with the pid %d\n", pid);
    ts = get_pid_task(find_get_pid(pid), PIDTYPE_PID);

    //Setting up character device.
    int err = 0;
    if((err = section_two_char_device()) < 0){
        printk(KERN_ALERT "Failed to create character device");
        return err;
    }

    if (ts) {
        printk(KERN_INFO "Found task with PID %d, command: %s \n", ts->pid, ts->comm);
        print_process_tree(ts, 0);
    } else {
        printk(KERN_INFO "No such task found with PID %d\n", pid);
        return -ESRCH;
    }
    return 0;
}

void simple_exit(void) {
    printk(KERN_INFO "Goodbye from the kernel, user: %s, age: %d\n", name, age);
    cleanup();
}
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset) {

    // Try to run `$ sudo head -c12 /dev/hello` to invoke the device read function
    size_t bytes_to_read = min(length, buffer_offset - *offset);
    printk(KERN_INFO "Read attempt from the user");
    if (bytes_to_read == 0) return 0;

    if (copy_to_user(buffer, process_tree_buffer + *offset, bytes_to_read)) {
        return -EFAULT;
    }

    *offset += bytes_to_read;
    return bytes_to_read;
}
 static ssize_t device_write(struct file *filp, const char *buf, size_t len, loff_t *off) {
     int input_pid;
     struct pid* pid_struct;
     struct task_struct *ts;

     if (len < sizeof(int)) return -EINVAL;

     if (copy_from_user(&input_pid, buf, sizeof(int))) return -EFAULT;

     printk(KERN_INFO "Received PID %d for process tree generation.\n", input_pid);

     pid_struct = find_get_pid(input_pid);
     ts = get_pid_task(pid_struct, PIDTYPE_PID);
     if (!ts) {
         printk(KERN_INFO "No such task found with PID %d\n", input_pid);
         return -ESRCH;
     }

     buffer_offset = 0; // Reset the buffer
     print_process_tree(ts, 0);
     put_pid(pid_struct);

     return len;
}

static int device_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "HelloWorld Character Device: Attempted Open\n");
    return 0;
}


static int device_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "HelloWorld Character Device: Attempted Release/Close\n");
    return 0;
}

static void cleanup(void) {
    device_destroy(pclass, dev_no);
    class_destroy(pclass);
    unregister_chrdev_region(dev_no, 1);
    unregister_chrdev(device_major_id, DEVICE_NAME);
}


module_init(simple_init);
module_exit(simple_exit);
