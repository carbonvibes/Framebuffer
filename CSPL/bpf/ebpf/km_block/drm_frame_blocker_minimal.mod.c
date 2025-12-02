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
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x63026490, "unregister_kprobe" },
	{ 0xfcca5424, "register_kprobe" },
	{ 0xe7a578c, "param_ops_bool" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x124bad4d, "kstrtobool" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x92997ed8, "_printk" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0x67c9aa87, "param_get_bool" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "49D1ABC9AB817DF4D620C83");
