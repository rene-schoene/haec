#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <error.h>
#include <errno.h>

#include <racr/racr.h>

#include "server.h"
#include "worker.h"
#include "event.h"
#include "cambri.h"


// TODO: move this to scheme
#define TIME_HALTING 13.0
#define TIME_REBOOTDELAY 12.0
#define TIME_NOCURRENT 12.0
#define ADAPTATION_FREQUENCY 20.0
#define MAX_BOOT_TIME 100.0

Server server;


void eval_string(const char* str);


ssize_t sendf(int s, const char* format, ...) {
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	return send(s, line, strlen(line) + 1, 0);
}


void server_log_event(const char* fmt, ...) {
	char buf[256];
	va_list args;
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	printf("%s", buf);
	fprintf(server.log_fd, "%s", buf);
	fflush(server.log_fd);
}


static int server_command(char* cmd) {

	double time = timestamp();
	double duration, size, max_e, max_rt, min_p;
	int id, work_id, reply_port;
	Worker* w;
	char scenario[256], objective[256], reply_ip[256];

	if (strcmp(cmd, "exit") == 0) {
		server.running = 0;
		printf("exiting...\n");
	}
	else if (strcmp(cmd, "status") == 0) {
		printf(" id   | parent | address:port          | socket | state   | time\n");
		printf("------+--------+-----------------------+--------+---------+-------------\n");
		for (w = worker_next(NULL); w; w = worker_next(w)) {
			char s[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &w->addr, s, sizeof(s));
			printf(" %-4d | %-6d | %-15s:%05d | %6d | %-7s | %s\n",
				w->id, w->parent_id, s, w->port, w->socket_fd,
				worker_state_string(w->state), format_timestamp(time - w->timestamp));
		}
	}
	else if (sscanf(cmd, "work %lf %lf", &size, &duration) == 2) {
		Event* e = event_append(EVENT_WORK_REQUEST);
		e->work_id = server.work_counter++;
		e->load_size = size;
		e->deadline = time + duration;
	}
	else if (sscanf(cmd, "scenario %s", scenario) == 1) {
		Event* e = event_append(EVENT_SCENARIO_START);
		e->scenario = strdup(scenario);
	}
	else if (strcmp(cmd, "done") == 0) {
		fclose(server.scenario_fd);
		server.scenario_fd = NULL;
		event_append(EVENT_SCENARIO_DONE);
	}
	else if (strcmp(cmd, "reset e-meter") == 0) {
		event_append(EVENT_E_METER_RESET);
	}


	// testing...
	else if (cmd[0] == '(') eval_string(cmd);

	else if (strncmp(cmd, "cambri0 ", 7) == 0) {
		cambri_write(0, cmd + 7);
		char buf[4096] = {};
		int ret = cambri_read(0, buf, sizeof(buf));
		if (ret > 0) printf("%s\n", buf);
	}
	else if (strncmp(cmd, "cambri1 ", 7) == 0) {
		cambri_write(1, cmd + 7);
		char buf[4096] = {};
		int ret = cambri_read(1, buf, sizeof(buf));
		if (ret > 0) printf("%s\n", buf);
	}

	else if (sscanf(cmd, "boot %d", &id) == 1) {
		w = worker_find_by_id(id);
		if (!w) {
			printf("error: %s\n", cmd);
			return 1;
		}
		if (w->state != WORKER_OFF) {
			printf("error: worker %d is not OFF\n", w->id);
			return 1;
		}
		w->state = WORKER_BOOTING;
		w->timestamp = timestamp();
		cambri_set_mode(w->id, CAMBRI_CHARGE);
	}

	else if (sscanf(cmd, "halt %d", &id) == 1) {
		w = worker_find_by_id(id);
		if (!w) {
			printf("error: %s\n", cmd);
			return 1;
		}
		sendf(w->socket_fd, "halt");
		w->state = WORKER_HALTING;
		w->timestamp = timestamp();
	}

	else if (sscanf(cmd, "work-command %d %d %lf", &id, &work_id, &size) == 3) {
		w = worker_find_by_id(id);
		if (!w) {
			printf("error: %s\n", cmd);
			return 1;
		}
		Event* e = event_append(EVENT_WORK_COMMAND);
		e->worker = w;
		e->work_id = work_id;
		e->load_size = size;
	} else {
		int found = sscanf(cmd, "request %d %s %lf %lf %lf %d %s", &work_id, objective, &max_e, &max_rt, &min_p,
			&reply_port, reply_ip);
		if (found >= 6) {
			Event* e = event_append(EVENT_MQUAT_REQUEST);
			e->work_id   = work_id;
			e->objective = strdup(objective);
//			printf("1. %s -> %s\n", objective, e->objective);
			e->max_e     = max_e;
			e->max_rt    = max_rt;
			e->min_p     = min_p;
			e->reply_port= reply_port;
//			char* p = strchr(reply_ip, ' ');
//			if (p) *p++ = '\0';
//			printf("2. %s -> %s, %s\n", objective, e->objective, reply_ip);
			if(found == 7) {
				strcpy(e->reply_ip, reply_ip);
//				printf("found reply_ip: %s\n", e->reply_ip);
			} else {
				strcpy(e->reply_ip, "127.0.0.1");
//				printf("using default reply_ip %s\n", e->reply_ip);
			}
//			printf("3. %s -> %s\n", objective, e->objective);
//			printf("sscanf: %d %s %lf %lf %lf %d %s\n", work_id, objective, max_e, max_rt, min_p,
//				reply_port, reply_ip);
//			printf("event: %d %s %lf %lf %lf %d %s\n", e->work_id, e->objective, e->max_e, e->max_rt, e->min_p,
//				e->reply_port, e->reply_ip);
		} else {
			printf("error: %s\n", cmd);
			return 1;
		}
	}

	return 0;
}


static void server_new_connection(int s) {
	struct sockaddr_in client;
	socklen_t size = sizeof(client);
	int newfd = accept(s, (struct sockaddr*) &client, &size);
	if (newfd < 0) perror("accept");
	else {
		Worker* w = worker_find_by_address(client.sin_addr, 0);
		if (!w) {
			char s[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &client.sin_addr, s, sizeof(s));
			printf("unexpected connection from %s:%d\n", s, client.sin_port);
			close(newfd);
			return;
		}

		FD_SET(newfd, &server.fds);
		if (newfd > server.fdmax) server.fdmax = newfd;

		w->port = client.sin_port;
		w->socket_fd = newfd;

		Event* e = event_append(EVENT_WORKER_ONLINE);
		e->worker = w;

	}
}


static void server_receive(int s) {
	char msg[256];
	ssize_t len = recv(s, msg, sizeof(msg), 0);

	Worker* w = worker_find_by_socket(s);
	if (!w) {
		printf("unknown receiver socker: %d\n", s);
		return;
	}

	if (len <= 0) {
		close(s);
		FD_CLR(s, &server.fds);

		w->socket_fd = -1;
		w->port = 0;

		Event* e = event_append(EVENT_WORKER_OFFLINE);
		e->worker = w;
		return;
	}

	//printf("received %d bytes from worker %d: %.*s\n", (int) len, w->id, (int) len, msg);

	char* p = strchr(msg, ' ');
	if (p) *p++ = '\0';
	int ack;

	if (strcmp(msg, "work-complete") == 0) {
		int id;
		if (sscanf(p, "%d %d", &id, &ack) != 2) goto ERROR;
		Event* e = event_append(EVENT_WORK_COMPLETE);
		e->worker = w;
		e->work_id = id;
		e->ack = ack;
	}
	else if (strcmp(msg, "work-ack") == 0) {
		if (sscanf(p, "%d", &ack) != 1) goto ERROR;
		Event* e = event_append(EVENT_WORK_ACK);
		e->worker = w;
		e->ack = ack;
	}
	else if (strcmp(msg, "halt-ack") == 0) {
		if (sscanf(p, "%d", &ack) != 1) goto ERROR;
		Event* e = event_append(EVENT_HALT_ACK);
		e->worker = w;
		e->ack = ack;
	}
	else if (strcmp(msg, "mem-ack") == 0) {
		if (sscanf(p, "%d", &ack) != 1) goto ERROR;
		Event* e = event_append(EVENT_MEM_ACK);
		e->worker = w;
		e->ack = ack;
	}
	else if (strcmp(msg, "cpu-ack") == 0) {
	}
	else goto ERROR;
	return;
ERROR:
	error(1, 0, "server_receive");
}

static int check_name(const char* binVar, int* workers, int* particles) {
	//check. if e.g. Sampling##KLD_5_700#r_1002 return 1 (set workers, particles). else return 0.
	int result;
	result = sscanf(binVar, "Sampling##KLD_%d_%d#*", workers, particles) == 2;
	printf("Checking %s gives %d\n", binVar, result);
	return result;
}

static _Bool starts_with(const char* string, const char* prefix)
{
    while(*prefix)
    {
        if(*prefix++ != *string++)
            return 0;
    }

    return 1;
}


static int server_read_scenario_command() {

	server.scenario_cmd[0] = '\0';
	server.scenario_cmd_time = 0;

	if (!server.scenario_fd) return 0;

	char buf[256];
	if (!fgets(buf, sizeof(buf), server.scenario_fd)) {
		fclose(server.scenario_fd);
		server.scenario_fd = NULL;
		event_append(EVENT_SCENARIO_DONE);
		return 0;
	}

	double time = read_timestamp(buf);
	if (time < 0) return -1;

	char* cmd = strchr(buf, ' ');
	if (!cmd) return -1;
	while (*cmd == ' ') cmd++;

	char* p = strchr(cmd, '\n');
	if (p) *p = '\0';

	strcpy(server.scenario_cmd, cmd);
	server.scenario_cmd_time = time;

	return 0;
}


void server_process_events(void) {
	Event* e;
	double time = timestamp();
	while ((e = event_pop())) {

		event_print(e, time);

		Worker* w = e->worker;
		switch (e->type) {
		case EVENT_SCENARIO_START:
			if (server.scenario_fd) fclose(server.scenario_fd);
			server.scenario_fd = fopen(e->scenario, "r");
			if (server.scenario_fd) {
				server.scenario_timestamp = time;
				while (server_read_scenario_command()) {
					printf("error reading scenario command");
				}
			}
			else printf("could not open scenario %s\n", e->scenario);
			free(e->scenario);
			e->scenario = NULL;
			break;

		case EVENT_SCENARIO_DONE:
			break;

		case EVENT_WORKER_ON:
			if (w->state != WORKER_OFF && w->state != WORKER_REBOOTING) {
				printf("worker %d cannot be turned on as it is not off\n", w->id);
				w->state = WORKER_ERROR;
			}
			else {
				w->state = WORKER_BOOTING;
				cambri_set_mode(w->id, CAMBRI_CHARGE);
			}
			w->timestamp = time;
			break;

		case EVENT_WORKER_ONLINE:
			if (w->state == WORKER_BOOTING) {
				printf("worker %d booted successfully in %.2f seconds\n", w->id, time - w->timestamp);
			}
			w->state = WORKER_RUNNING;
			w->timestamp = time;
			racr_call_str("event-worker-online", "id", w->id, time);
			break;

		case EVENT_WORKER_OFFLINE:
			if (w->state != WORKER_HALTING) {
				printf("worker %d hung up unexpectedly\n", w->id);
				w->state = WORKER_ERROR;
				w->timestamp = time;
			}
			racr_call_str("event-worker-offline", "id", w->id, time);
			break;

		case EVENT_WORKER_OFF:
			w->state = WORKER_OFF;
			w->timestamp = time;
			w->socket_fd = -1;
			w->port = 0;
			cambri_set_mode(w->id, CAMBRI_OFF);
			racr_call_str("event-worker-off", "id", w->id, time);
			break;

		case EVENT_WORKER_REBOOT:
			w->state = WORKER_REBOOTING;
			w->timestamp = time;
			cambri_set_mode(w->id, CAMBRI_OFF);
			break;

		case EVENT_WORK_REQUEST:
			racr_call_str("event-work-request", "didd", time, e->work_id, e->load_size, e->deadline);
			break;

		case EVENT_WORK_COMMAND:
			sendf(w->socket_fd, "work %d %lf", e->work_id, e->load_size);
			break;

		case EVENT_WORK_ACK:
			break;

		case EVENT_WORK_COMPLETE:
			racr_call_str("event-work-complete", "idi", w->id, time, e->work_id);
			break;

		case EVENT_MEM_COMMAND:
			sendf(w->socket_fd, "mem");
			break;

		case EVENT_MEM_ACK:
			break;

		case EVENT_HALT_COMMAND:
			sendf(w->socket_fd, "halt");
			w->state = WORKER_HALTING;
			w->timestamp = timestamp();
			break;

		case EVENT_HALT_ACK:
			break;

		case EVENT_E_METER_RESET:
			cambri_set_energy(0);
			server.e_meter_timestamp = time;
			break;

		case EVENT_ADAPT:
			racr_call_str("event-adapt", "d", time);
			break;

		case EVENT_MQUAT_REQUEST:
			printf("obj: %s\n", e->objective);
			racr_call_str("event-request", "disddd", time, e->work_id, e->objective, e->max_e, e->max_rt, e->min_p);
			// invoke glpsol
			int result = 1, workers, particles;
			printf("Calling glpsol ...\n");
			char buf[256], fout[256];
			snprintf(&fout, 256, "ilps/%d.out", e->work_id);
			snprintf(&buf, 256, "glpsol --lp ilps/%d.lp -o %s >/dev/null", e->work_id, fout);
			int return_code = system(buf);
			if(return_code > 0) {
				printf("glpsol failed with %d\n", return_code);
				workers = particles = 0;
			} else {
				// interpret result
				FILE *fp;
				fp = fopen(fout, "r");
				if (fp == NULL) {
					printf("Could not open result at '%s'\n", fout);
				} else {
					//read line by line
					const size_t line_size = 300;
					char* line = malloc(line_size);
					int status = 1; //START
					int row = 0;
					char lastName[256];
					double val, lb, ub;
					while (fgets(line, line_size, fp) != NULL){
						if(result == 0) {
							break;
						}
//						printf(line);
						switch(status) {
							case 1: //Start
								if(starts_with(line, "   No. Column")) { status = 2; printf("scanning vars..\n"); }
								break;
							case 2: //Name of var, or Name+value
								if (sscanf(line, " %d b#%s", &row, lastName) == 2) { // Name of var
									status = 3;
									printf("found var %s\n", lastName);
								} else if (sscanf(line, " %d b#%s * %lf %lf %lf ", &row, lastName, &val, &lb, &ub) == 5) {
									printf("val of %s on same line is %lf\n", lastName, val);
									if(val == 1) {
										if(check_name(lastName, &workers, &particles)) {
											result = 0;
										}
									}
								}
								break;
							case 3: //Value of var
								if(sscanf(line, " * %lf %lf %lf ", &val, &lb, &ub) == 3) {
									printf("val of %s is %lf\n", lastName, val);
									if(val == 1) {
										if(check_name(lastName, &workers, &particles)) {
											result = 0;
										}
									}
								}
								status = 2;
								break;
							default:
								printf("Unknown status %d\n", status);
								break;
							}
						}
						fclose(fp);
					}
				}
			// open socket to reply_port and send result
			int socket_fd;
			printf("create addr, reply to %s:%d\n", e->reply_ip, e->reply_port);
			struct sockaddr_in client = { AF_INET, htons(e->reply_port), { inet_addr(e->reply_ip) } };
			printf("creating socket\n");
//			socket_fd = socket(AF_INET, SOCK_STREAM, 0);
			socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (socket_fd < 0) error(1, 0, "socket\n");
			printf("check if connected\n");
			if (connect(socket_fd, (struct sockaddr*) &client, sizeof(client)) < 0) {
				if (errno == ECONNREFUSED) {
					printf("connection refused\n");
					error(1, 0, "refused");
				}
				else {
//					close(socket_fd);
					perror("main");
					error(1, 0, "connect");
				}
			} else {
				// connected
				printf("connected\n");
				
				return_code = sendf(socket_fd, "result %d %d %d", result, workers, particles);
				printf("sent %d bytes, closing stream\n", return_code);
				close(socket_fd);
				printf("stream closed\n");
			}
			break;

		default:
			printf("unknown event: %d\n", e->type);
			break;
		}
		if (e->type != EVENT_MQUAT_REQUEST) {
			printf("before free\n");
			free(e);
			printf("after free\n");
		}
	}
}

static void server_done(int sig) { server.running = 0; }


void server_run(int argc, char** argv) {

	if (argc == 2) {
		Event* e = event_append(EVENT_SCENARIO_START);
		e->scenario = strdup(argv[1]);
	}


	if (cambri_init()) printf("error initializing cambri\n");
	if (worker_init()) {
		printf("error initializing workers\n");
		return;
	}


	int listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) error(1, 0, "socket");
	int yes = 1;
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
		error(1, 0, "setsockopt");
	}
	struct sockaddr_in addr = { AF_INET, htons(PORT), { INADDR_ANY } };
	if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		error(1, 0, "bind");
	}
	listen(listener, 5);

	int commander = socket(AF_INET, SOCK_DGRAM, 0);
	if (commander < 0) error(1, 0, "socket");
	addr.sin_port = htons(PORT + 1);
	if (bind(commander, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		error(1, 0, "bind");
	}



	FD_ZERO(&server.fds);
	FD_SET(STDIN, &server.fds);
	FD_SET(listener, &server.fds);
	FD_SET(commander, &server.fds);

	server.fdmax = commander;
	server.work_counter = 0;
	server.timestamp = absolut_timestamp();
	server.e_meter_timestamp = 0;
	server.running = 1;
	signal(SIGINT, server_done);

	server.log_fd = fopen("event.log", "w");

	double adapt_time = timestamp();

	printf("entering server loop\n");
	while (server.running) {

		server_process_events();

		// check for commands from scenario
		double time = timestamp();
		while (server.scenario_fd &&
		time >= server.scenario_timestamp + server.scenario_cmd_time) {
			server_command(server.scenario_cmd);
			while (server_read_scenario_command()) {
				printf("error reading scenario command");
			}
		}

		// receive
		fd_set fds = server.fds;
		struct timeval timeout = { 0, 50000 };
		int count;
		for (;;) {
tryagain:
			count = select(server.fdmax + 1, &fds, NULL, NULL, &timeout);
			if (count == 0) break;
			if (count < 0) {
				if (errno == EINTR) {
					printf("EINTR\n");
					goto tryagain;
				}
				perror("select");
				break;
			}
			int i;
			for (i = 0; i <= server.fdmax; i++) {
				if (FD_ISSET(i, &fds)) {
					if (i == STDIN) {
						char cmd[256];
						if (fgets(cmd, sizeof(cmd), stdin)) {
							char* p = strchr(cmd, '\n');
							if (p) *p = '\0';
							server_command(cmd);
						}
					}
					else if (i == commander) {
						char cmd[256] = {};
						recvfrom(commander, cmd, sizeof(cmd), 0, NULL, NULL);
						server_command(cmd);
					}
					else if (i == listener) server_new_connection(listener);
					else server_receive(i);
				}
			}
		}


		// turn off halting workers
		Worker* w;
		for (w = worker_next(NULL); w; w = worker_next(w)) {
			if (w->state == WORKER_HALTING
			&& time - w->timestamp > TIME_HALTING) {
				Event* e = event_append(EVENT_WORKER_OFF);
				e->worker = w;
			}
			else if (w->state == WORKER_BOOTING && time - w->timestamp > MAX_BOOT_TIME) {
				Event* e = event_append(EVENT_WORKER_REBOOT);
				e->worker = w;
			}
			else if (w->state == WORKER_BOOTING && time - w->timestamp > TIME_NOCURRENT) {
				if (cambri_get_current(w->id) == 0) {
					Event* e = event_append(EVENT_WORKER_REBOOT);
					e->worker = w;
				}
			}
			else if(w->state == WORKER_REBOOTING
			&& time - w->timestamp > TIME_REBOOTDELAY) {
				Event* e = event_append(EVENT_WORKER_ON);
				e->worker = w;
			}
		}

		if (timestamp() - adapt_time > ADAPTATION_FREQUENCY ) {
			Event* e = event_append(EVENT_ADAPT);
			e->worker = w;
			adapt_time = timestamp();
		}

		cambri_log_data(time, "scheduler1");
	}

	close(listener);
	close(commander);

	cambri_kill();
	worker_kill();
	fclose(server.log_fd);
}
