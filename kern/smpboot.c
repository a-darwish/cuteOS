/*
 * Multiple-Processor (MP) Initialization
 *
 * Copyright (C) 2009-2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <smpboot.h>
#include <paging.h>
#include <string.h>
#include <apic.h>
#include <idt.h>
#include <pit.h>
#include <mptables.h>
#include <proc.h>
#include <percpu.h>
#include <kmalloc.h>
#include <sched.h>

/*
 * Assembly trampoline code start and end pointers
 */
extern const char trampoline[];
extern const char trampoline_end[];

/*
 * Parameters to be sent to other AP cores.
 */
struct smpboot_params {
	uintptr_t cr3;
	struct idt_descriptor idtr;
	struct gdt_descriptor gdtr;

	/* Unique values for each core */
	char *stack_ptr;
	void *percpu_area_ptr;
} __packed;

/*
 * Validate the manually calculated parameters offsets
 * we're sending to the assembly trampoline code
 */
static inline void smpboot_params_validate_offsets(void)
{
	compiler_assert(SMPBOOT_CR3 ==
	       offsetof(struct smpboot_params, cr3));

	compiler_assert(SMPBOOT_IDTR ==
	       offsetof(struct smpboot_params, idtr));

	compiler_assert(SMPBOOT_IDTR_LIMIT ==
	       offsetof(struct smpboot_params, idtr) +
	       offsetof(struct idt_descriptor, limit));

	compiler_assert(SMPBOOT_IDTR_BASE ==
	       offsetof(struct smpboot_params, idtr) +
	       offsetof(struct idt_descriptor, base));

	compiler_assert(SMPBOOT_GDTR ==
	       offsetof(struct smpboot_params, gdtr));

	compiler_assert(SMPBOOT_GDTR_LIMIT ==
	       offsetof(struct smpboot_params, gdtr) +
	       offsetof(struct gdt_descriptor, limit));

	compiler_assert(SMPBOOT_GDTR_BASE ==
	       offsetof(struct smpboot_params, gdtr) +
	       offsetof(struct gdt_descriptor, base));

	compiler_assert(SMPBOOT_STACK_PTR ==
	       offsetof(struct smpboot_params, stack_ptr));

	compiler_assert(SMPBOOT_PERCPU_PTR ==
	       offsetof(struct smpboot_params, percpu_area_ptr));

	compiler_assert(SMPBOOT_PARAMS_SIZE ==
	       sizeof(struct smpboot_params));
}

/*
 * Number of active CPUs so far: BSC + SIPI-started AP
 * cores that are now verifiably executing kernel code.
 */
static int nr_alive_cpus = 1;

/*
 * Common Inter-Processor Interrupts
 */

/*
 * Zero INIT vector field for "future compatibility".
 */
static inline void send_init_ipi(int apic_id)
{
	apic_send_ipi(apic_id, APIC_DELMOD_INIT, 0);
}

/*
 * ICR's vector field is 8-bits; For the value 0xVV,
 * SIPI target core will start from 0xVV000.
 */
static inline void send_startup_ipi(int apic_id, uint32_t start_vector)
{
	assert(page_aligned(start_vector));
	assert(start_vector >= 0x10000 && start_vector <= 0x90000);

	apic_send_ipi(apic_id, APIC_DELMOD_START, start_vector >> 12);
}

/*
 * "It is up to the software to determine if the SIPI was
 * not successfully delivered and to reissue the SIPI if
 * necessary." --Intel
 */
#define MAX_SIPI_RETRY	3

/*
 * Do not broadcast Intel's INIT-SIPI-SIPI sequence as this
 * may wake-up CPUs marked by the BIOS as faulty, or defeat
 * the user choice of disabing a certain core in BIOS setup.
 *
 * The trampoline code cannot also be executed in parallel.
 *
 * FIXME: 200 micro-second delay between the SIPIs
 * FIXME: fine-grained timeouts using micro-seconds
 */
static int start_secondary_cpu(struct percpu *cpu, struct smpboot_params *params)
{
	int count, acked, timeout, apic_id;

	barrier();
	count = nr_alive_cpus;
	apic_id = cpu->apic_id;

	/*
	 * Personally allocate a 'current' thread descriptor and a stack
	 * for the given CPU. It can't do this by itself since kmalloc()
	 * uses lots of spinlocks, which need an already allocated
	 * 'current' thread descriptor (cyclic dependency.)
	 *
	 * We've statically allocated such structures for the boot core.
	 */

	/* All cores initialize their own 'current'; dont do it for 'em. */
	cpu->__current = kmalloc(sizeof(struct proc));

	params->stack_ptr = kmalloc(STACK_SIZE);
	params->stack_ptr += STACK_SIZE;
	params->percpu_area_ptr = cpu;

	memcpy(TRAMPOLINE_PARAMS, params, sizeof(*params));

	/* INIT: wakeup the core from its deep (IF=0)
	 * halted state and let it wait for the SIPIs */
	send_init_ipi(apic_id);
	acked = apic_ipi_acked();
	if (!acked) {
		printk("SMP: Failed to deliver INIT to CPU#%d\n", apic_id);
		goto fail;
	}

	pit_mdelay(10);

	for (int j = 1; j <= MAX_SIPI_RETRY; j++) {
		send_startup_ipi(apic_id, SMPBOOT_START);
		acked = apic_ipi_acked();
		if (acked)
			break;

		printk("SMP: Failed to deliver SIPI#%d to CPU#%d\n",
		       j, apic_id);

		if (j == MAX_SIPI_RETRY) {
			printk("SMP: Giving-up SIPI delivery\n");
			goto fail;
		}

		printk("SMP: Retrying SIPI delivery\n");
	}

	/* The just-started AP core should now signal us
	 * by incrementing the active-CPUs counter by one */
	timeout = 1000;
	while (timeout-- && count == nr_alive_cpus) {
		barrier();
		pit_mdelay(1);
	}
	if (timeout == -1) {
		printk("SMP: Timeout waiting for CPU#%d to start\n",
		       apic_id);
		goto fail;
	}

	return 0;

fail:
	kfree(cpu->__current);
	kfree(params->stack_ptr - STACK_SIZE);
	return 1;
}

/*
 * @cpu: iterator of type ‘struct percpu *’.
 */
#define for_all_cpus(cpu)			\
	for (cpu = &cpus[0]; cpu != &cpus[mptables_get_nr_cpus()]; cpu++)
#define for_all_cpus_except_bootstrap(cpu)	\
	for (cpu = &cpus[1]; cpu != &cpus[mptables_get_nr_cpus()]; cpu++)

/*
 * AP cores C code entry. We come here from the trampoline,
 * which has assigned us a unique stack, the per-CPU area
 * addr in %gs, and bootstrap's gdt, idt, and page tables.
 */
void __no_return secondary_start(void)
{
	union apic_id id;

	/* Quickly tell the parent we're alive */
	++nr_alive_cpus;

	schedulify_this_code_path(SECONDARY);
	apic_local_regs_init();

	/* Assert validity of our per-CPU area */
	id.raw = apic_read(APIC_ID);
	assert(id.id == percpu_get(apic_id));

	printk("SMP: CPU apic_id=%d started\n", id.id);

	local_irq_enable();
	smpboot_run_tests();
	halt();
}

/*
 * NOTE! This function is called by panic(): it should
 * not include any asserts or panics.
 */
int smpboot_get_nr_alive_cpus(void)
{
	barrier();
	return nr_alive_cpus;
}

void smpboot_init(void)
{
	int nr_cpus;
	struct smpboot_params *params;
	struct percpu *cpu;

	smpboot_params_validate_offsets();

	params = kmalloc(sizeof(*params));
	params->cr3 = get_cr3();
	params->idtr = get_idt();
	params->gdtr = get_gdt();

	nr_cpus = mptables_get_nr_cpus();
	printk("SMP: %d usable CPU(s) found\n", nr_cpus);

	memcpy(TRAMPOLINE_START, trampoline, trampoline_end - trampoline);

	for_all_cpus_except_bootstrap(cpu) {
		if (start_secondary_cpu(cpu, params))
			panic("SMP: Could not start-up all AP cores\n");
	}

	kfree(params);

	barrier();
	assert(nr_alive_cpus == nr_cpus);
}

#if	SCHED_TESTS
#include <vga.h>

static void __no_return test0(void)  { loop_print('G', VGA_LIGHT_GREEN); }
static void __no_return test1(void)  { loop_print('H', VGA_LIGHT_GREEN); }
static void __no_return test2(void)  { loop_print('I', VGA_LIGHT_GREEN); }
static void __no_return test3(void)  { loop_print('J', VGA_LIGHT_MAGNETA); }
static void __no_return test4(void)  { loop_print('K', VGA_LIGHT_MAGNETA); }
static void __no_return test5(void)  { loop_print('L', VGA_LIGHT_MAGNETA); }

void smpboot_run_tests(void)
{
	for (int i = 0; i < 20; i++) {
		kthread_create(test0);
		kthread_create(test1);
		kthread_create(test2);
		kthread_create(test3);
		kthread_create(test4);
		kthread_create(test5);
	}
}

#endif /* SCHED_TESTS */
