#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <asm/mman.h>

MODULE_LICENSE("Dual BSD/GPL");

#define RUNNING_TOTAL_PROC_NAME "running_total"
#define SORTED_LIST_PROC_NAME "sorted_list"
#define MY_PID_PROC_NAME "my_piddo"
#define KERN_BUFFER_MAX 22 // 2^64 has 20 digits, 1 for null terminator, 1 for new line

/* Some suggested state/data structures. */
static spinlock_t state_lock;
static int64_t total = 0;
static struct list_head sorted_list_head;
struct list_head* curr_list_head = NULL;
struct sorted_node {
  int64_t val;
  struct list_head list;
};

static struct proc_dir_entry * running_total_entry;
static struct proc_dir_entry * sorted_list_entry;
static struct proc_dir_entry * my_pid_entry;
/**
 * Hook to read the running total.
 *
 * This function should copy the value of the running total into the
 * user-level buffer provided.
 *
 * It retuns the number of bytes written into the buffer.
 *
 * It should also adjust *offp to reflect the offset into the file.
 *
 * @filp is the file handle.  You probably won't need to use this.
 * @buffer is the user-level buffer.
 * @count is the size of the user-level buffer.  You will never copy more
 *        than count bytes into bufffer.
 * @offp is a pointer to an unsigned int that stores the offset into the "file"
 */
static ssize_t proc_running_total_read(struct file *filp, char __user *buffer,
				       size_t count, loff_t *offp)
{
	int ret = 0;
	int num_bytes;

	// Signal end-of-file (EOF) if the offset is not zero
	// This is a little hacky, since we are assuming we will always
	// be able to return the entire contents of the "file" in one read()
	// system call.  This is true for a single number, but may not be
	// true in general.

	// It seems that we are assuming that every time we call cat, the offp argument will always be 0?
	// This makes sense if cat supplies its own offset variable and it starts reading at 0.
	// Every time cat calls read, the offset will be updated.
	// Just that in our case, we only allow it to read one time no matter what.
	// ASK IN OFFICE HOUR
	if (*offp) return 0;

	// Exercise 1: Your code here
	spin_lock(&state_lock);
	int64_t local_total = total;
	spin_unlock(&state_lock);

	char* kern_buffer = kmalloc(KERN_BUFFER_MAX, GFP_KERNEL);
	num_bytes = sprintf(kern_buffer, "%lld\n", local_total);
	// if(num_bytes < 0) {
	// 	return num_bytes;
	// }
		
	ret = copy_to_user(buffer, kern_buffer, num_bytes);
	// if(ret) { 
	// 	return ret;
	// }

	*offp = 1; // Assuming it always finishes in one call. What if it does not? I don't care!
	kfree(kern_buffer);
	return num_bytes;
}

/**
 * Hook to update the running total.
 *
 * The user writes an integer (as a string) into the
 * proc file, which should be converted to an integer and added to the running total.
 *
 * It retuns the number of bytes "written" to the integer, or, the
 * number of bytes "consumed" in the conversion to an integer.
 *
 * It should also adjust *offp to reflect the offset into the file.
 *
 * For this exercise, one call should consume all of the bytes in the
 * common case.
 *
 * Finally, you will probably get subsequent calls with a non-zero offset;
 * you can just return zero when this happens (i.e., EOF).
 *
 * @filp is the file handle.  You probably won't need to use this.
 * @buffer is the user-level buffer.
 * @count is the size of the user-level buffer.  You will never copy more
 *        than count bytes from the bufffer.
 * @offp is a pointer to an unsigned int that stores the offset into the "file"
 */
static ssize_t proc_running_total_write(struct file *file,
					const char __user *buffer,
					size_t count, loff_t *offp)
{
	// Signal EOF if the offset is not zero
	// This is different from cat. We have to return count because echo gonna keep calling write until we return count.
	// Which has to relate to the logic how this pipe writing works.
	if (*offp) return 0;

	// Exercise 1: Your code here:
	//
	// As with the read hook, allocate a kernel buffer, and copy
	// the data from userspace before trying to convert to an
	// integer.  Then add the integer to the running total.
	//
	// Hints: copy_from_user and sscanf may be helpful.

	// Here we can assume that, in the absence of errors,
	// we consume all the bytes written in one call.
	// Then, use copy_to_user to copy the contents of
	// the buffer to the user-provided buffer.  In general,
	// kernel code ought not work directly on user memory,
	// which could be a bad pointer or changed by another user thread.
	char* kern_buffer_user = kmalloc(KERN_BUFFER_MAX, GFP_KERNEL);
	int ret = copy_from_user(kern_buffer_user, buffer, count);
	if(ret < 0)
		;

	int64_t user_num;
	sscanf(kern_buffer_user, "%lld", &user_num);

	spin_lock(&state_lock);
	total = user_num + total;
	spin_unlock(&state_lock);

	*offp = 1;
	kfree(kern_buffer_user);
	return count;
}

/**
 * Hook to read the sorted list
 *
 * This function should copy the value of each entry in the list (with a newline)
 * into the user-level buffer provided, in sorted order.
 *
 * It retuns the number of bytes written into the buffer.
 *
 * It should also adjust *offp to reflect the offset into the file.
 *
 * For simplicity, we can assume everything will be read in one call
 * (i.e., you don't need to worry about a read that doesn't return the
 * entire list - assume that if count bytes are filled, you have
 * returned the whole list.  You should do bounds checks on any buffers,
 * however - if your list is longer than buffer can hold, do not overflow
 * any buffers.
 *
 * @filp is the file handle.  You probably won't need to use this.
 * @buffer is the user-level buffer.
 * @count is the size of the user-level buffer.  You will never copy more
 *        than count bytes into bufffer.
 * @offp is a pointer to an unsigned int that stores the offset into the "file"
 */
static ssize_t proc_sorted_list_read(struct file *filp, char __user *buffer,
				     size_t count, loff_t *offp)
{
	size_t my_count = 0;
	int ret;
	int num_bytes;
	int max_idx = 0;

	// Signal EOF if the offset is not zero
	if (*offp) return 0;

	// Guaranteed less than "count" number of entries in the linked list. 
	// This is using more memory than needed, but this is not absurdly inefficient and is fast.
	int nums[count];
	struct sorted_node* curr_node;
	// Minimizing critical section
	// Actually, I do this because copy_to_user triggers weird error when used inside spin_lock
	// It might be a result of spin_lock setting scheduler to have "ATOMIC" flag??
	spin_lock(&state_lock);
	// list_for_each_entry starts at the the entry pointed to by sorted_list_head
	list_for_each_entry(curr_node, &sorted_list_head, list) {
		nums[max_idx] = curr_node->val;
		max_idx += 1;
	}
	spin_unlock(&state_lock);

	// Preparing User Buffer outside of critical section
	char* kern_buffer = kmalloc(KERN_BUFFER_MAX, GFP_KERNEL);
	for(int i = 0; i < max_idx; i++) {
		num_bytes = sprintf(kern_buffer, "%lld\n", nums[i]);
		if((my_count + num_bytes) > count) {
			num_bytes = my_count + num_bytes - count;
			ret = copy_to_user(buffer + my_count, kern_buffer, num_bytes);
			my_count += num_bytes;
			break;
		}
		ret = copy_to_user(buffer + my_count, kern_buffer, num_bytes);
		my_count += num_bytes;
	}
	kfree(kern_buffer);
	*offp = 1;
	return my_count;

	// Exercise 2: Your code here.
	//
	// Much of the code from proc_running_total_read() can be
	// copied here.  Instead of writing a single integer into the
	// user provided buffer, you will write a series of integers.
	//
	// You may find the helper functions in include/linux/list.h helpful,
	// such as list_for_each_entry().
	//
	// As before, be cognizant of concurrency and use a lock
}

/**
 * Hook to add a number to the sorted list.
 *
 * The user writes an integer (as a string) into the
 * proc file, which should be converted to an integer and added to the
 * list of integers in the right place.
 *
 * Duplicates are allowed.
 *
 * It retuns the number of bytes "written" to the integer, or, the
 * number of bytes "consumed" in the conversion to an integer.
 *
 * It should also adjust *offp to reflect the offset into the file.
 *
 * For this exercise, one call should consume all of the bytes in the
 * common case.
 *
 * Finally, you will probably get subsequent calls with a non-zero offset;
 * you can just return zero when this happens (i.e., EOF).
 *
 * @filp is the file handle.  You probably won't need to use this.
 * @buffer is the user-level buffer.
 * @count is the size of the user-level buffer.  You will never copy more
 *        than count bytes from the bufffer.
 * @offp is a pointer to an unsigned int that stores the offset into the "file"
 */
static ssize_t proc_sorted_list_write(struct file *file,
				      const char __user *buffer,
				      size_t count, loff_t *offp)
{
	// Signal EOF if the offset is not zero
	if (*offp) return 0;
	
	// Create new Node
	char* kern_buffer_user = kmalloc(KERN_BUFFER_MAX, GFP_KERNEL);
	struct sorted_node* new_node = kmalloc(sizeof(struct sorted_node), GFP_KERNEL);
	int ret = copy_from_user(kern_buffer_user, buffer, count);
	if(ret < 0){;}

	int64_t user_num;
	sscanf(kern_buffer_user, "%lld", &user_num);
	new_node->val = user_num;

	// List operations do not block, so they are safe inside a critical section.
	
	// Deal with Insert
	struct sorted_node* curr_node;
	// list_for_each_entry starts at the the entry pointed to by the supplied list_head
	spin_lock(&state_lock);
	if(list_empty(&sorted_list_head)){ 
		list_add(&(new_node->list), &sorted_list_head);
	} else {
		list_for_each_entry(curr_node, &sorted_list_head, list) {
			int curr_val = curr_node->val;
			if(curr_val >= new_node->val) { 
				list_add_tail(&(new_node->list), &(curr_node->list));
				break;
			}
			if((curr_node->list).next == &sorted_list_head) {
				// not safe i know
				list_add(&(new_node->list), &(curr_node->list));
				break;
			}	
		}
	}
	spin_unlock(&state_lock);

	kfree(kern_buffer_user);
	*offp = 1;
	return count;
	// Exercise 2: Your code here.
	//
	// Much of the code from proc_running_total_write() can be
	// copied here.  Just like before, you will read a single integer in,
	// but rather than add it to a total, you will place it in the list,
	// in the correct position in the list.
	//
	// You may find the helper functions in include/linux/list.h helpful,
	// such as list_for_each_entry() and list_add_tail().
	//
	// As before, be cognizant of concurrency and use a lock
}

/* Helper code borrowed form Linux because it is not exported as a
 * symbol; this is a brittle practice and could break on a different kernel version. */
static void set_pid_nr_ns(struct pid *pid, struct pid_namespace *ns, pid_t new_pid)
{
	struct upid *upid;

	if (pid && ns->level <= pid->level) {
		upid = &pid->numbers[ns->level];
		if (upid->ns == ns)
		  upid->nr = new_pid;
	} else {
	  printk(KERN_ERR "Failed to set pid...\n");
	}
}

/* Also stolen from Linux */
static struct pid **task_pid_ptr(struct task_struct *task, enum pid_type type)
{
	return (type == PIDTYPE_PID) ?
		&task->thread_pid :
		&task->signal->pids[type];
}

/* Also heavily inspired by linux code */
static void my_change_pid(struct task_struct *task, pid_t new_pid)
{
	struct pid **pid_ptr;
	struct pid_namespace *ns;
	rcu_read_lock();
	ns = task_active_pid_ns(current);
	pid_ptr = task_pid_ptr(task, PIDTYPE_TGID);
	set_pid_nr_ns(rcu_dereference(*pid_ptr), ns, new_pid);
	rcu_read_unlock();
}

/**
 * User writes an integer in; set the uid field of the task struct to
 * this value.  This is not secure in any way - it basically bypasses
 * permission/password/authentication checks for su/login/ssh.
 *
 * The user writes an integer (as a string) into the
 * proc file, which should be converted to an integer.  We provide
 * helper functions to modify the task struct; you will use my_change_pid().
 *
 * It retuns the number of bytes "written", storing the integer, or, the
 * number of bytes "consumed" in the conversion to an integer.
 *
 * It should also adjust *offp to reflect the offset into the file.
 *
 * For this exercise, one call should consume all of the bytes in the
 * common case.
 *
 * Finally, you will probably get subsequent calls with a non-zero offset;
 * you can just return zero when this happens (i.e., EOF).
 *
 * @filp is the file handle.  You probably won't need to use this.
 * @buffer is the user-level buffer.
 * @count is the size of the user-level buffer.  You will never copy more
 *        than count bytes from the bufffer.
 * @offp is a pointer to an unsigned int that stores the offset into the "file"
 */
static ssize_t proc_my_pid_write(struct file *file,
				 const char __user *buffer,
				 size_t count, loff_t *offp)
{
	// Signal EOF if the offset is not zero
	if (*offp) return 0;

	// Execise 3: Your code here.  Again, this can be largely copied and pasted.
	// The main difference is using my_change_pid().
	char* kern_buffer_user = kmalloc(KERN_BUFFER_MAX, GFP_KERNEL);
	int ret = copy_from_user(kern_buffer_user, buffer, count);
	if(ret < 0)
		;

	int64_t user_num;
	sscanf(kern_buffer_user, "%lld", &user_num);

	// spin_lock(&state_lock);
	my_change_pid(current, user_num);
	// spin_unlock(&state_lock);

	*offp = 1;
	kfree(kern_buffer_user);
	// Silence compiler warnings;  eventually delete this line
	
	return count;
}


const struct proc_ops running_total_proc_ops = {
	.proc_read = proc_running_total_read,
	.proc_write = proc_running_total_write,
};


const struct proc_ops sorted_list_proc_ops = {
	.proc_read = proc_sorted_list_read,
	.proc_write = proc_sorted_list_write,
};

const struct proc_ops my_pid_proc_ops = {
	.proc_write = proc_my_pid_write,
};


/*
 * If an init function is provided, an exit function must also be provided
 * to allow module unload.
 */
static int init_630hax(void)
{
	int rv = 0;

	spin_lock_init(&state_lock);
	total = 0;
	INIT_LIST_HEAD(&sorted_list_head);
	/* create proc file */
	running_total_entry = proc_create(RUNNING_TOTAL_PROC_NAME, 0666, NULL,
					  &running_total_proc_ops);
	if (running_total_entry == NULL) {
		remove_proc_entry(RUNNING_TOTAL_PROC_NAME, NULL);
		printk(KERN_ALERT "Failed to initialize running total procfile: %s\n", RUNNING_TOTAL_PROC_NAME);
		return -ENOMEM;
	}
	printk(KERN_ALERT "running total procfs entry created\n");

	/* create proc file */
	sorted_list_entry = proc_create(SORTED_LIST_PROC_NAME, 0666, NULL,
					&sorted_list_proc_ops);
	if (sorted_list_entry == NULL) {
		remove_proc_entry(SORTED_LIST_PROC_NAME, NULL);
		printk(KERN_ALERT "Failed to initialize sorted list procfile: %s\n", RUNNING_TOTAL_PROC_NAME);
		return -ENOMEM;
	}
	printk(KERN_ALERT "sorted list procfs entry created\n");

	my_pid_entry = proc_create(MY_PID_PROC_NAME, 0222, NULL,
					&my_pid_proc_ops);
	if (my_pid_entry == NULL) {
		remove_proc_entry(MY_PID_PROC_NAME, NULL);
		printk(KERN_ALERT "Failed to initialize sorted list procfile: %s\n", RUNNING_TOTAL_PROC_NAME);
		return -ENOMEM;
	}
	printk(KERN_ALERT "my uid procfs entry created\n");


	printk(KERN_ALERT "Hello, comp 630 world\n");
	return rv;
}

static void exit_630hax(void)
{
	printk(KERN_ALERT "Goodbye, comp 630 world\n");

	/* remove proc files */
	remove_proc_entry(RUNNING_TOTAL_PROC_NAME, NULL);
	printk(KERN_ALERT "running total procfs entry removed\n");

	remove_proc_entry(SORTED_LIST_PROC_NAME, NULL);
	printk(KERN_ALERT "sorted list procfs entry removed\n");

	remove_proc_entry(MY_PID_PROC_NAME, NULL);
	printk(KERN_ALERT "my uid procfs entry removed\n");

	// Exercise 2: Your code here: Free list nodes
	while(!list_empty(&sorted_list_head)){
		struct sorted_node* entry = list_first_entry(&sorted_list_head, struct sorted_node, list);
		list_del(&(entry->list));
		kfree(entry);
	}
}

module_init(init_630hax);
module_exit(exit_630hax);
