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
	{ 0x53363e9, "single_release" },
	{ 0x11b64b24, "seq_lseek" },
	{ 0x223983ec, "seq_read" },
	{ 0x82d822d9, "proc_remove" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0x367b5b62, "proc_create" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xc876a99f, "seq_printf" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x10ea38cd, "single_open" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x2d3385d3, "system_wq" },
	{ 0x5a921311, "strncmp" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0xa7eedcc4, "call_usermodehelper" },
	{ 0x92997ed8, "_printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "4F62FDF68B56EBB9D235B5D");
