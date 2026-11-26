// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/timekeeping.h>

static struct kobject *demo_kobject;
static u64 loaded_at_ns;

/* Wyświetla czas od załadowania modułu jako jedną wartość w milisekundach. */
static ssize_t loaded_ms_show(struct kobject *kobject,
                             struct kobj_attribute *attribute, char *buffer)
{
    u64 elapsed_ns = ktime_get_ns() - loaded_at_ns;

    return sysfs_emit(buffer, "%llu\n",
                      (unsigned long long)div_u64(elapsed_ns, NSEC_PER_MSEC));
}

static struct kobj_attribute loaded_ms_attribute = __ATTR_RO(loaded_ms);

/* Tworzy katalog i atrybut, a przy błędzie zwalnia już utworzony katalog. */
static int __init rpi_sentinel_demo_init(void)
{
    int error;

    loaded_at_ns = ktime_get_ns();
    demo_kobject = kobject_create_and_add("rpi_sentinel_demo", kernel_kobj);
    if (!demo_kobject)
        return -ENOMEM;

    error = sysfs_create_file(demo_kobject, &loaded_ms_attribute.attr);
    if (error) {
        kobject_put(demo_kobject);
        return error;
    }

    pr_info("Załadowano moduł diagnostyczny\n");
    return 0;
}

/* Usuwa atrybut przed zwolnieniem obiektu, aby zakończyć dostęp z userspace. */
static void __exit rpi_sentinel_demo_exit(void)
{
    sysfs_remove_file(demo_kobject, &loaded_ms_attribute.attr);
    kobject_put(demo_kobject);
    pr_info("Usunięto moduł diagnostyczny\n");
}

module_init(rpi_sentinel_demo_init);
module_exit(rpi_sentinel_demo_exit);

MODULE_DESCRIPTION("Odczyt czasu od załadowania modułu przez sysfs");
MODULE_LICENSE("GPL");
