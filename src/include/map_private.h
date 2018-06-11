#ifndef MAP_PRIVATE_H
#define MAP_PRIVATE_H

#define MAP_PREFIX	"map"
#define MAX_MAPS (1 << 2)

/** 
	Make signature id (outer). 
	UDP_N_PATTERNS define the maximum patterns per udp. 
	so 8bits for pattern can support maxmum 255 patterns. 
*/
#define UDP_ID_OFFSET	(8)
#define PATTERN_ID_OFFSET (0)
#define MAP_ID_OFFSET	(0)

#define mkpid(map_id, udp_id, pattern_id)\
	(((udp_id) << UDP_ID_OFFSET) | (((pattern_id) << PATTERN_ID_OFFSET) & 0xFF))
#define mksid(map_id, udp_id, pattern_id)\
	(((udp_id) << UDP_ID_OFFSET) | (((pattern_id) << PATTERN_ID_OFFSET) & 0xFF))

#define split_udp_id(sid)\
	((sid >> UDP_ID_OFFSET))
#define split_pattern_id(sid)\
	((sid >> PATTERN_ID_OFFSET) & 0xFF)
#define split_map_id(sid)\
	0
#define split_id(sid, map_id, udp_id, pattern_id)\
	map_id=split_map_id(sid);\
	udp_id=split_udp_id(sid);\
	pattern_id=split_pattern_id(sid);

typedef enum {
	MAP_CMD_ADD,
	MAP_CMD_DEL,
	MAP_CMD_ADD_APPL,
	MAP_CMD_DEL_APPL,
} map_cmd;

/** Vector table is used for DATAPLANE and CONTROLPLANE. */
enum {
	VECTOR_TABLE0,
	VECTOR_TABLE1,
	VECTOR_TABLES,
};

enum {
	MPM_TABLE0,
	MPM_TABLE1,
	MPM_TABLES,
};

enum {
	ACL_TABLE0,
	ACL_TABLE1,
	ACL_TABLES,
};

/** Dataplane traffic output. */
struct traffic_dpo_t {
	u8 uc_mode;
	void (*dpo_fn)(void *packet, void *map);
};

typedef struct _action_t {
	uint8_t act;
}action_t;

/**
 * Packets matching multiple maps in a configuration are sent to the map with the highest
 * priority when the network ports are shared among multiple maps with pass-by map
 * rules. By default, the first map configured has the highest priority; however, you can
 * adjust this.
 * Defaultly, priority for each map is ruled by ul_id. 
 */
struct map_t {

	char			sc_alias[32];				/** Unique, and also can be well human-readable. */
	uint32_t		ul_id;						/** Unique, allocated automatically. */
	os_lock_t 		*ol_lock;
	char 			*port_list_str[QUA_RXTX];	/** A temporary variable holding argvs from CLI,
	     											and will be freee after split. */
	uint32_t		rx_panel_port_mask;			/** Attention: QUA_RX, Where frame comes from.
													More than one map can be configured with 
													the same source ports in the port list.
													The source port list of one map must be 
													exactly the same as the source port list 
													of another map (have the same ports 
													as well as the same number of ports) and 
													it must not overlap with the source port list
													of any other map. Maps sharing the same source 
													port list are grouped together for the purpose
													of prioritizing their rules.*/
	uint32_t		tx_panel_port_mask;			/** The same with this.rx_panel_port_mask. */

	void			*acl_config_ctx[ACL_TABLES];

	struct list_head	prio_node;

#define MAP_DEFAULT				(1 << 0)		/** Default map can not be removed. */
#define MAP_TRAFFIC_TRANSPARENT	(1 << 1)		/** Transparent, default setting for a map.
												 * When a map created, traffic will be transparent bettwen its "from" and "to" port,
												 * no matter whether there are passed applications, udps or not. */
#define MAP_HASH_5TUPLE			(1 << 2)
	uint32_t		ul_flags;
	uint64_t		ull_create_time;			/** Create time. */
	
};
#define map_id(map) ((map)->ul_id)
#define map_alias(map) ((map)->sc_alias)

typedef struct {
	uint32_t				ul_flags;
	uint32_t				nb_acl_rules;
	volatile uint32_t		nb_maps;
	struct list_head		map_priority_list;
	os_lock_t 				lock;
	volatile uint32_t		vector_runtime;
	volatile oryx_vector	map_curr_table;
	oryx_vector				entry_vec[VECTOR_TABLES];
	//oryx_vector				entry_vec;
	struct oryx_htable_t	*htable;	
	struct map_t			*lowest_map;	/** fast tracinging. unused actually. */
	struct map_t			*highest_map;
}vlib_map_main_t;

extern vlib_map_main_t vlib_map_main;

static __oryx_always_inline__
void map_entry_lookup_alias (vlib_map_main_t *mm, char *alias, struct map_t **map)
{
	(*map) = NULL;
	
	if (!alias) return;

	void *s = oryx_htable_lookup (mm->htable,
		(ht_value_t)alias, strlen((const char *)alias));

	if (s) {
		(*map) = (struct map_t *) container_of (s, struct map_t, sc_alias);
	}
}

static __oryx_always_inline__
void map_entry_lookup_id (vlib_map_main_t *mm, u32 id, struct map_t **m)
{
	BUG_ON(mm->map_curr_table == NULL);

	if (!vec_active(mm->map_curr_table) || 
		(id > vec_active(mm->map_curr_table)))
		return;

	(*m) = vec_lookup (mm->map_curr_table, id);
}

static __oryx_always_inline__
void map_table_entry_lookup (struct prefix_t *lp, 
	struct map_t **m)
{
	vlib_map_main_t *mm = &vlib_map_main;

	ASSERT (lp);
	ASSERT (m);
	(*m) = NULL;
	
	switch (lp->cmd) {
		case LOOKUP_ID:
			map_entry_lookup_id(mm, (*(u32*)lp->v), m);
			break;
		case LOOKUP_ALIAS:
			map_entry_lookup_alias(mm, (char*)lp->v, m);
			break;
		default:
			break;
	}
}

#endif

