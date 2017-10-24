/**
 *******************************************************************************
 *******************************************************************************
 *
 * @file    main.c
 * @author  R. Bush
 * @email   bush@krtkl.com
 * @version 0.1
 * @date    2017 October 17
 * @brief   Userspace Controlled Interrupt and Memory Interface Testing
 * @license FreeBSD
 *
 *******************************************************************************
 *
 * Copyright (c) 2016, krtkl inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 *******************************************************************************
 */


#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "uio.h"

#ifdef DEBUG
# define DEBUG_PRINT(__FORMAT__, args...)					\
		fprintf(stderr, "DEBUG: %s [%d]: %s(): " __FORMAT__ "\n",	\
			__FILE__, __LINE__, __FUNCTION__, ##args)
#else
# define DEBUG_PRINT(...)														\
							do {} while (0)
#endif


#define print_error(__FORMAT__, args...)					\
		fprintf(stderr, "ERROR: %s [%d]: %s(): " __FORMAT__ "\n",	\
			__FILE__, __LINE__, __FUNCTION__, ##args)


#define INPUT_BUFFER_SIZE			(128)
#define INPUT_BUFFER_BASE			(0x43C0000UL)

#define AXI_GPIO_BASEADDRESS			(0x41200000UL)
#define AXI_GPIO_ADDR_SIZE			(0x00010000UL)

#define AXI_GPIO_DATA_OFFSET			(0x00000000UL)
#define AXI_GPIO_TRI_OFFSET			(0x00000004UL)
#define AXI_GPIO_GIER_OFFSET			(0x0000011CUL)
#define AXI_GPIO_IPISR_OFFSET			(0x00000120UL)
#define AXI_GPIO_IPIER_OFFSET			(0x00000128UL)

#define AXI_GPIO_GIER_ENABLE			(1 << 31)


#define VERSION_STR	"0.1"

const char *const uiotest_version =
"uiotest v" VERSION_STR "\n"
"Copyright (c) 2017, krtkl inc.";

const char *const uiotest_license =
"This software may be distributed under the terms of the BSD license.\n";




void usage(void)
{
	printf("Usage: uiotest -d uio_num [-D]\n");
}


static inline unsigned long read_reg(void *base, unsigned long offset)
{
	return *(volatile unsigned long *)((unsigned char *)base + offset);
}



static inline void write_reg(void *base, unsigned long offset, unsigned long val)
{
	*(volatile unsigned long *)((unsigned char *)base + offset) = val;
}



void uio_terminate(int signo)
{
	/*
	 * The process was interrupted.
	 * Handle the termination gracefully.
	 */

	if (signo == SIGINT) {

		exit(EXIT_SUCCESS);
	} else if (signo == SIGKILL) {

		exit(EXIT_SUCCESS);
	}
}


int uio_run(char *name)
{
	size_t nb;
	unsigned long tri, ier, data, en, isr;
	int fd;
	void *iomem;

	unsigned long npending;

	DEBUG_PRINT("Opening UIO device /dev/uio0");

	fd = open(name, O_RDWR | O_SYNC);
	if (fd < 0) {
		print_error("Failure to open /dev/uio0 device - %s", strerror(errno));
	}

	/* Memory map for AXI GPIO registers */
	iomem = mmap(NULL,
			AXI_GPIO_ADDR_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			fd,
			0);

	if (iomem == MAP_FAILED) {
		print_error("Failure to map memory - %s", strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}

	DEBUG_PRINT("Initializing AXI GPIO peripheral...");

	/* Initialize the interrupts on the GPIO peripheral */
	write_reg(iomem, AXI_GPIO_GIER_OFFSET, 0);

	/* Check the direction of the pin */
	tri = read_reg(iomem, AXI_GPIO_TRI_OFFSET);
	tri |= (1 << 0);
	write_reg(iomem, AXI_GPIO_TRI_OFFSET, tri);

	ier = read_reg(iomem, AXI_GPIO_IPIER_OFFSET);
	ier |= (1 << 0);
	write_reg(iomem, AXI_GPIO_IPIER_OFFSET, ier);

	write_reg(iomem, AXI_GPIO_GIER_OFFSET, AXI_GPIO_GIER_ENABLE);

	usleep(1000 * 50);

	DEBUG_PRINT("Waiting for interrupt to occur");

	while (1) {
		/* Unmask the interrupt */
		en = 1;
		nb = write(fd, &en, sizeof(ier));
		if (nb < sizeof(en)) {
			print_error("Unmasking interrupts");
			munmap(iomem, AXI_GPIO_ADDR_SIZE);
			close(fd);
			return EXIT_FAILURE;
		}

		/* Wait for an interrupt */
		nb = read(fd, &npending, sizeof(unsigned long));

		if (npending) {
			/* Handle the interrupt */
			printf("Interrupt was detected %lu\n", npending);

			/* Read the interrupt status register */
			isr = read_reg(iomem, AXI_GPIO_IPISR_OFFSET);

			if (isr)
				write_reg(iomem, AXI_GPIO_IPISR_OFFSET, isr);

			data = read_reg(iomem, AXI_GPIO_DATA_OFFSET);
			DEBUG_PRINT("GPIO_DATA: 0x%lx", data);
		}
	}

	munmap(iomem, AXI_GPIO_ADDR_SIZE);
	close(fd);

	return EXIT_SUCCESS;
}




int main(int argc, char **argv)
{
	char *name = NULL;
	int opt, status;
	int daemonize;
	pid_t pid;

	/* Make sure we can handle SIGINT */
	if (signal(SIGINT, uio_terminate) == SIG_ERR)
		exit(EXIT_FAILURE);

	/* Parse the command line arguments */
	while ((opt = getopt(argc, argv, "d:D")) != -1) {
		switch (opt) {
		case 'd':
			name = optarg;
			break;
		case 'D':
			/* Daemonize the process */
			daemonize = 1;
			break;
		case '?':
			if (isprint(optopt))
				print_error("Unknown option `-%c'", optopt);
			else
				print_error("Unknown option character `\\x%x'", optopt);

			usage();
			return EXIT_FAILURE;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	/* Should we daemonize the process? */
	if (daemonize) {

		pid = fork();
		if (pid < 0) {
			print_error("Failed to fork process");
			err(EXIT_FAILURE, NULL);
		} else if (pid > 0) {
			/* Fork successful. Print information about the process and exit. */
			printf("UIO test daemon starting with PID: %d\n", pid);
			return EXIT_SUCCESS;
		}
	}

	if (name == NULL) {
		usage();
		return EXIT_FAILURE;
	}

	/* Run the UIO handler */
	status = uio_run(name);
	exit(status);
}
