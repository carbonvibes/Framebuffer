#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x2c635209, "module_layout" },
	{ 0xae2ae519, "param_ops_int" },
	{ 0x4a165127, "kobject_put" },
	{ 0xdb6ebebf, "sysfs_remove_file_ns" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0xd7558de5, "sysfs_create_file_ns" },
	{ 0x63026490, "unregister_kprobe" },
	{ 0x6fabae87, "kobject_create_and_add" },
	{ 0xaafb0b88, "kernel_kobj" },
	{ 0xfcca5424, "register_kprobe" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0xf9a482f9, "msleep" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x92997ed8, "_printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "D6FC16728CD8A6BA0D20E33");
