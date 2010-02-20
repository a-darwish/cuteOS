/*
 * Local APIC configuration
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <segment.h>
#include <msr.h>
#include <apic.h>
#include <pit.h>

/*
 * Local APIC
 */

void apic_init(void)
{
	union apic_tpr tpr = { .value = 0 };
	union apic_lvt_timer timer = { .value = 0 };
	union apic_lvt_thermal thermal = { .value = 0 };
	union apic_lvt_perfc perfc = { .value = 0 };
	union apic_lvt_lint lint0 = { .value = 0 };
	union apic_lvt_lint lint1 = { .value = 0 };
	union apic_spiv spiv = { .value = 0 };

	/* Before doing any apic operation, assure the APIC
	 * registers base address is set as we expect it */
	msr_apicbase_setaddr(APIC_PHBASE);

	/* No complexitis; set the task priority register to
	 * zero: "all interrupts are allowed" */
	tpr.subclass = 0;
	tpr.priority = 0;
	apic_write(APIC_TPR, tpr.value);

	/*
	 * Intialize LVT entries
	 */

	timer.vector = APIC_TIMER_VECTOR;
	timer.mask = APIC_MASK;
	apic_write(APIC_LVTT, timer.value);

	thermal.vector = APIC_THERMAL_VECTOR;
	thermal.mask = APIC_MASK;
	apic_write(APIC_LVTTHER, thermal.value);

	perfc.vector = APIC_PERFC_VECTOR;
	perfc.mask = APIC_MASK;
	apic_write(APIC_LVTPC, perfc.value);

	lint0.vector = 0;
	lint0.trigger = APIC_TRIGGER_EDGE;
	lint0.delivery_mode = APIC_DELMOD_ExtINT;
	lint0.mask = APIC_UNMASK;
	apic_write(APIC_LVT0, lint0.value);

	lint1.vector = 0;
	lint1.trigger = APIC_TRIGGER_EDGE;
	lint1.delivery_mode = APIC_DELMOD_NMI;
	lint1.mask = APIC_UNMASK;
	apic_write(APIC_LVT1, lint1.value);

	/*
	 * Enable local APIC
	 */

	spiv.value = apic_read(APIC_SPIV);
	spiv.apic_enable = 1;
	apic_write(APIC_SPIV, spiv.value);

	msr_apicbase_enable();

	printk("APIC: bootstrap core lapic enabled, apic_id=0x%x\n",
	       apic_read(APIC_ID));
}

/*
 * Poll the delivery status bit till the latest IPI is acked
 * by the destination core, or timeout. As advised by Intel,
 * this should be checked after sending each IPI.
 *
 * Return 1 in case of delivery success, else return 0
 * FIXME: fine-grained timeouts using micro-seconds.
 */
int apic_ipi_acked(void)
{
	union apic_icr icr = { .value = 0 };
	int timeout = 100;

	while (timeout--) {
		icr.value_low = apic_read(APIC_ICRL);

		if (icr.delivery_status == APIC_DELSTATE_IDLE)
			break;

		pit_mdelay(1);
	}

	return timeout;
}
