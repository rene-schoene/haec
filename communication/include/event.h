#pragma once

#include "worker.h"


typedef struct Event Event;
struct Event {
	Event*	next;
	int		type;

	// validity of fields depends on type
	Worker* worker;
	double	load_size;
	double	deadline;
	double	max_e;
	double	max_rt;
	double	min_p;
	int		work_id;
	int		ack;
	int		reply_port;
	char*	scenario;
	char*	objective;
	char	reply_ip[256];
};


enum {
	EVENT_SCENARIO_START,
	EVENT_SCENARIO_DONE,

	EVENT_WORKER_ON,
	EVENT_WORKER_ONLINE,
	EVENT_WORKER_OFFLINE,
	EVENT_WORKER_OFF,

	EVENT_WORKER_REBOOT,

	EVENT_WORK_REQUEST,
	EVENT_WORK_COMMAND,
	EVENT_WORK_ACK,
	EVENT_WORK_COMPLETE,

	EVENT_HALT_COMMAND,
	EVENT_HALT_ACK,

	EVENT_MEM_COMMAND,
	EVENT_MEM_ACK,

	EVENT_E_METER_RESET,

	EVENT_ADAPT,

	EVENT_MQUAT_REQUEST,

};


static inline const char* event_type_string(int type) {
	static const char* strings[] = {
		"SCENARIO_START",
		"SCENARIO_DONE",

		"WORKER_ON",
		"WORKER_ONLINE",
		"WORKER_OFFLINE",
		"WORKER_OFF",

		"WORKER_REBOOT",

		"WORK_REQUEST",
		"WORK_COMMAND",
		"WORK_ACK",
		"WORK_COMPLETE",

		"HALT_COMMAND",
		"HALT_ACK",

		"MEM_COMMAND",
		"MEM_ACK",

		"E_METER_RESET",

		"ADAPT",

		"EVENT_MQUAT_REQUEST",

	};
	return strings[type];
}


Event*	event_append(int type);
Event*	event_pop(void);
void	event_print(const Event* e, double time);

double	absolut_timestamp(void);
double	timestamp(void);
const char* format_timestamp(double t);
double read_timestamp(const char* t);
