/* Zebra daemon server header.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _ZEBRA_ZSERV_H
#define _ZEBRA_ZSERV_H

#include "zebra.h"

/* Default port information. */
#define ZEBRA_PORT                    2600
#define ZEBRA_VTY_PORT                2601

/* Default configuration filename. */
#define DEFAULT_CONFIG_FILE "zebra.conf"
#define SYSCONFDIR ""

/* vty local configuration file. */
#define VTY_DEFAULT_CONFIG "/etc/config/default.conf"
/* defined by tsihang for current config file*/
#define VTY_CURRENT_CONFIG "/etc/config/current.conf"

/* Client structure. */
struct zserv
{
    /* Client file descriptor. */
    int sock;
    
    /* Input/output buffer to the client. */
    struct stream *ibuf;
    struct stream *obuf;
    
    /* Threads for read/write. */
    struct thread *t_read;
    struct thread *t_write;
    
    /* default routing table this client munges */
    int rtm_table;
    
    /* This client's redistribute flag. */
    u_char redist[ZEBRA_ROUTE_MAX];
    
    /* Redistribute default route flag. */
    u_char redist_default;
    
    /* Interface information. */
    u_char ifinfo;
    
    /* Router-id information. */
    u_char ridinfo;
};

/* Zebra instance */
struct zebra_t
{
    /* Thread master */
    struct thread_master *master;
    struct list *client_list;
    
    /* default table */
    int rtm_table_default;
    
};
extern struct zebra_t cmdline_interface;

/* Count prefix size from mask length */
#define PSIZE(a) (((a) + 7) / (8))

/* Prototypes. */
void zebra_init();
void zebra_if_init();
void hostinfo_get();
void rib_init();
void interface_list();
void kernel_init();
void route_read();
void rtadv_init();
void zebra_snmp_init();

#endif /* _ZEBRA_ZEBRA_H */
