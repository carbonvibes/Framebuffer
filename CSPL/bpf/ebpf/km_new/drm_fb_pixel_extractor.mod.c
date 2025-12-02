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
	{ 0xc6c777de, "put_devmap_managed_page" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x10ea38cd, "single_open" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x63026490, "unregister_kprobe" },
	{ 0x53363e9, "single_release" },
	{ 0xce6f2954, "pagecache_get_page" },
	{ 0xfcca5424, "register_kprobe" },
	{ 0xc876a99f, "seq_printf" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x999e8297, "vfree" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x223983ec, "seq_read" },
	{ 0x82d822d9, "proc_remove" },
	{ 0x12bba0a9, "dma_buf_vunmap" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x38c5fdb9, "default_llseek" },
	{ 0x4c9f47a5, "current_task" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x92997ed8, "_printk" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x11b64b24, "seq_lseek" },
	{ 0x69acdf38, "memcpy" },
	{ 0x7d628444, "memcpy_fromio" },
	{ 0x367b5b62, "proc_create" },
	{ 0x26922e7f, "dma_buf_vmap" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xd54fe6d2, "__put_page" },
	{ 0x587f22d7, "devmap_managed_key" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "C4CF98DD458ACEBCF89AB91");
