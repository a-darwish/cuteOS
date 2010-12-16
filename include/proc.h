#ifndef _PROC_H
#define _PROC_H

/*
 * Processes and their related definitions
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Please also check comments on top of the timer handler (idt.S)
 */

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>
#include <string.h>
#include <list.h>
#include <sched.h>

/*
 * IRQ 'stack protocol'.
 *
 * Stack view for any code executed at IRQ context
 * (.e.g. context-switching code, all handlers, ..)
 */
struct irq_ctx {
	/* ABI scratch registers. Pushed by us on IRQ
	 * handlers entry to freely call C code from
	 * those handlers without corrupting state */
	uint64_t r11;			/* 0x0 (%rsp) */
	uint64_t r10;			/* 0x8 (%rsp) */
	uint64_t r9;			/* 0x10(%rsp) */
	uint64_t r8;			/* 0x18(%rsp) */
	uint64_t rsi;			/* 0x20(%rsp) */
	uint64_t rdi;			/* 0x28(%rsp) */
	uint64_t rdx;			/* 0x30(%rsp) */
	uint64_t rcx;			/* 0x38(%rsp) */
	uint64_t rax;			/* 0x40(%rsp) */

	/* Regs pushed by CPU directly before invoking
	 * IRQ handlers.
	 *
	 * Those are %RIP and stack of the _interrupted_
	 * code, where all handlers including the ticks
	 * context-switching code will 'resume' to */
	uint64_t rip;			/* 0x48(%rsp) */
	uint64_t cs;			/* 0x50(%rsp) */
	uint64_t rflags;		/* 0x58(%rsp) */
	uint64_t rsp;			/* 0x60(%rsp) */
	uint64_t ss;			/* 0x68(%rsp) */
};

static inline void irq_ctx_init(struct irq_ctx *ctx)
{
	memset64(ctx, 0xdeadfeeddeadfeed, sizeof(*ctx));
}

/*
 * Process Control Block, holding machine-dependent state that
 * get swapped during a context switch.
 *
 * We already save ABI-mandated scratch registers upon ticks
 * handler entry, so we don't save them again here. They will
 * get their values restored at the end of the handler.
 *
 * The stack value here was either:
 * [1] filled by a previous context switch
 * [2] manually set at thread creation time
 *
 * In ALL ways, below stack MUST respect our IRQ stack protocol.
 * After moving to a new process (current = next), the context
 * switch code will set its stack with this value, and such code
 * runs at IRQ context of the timer handler.
 *
 * At [1], we respect the protocol by definition. In [2], we
 * set the %rsp value with an emulated stack that respects our
 * protocol.
 */
struct pcb {
	uint64_t rbp;
	uint64_t rbx;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;

	uint64_t rsp;
};

static inline void pcb_init(struct pcb *pcb)
{
	memset64(pcb, 0xdeadfeeddeadfeed, sizeof(*pcb));
}

/*
 * Process descriptor; one for each process
 */
struct proc {
	uint64_t pid;
	struct pcb pcb;			/* Hardware state (for ctxt switch) */
	int state;			/* Current process state */
	struct list_node pnode;		/* for the runqueue lists */
	clock_t runtime;		/* # ticks running on the CPU */
	clock_t enter_runqueue_ts;	/* Timestamp runqueue entrance */

	struct {			/* Scheduler statistics .. */
		clock_t runtime_overall;/* Overall runtime (in ticks) */
		uint dispatch_count;	/* # got chosen from the runqueue */
		clock_t rqwait_overall;	/* Overall wait in runqueue (ticks) */
		clock_t prio_map[MAX_PRIO+1];/* # runtime ticks at priority i */
		uint preempt_high_prio;	/* cause of a higher-priority thread */
		uint preempt_slice_end; /* cause of timeslice end */
	} stats;
};

enum proc_state {
	TD_RUNNABLE,			/* In the runqueues, to be dispatched */
	TD_ONCPU,			/* Currently runnning on the CPU */
	TD_INVALID,			/* NULL mark */
};

uint64_t proc_alloc_pid(void);

static inline void proc_init(struct proc *proc)
{
	memset(proc, 0, sizeof(struct proc));

	proc->pid = proc_alloc_pid();
	pcb_init(&proc->pcb);
	proc->state = TD_INVALID;
	list_init(&proc->pnode);
}

#endif	/* !_ASSEMBLY */

/*
 * Process Control Block register offsets
 *
 * `offsetof(struct pcb, rax)' is a C expression that can't be
 * referenced at ASM files. We manually calculate those here.
 *
 * Those offsets validity is verified at compile time.
 *
 * FreeBSD neatly solves this problem by producing a C object
 * containing:
 *
 *	char PCB_RAX[offsetof(struct pcb, rax)];
 *
 * for each PCB_* macro below. Afterwards, it let a .sh script
 * extract the symbol size (read: the expression value) using
 * binutils nm. Thus, dynamically producing below ASM-friendly
 * definitions.
 *
 * The trick is worth a mention, but not worth the effort.
 */
#define PCB_RBP		0x0
#define PCB_RBX		0x8
#define PCB_R12		0x10
#define PCB_R13		0x18
#define PCB_R14		0x20
#define PCB_R15		0x28
#define PCB_RSP		0x30
#define PCB_SIZE	(PCB_RSP + 0x8)

/*
 * Process Descriptor offsets
 */
#define PD_PID		0x0
#define PD_PCB		0x8

/*
 * IRQ stack protocol offsets
 */
#define IRQCTX_R11	0x0
#define IRQCTX_R10	0x8
#define IRQCTX_R9	0x10
#define IRQCTX_R8	0x18
#define IRQCTX_RSI	0x20
#define IRQCTX_RDI	0x28
#define IRQCTX_RDX	0x30
#define IRQCTX_RCX	0x38
#define IRQCTX_RAX	0x40
#define IRQCTX_RIP	0x48
#define IRQCTX_CS	0x50
#define IRQCTX_RFLAGS	0x58
#define IRQCTX_RSP	0x60
#define IRQCTX_SS	0x68
#define IRQCTX_SIZE	(IRQCTX_SS + 0x8)

#ifndef __ASSEMBLY__

/*
 * Verify offsets manually calculated above
 */
static inline void pcb_validate_offsets(void)
{
	compiler_assert(PCB_RBP  == offsetof(struct pcb, rbp));
	compiler_assert(PCB_RBX  == offsetof(struct pcb, rbx));
	compiler_assert(PCB_R12  == offsetof(struct pcb, r12));
	compiler_assert(PCB_R13  == offsetof(struct pcb, r13));
	compiler_assert(PCB_R14  == offsetof(struct pcb, r14));
	compiler_assert(PCB_R15  == offsetof(struct pcb, r15));
	compiler_assert(PCB_RSP  == offsetof(struct pcb, rsp));
	compiler_assert(PCB_SIZE == sizeof(struct pcb));

	compiler_assert(PD_PID == offsetof(struct proc, pid));
	compiler_assert(PD_PCB == offsetof(struct proc, pcb));

	compiler_assert(IRQCTX_R11 == offsetof(struct irq_ctx, r11));
	compiler_assert(IRQCTX_R10 == offsetof(struct irq_ctx, r10));
	compiler_assert(IRQCTX_R9  == offsetof(struct irq_ctx, r9 ));
	compiler_assert(IRQCTX_R8  == offsetof(struct irq_ctx, r8 ));
	compiler_assert(IRQCTX_RSI == offsetof(struct irq_ctx, rsi));
	compiler_assert(IRQCTX_RDI == offsetof(struct irq_ctx, rdi));
	compiler_assert(IRQCTX_RDX == offsetof(struct irq_ctx, rdx));
	compiler_assert(IRQCTX_RCX == offsetof(struct irq_ctx, rcx));
	compiler_assert(IRQCTX_RAX == offsetof(struct irq_ctx, rax));
	compiler_assert(IRQCTX_RIP == offsetof(struct irq_ctx, rip));
	compiler_assert(IRQCTX_CS  == offsetof(struct irq_ctx, cs ));
	compiler_assert(IRQCTX_RSP == offsetof(struct irq_ctx, rsp));
	compiler_assert(IRQCTX_SS  == offsetof(struct irq_ctx, ss ));
	compiler_assert(IRQCTX_RFLAGS == offsetof(struct irq_ctx, rflags));
	compiler_assert(IRQCTX_SIZE == sizeof(struct irq_ctx));
}

#endif /* !__ASSEMBLY__ */

#endif /* _PROC_H */
