// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/pgtable.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/huge_mm.h>
#include <linux/hugetlb.h>

#include <linux/ept.h>

#define EPT_NAME "ept"

// Compatibility wrappers
#ifndef my_zero_pfn
#define my_zero_pfn() page_to_pfn(ZERO_PAGE(0))
#endif

/**
 * ept_fault - Handle page fault for EPT mapping
 * @vmf: Fault information
 *
 * Maps the physical page containing the PTE for a given virtual address
 * into userspace. Returns zero page if the PTE doesn't exist or is invalid.
 */
static vm_fault_t ept_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long fault_addr = vmf->address;
	unsigned long fault_page = fault_addr & PAGE_MASK;
	unsigned long index;
	unsigned long target_addr;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	unsigned long pfn;
	unsigned long pte_page_vaddr;
	unsigned long vpn_offset;

	if (WARN_ON_ONCE(fault_addr < vma->vm_start ||
			 fault_addr >= vma->vm_end))
		return VM_FAULT_SIGBUS;

	if (!mm)
		return VM_FAULT_SIGBUS;

	/*
	 * Calculate the target virtual address.
	 * vm_pgoff is in PAGE_SIZE units.
	 */
	index = (fault_addr - vma->vm_start) >> PAGE_SHIFT;
	index *= (PAGE_SIZE / sizeof(unsigned long));
	vpn_offset = vma->vm_pgoff * (PAGE_SIZE / sizeof(unsigned long));
	target_addr = (index + vpn_offset) << PAGE_SHIFT;

	/* Walk the page table hierarchy */
	pgd = pgd_offset(mm, target_addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto map_zero;

	p4d = p4d_offset(pgd, target_addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		goto map_zero;

	pud = pud_offset(p4d, target_addr);
	if (pud_none(*pud) || pud_bad(*pud))
		goto map_zero;

#ifdef CONFIG_HUGETLB_PAGE
	if (pud_trans_huge(*pud) || pud_devmap(*pud))
#else
	if (pud_trans_huge(*pud) || pud_devmap(*pud))
#endif
		goto map_zero;

	pmd = pmd_offset(pud, target_addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto map_zero;

#ifdef CONFIG_HUGETLB_PAGE
	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd))
#else
	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd))
#endif
		goto map_zero;

	/*
	 * Get the PTE page address.
	 * Use pte_offset_map() which is safe.
	 */
	ptep = pte_offset_map(pmd, target_addr);
	if (!ptep)
		goto map_zero;

	pte_page_vaddr = (unsigned long)ptep & PAGE_MASK;
	pte_unmap(ptep);

	if (!virt_addr_valid(pte_page_vaddr)) {
		pr_warn_once("EPT: Invalid kernel virtual address for PTE page\n");
		goto map_zero;
	}

	/*
	 * Convert kernel virtual address to PFN.
	 * This is the physical page containing the PTE entries.
	 */
	pfn = __pa(pte_page_vaddr) >> PAGE_SHIFT;

	return vmf_insert_pfn(vma, fault_page, pfn);

map_zero:
	pfn = my_zero_pfn();
	return vmf_insert_pfn(vma, fault_page, pfn);
}

/**
 * ept_close - Cleanup when VMA is closed
 * @vma: VMA being closed
 */
static void ept_close(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm && READ_ONCE(mm->ept_vma) == vma)
		WRITE_ONCE(mm->ept_vma, NULL);
}

static const struct vm_operations_struct ept_vm_ops = {
	.fault = ept_fault,
	.close = ept_close,
};

/**
 * ept_mmap - Map EPT into userspace
 * @file: File being mapped
 * @vma: VMA for the mapping
 */
static int ept_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	vm_flags_clear(vma, VM_WRITE | VM_MAYWRITE);

	if (READ_ONCE(mm->ept_vma))
		return -EBUSY;

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND |
				VM_DONTDUMP | VM_DONTCOPY);

	vma->vm_ops = &ept_vm_ops;
	WRITE_ONCE(mm->ept_vma, vma);

	return 0;
}

static const struct file_operations ept_fops = {
	.owner  = THIS_MODULE,
	.mmap   = ept_mmap,
	.llseek = noop_llseek,
};

static struct miscdevice ept_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = EPT_NAME,
	.fops  = &ept_fops,
	.mode  = 0400,
};

static int __init ept_init(void)
{
	int ret = misc_register(&ept_dev);

	if (ret)
		pr_err(EPT_NAME ": misc_register failed (%d)\n", ret);
	else
		pr_info(EPT_NAME ": registered /dev/%s\n", EPT_NAME);

	return ret;
}

static void __exit ept_exit(void)
{
	misc_deregister(&ept_dev);
	pr_info(EPT_NAME ": unregistered\n");
}

module_init(ept_init);
module_exit(ept_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("User-space exposed flat page table (EPT)");
MODULE_AUTHOR("Your Name");
