# Exposing-Page-Table

# Linux Kernel Patch: EPT (Exposed Page Tables)

## Description

This patch introduces a new subsystem in the Linux kernel called **EPT (Exposed Page Tables)**. Its purpose is to make a process’s page tables accessible from user space in a controlled, read-only manner through the device `/dev/ept`.

In simple terms, it allows a user-space program to inspect how virtual memory is mapped at the page table level, without modifying anything.

---

## How it works

The patch adds a new misc device, `/dev/ept`, which can be memory-mapped by a user-space application. When mapped, each page in the mapping corresponds to page table data associated with a process’s virtual address space.

When the user accesses a page in this mapping, a page fault is triggered. This fault is handled by the kernel through the `ept_fault()` function. The handler walks the page table hierarchy (PGD → P4D → PUD → PMD → PTE) for the target virtual address and identifies the physical page that contains the relevant PTE entries.

That physical page is then mapped into user space. If the page table entry does not exist or is not valid, the kernel maps the zero page instead.

---

## Access model and restrictions

Access to EPT is intentionally limited:

* Only processes with `CAP_SYS_ADMIN` can use it
* The mapping is strictly read-only
* Each process can have only one active EPT mapping
* The mapping is not inherited across fork
* It is excluded from core dumps

These restrictions are meant to reduce the risk of exposing sensitive kernel memory structures.

---

## Keeping mappings consistent

Because page tables change over time, the patch introduces a mechanism to keep the exposed data consistent.

Whenever a page table is modified, the kernel calls `ept_invalidate()`. This function locates the corresponding region in the EPT mapping and invalidates it by unmapping the affected page. The next access from user space will trigger a new fault and refresh the data.

---

## Kernel changes

The patch adds:

* A new source file: `mm/ept.c`
* A new header: `include/linux/ept.h`

It also modifies several memory management paths to integrate with EPT. For example:

* `mm_struct` is extended with a pointer to track the active EPT mapping
* `pte_alloc()` and related functions now receive the faulting address
* `pmd_install()` is modified to trigger invalidation when needed

These changes ensure that EPT stays in sync with the rest of the memory subsystem.

---

## Use cases

This feature is mainly useful for low-level work such as:

* Debugging virtual memory issues
* Studying how the kernel manages page tables
* Building tools that inspect memory mappings in real time
* Research on memory management and performance

It is not intended for general application development.

---

## Limitations

* The interface is read-only; it does not allow modifying page tables
* Huge pages are not handled in this implementation
* There may be performance overhead due to frequent invalidations
* The design is tightly coupled to the Linux kernel internals

---

## Example (conceptual)

```c
int fd = open("/dev/ept", O_RDONLY);
void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, offset);

/* Read a page table entry */
unsigned long pte = ((unsigned long *)map)[index];
```

---

## Summary

EPT provides a controlled way for user space to observe page tables. It exposes internal memory management structures without breaking isolation, making it useful primarily for debugging, analysis, and research.
