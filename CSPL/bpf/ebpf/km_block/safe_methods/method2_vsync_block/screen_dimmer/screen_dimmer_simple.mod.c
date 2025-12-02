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
	{ 0x4f20b4d4, "param_ops_string" },
	{ 0xdb6ebebf, "sysfs_remove_file_ns" },
	{ 0x4a165127, "kobject_put" },
	{ 0xd7558de5, "sysfs_create_file_ns" },
	{ 0x6fabae87, "kobject_create_and_add" },
	{ 0xaafb0b88, "kernel_kobj" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xf435dd31, "kernel_write" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x92997ed8, "_printk" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x8c8569cb, "kstrtoint" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xf8c7264e, "filp_close" },
	{ 0x9591a9f, "kernel_read" },
	{ 0x98303217, "filp_open" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "1ED5924F53B8624F1574904");
