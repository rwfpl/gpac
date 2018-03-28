/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2017
 *					All rights reserved
 *
 *  This file is part of GPAC / gpac application
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <gpac/main.h>
#include <gpac/filters.h>
#include <time.h>

static u64 log_time_start = 0;
static Bool log_utc_time = GF_FALSE;
static Bool logs_set = GF_FALSE;
static FILE *logfile = NULL;
static s32 nb_threads=0;
static GF_SystemRTInfo rti;
static u32 list_filters = 0;
static Bool dump_stats = GF_FALSE;
static Bool dump_graph = GF_FALSE;
static Bool print_filter_info = GF_FALSE;
static Bool load_test_filters = GF_FALSE;
static u64 last_log_time=0;

static void print_filters(int argc, char **argv, GF_FilterSession *session);

static void on_gpac_log(void *cbk, GF_LOG_Level ll, GF_LOG_Tool lm, const char *fmt, va_list list)
{
	FILE *logs = cbk ? cbk : stderr;

	if (log_time_start) {
		u64 now = gf_sys_clock_high_res();
		fprintf(logs, "At "LLD" (diff %d) - ", now - log_time_start, (u32) (now - last_log_time) );
		last_log_time = now;
	}
	if (log_utc_time) {
		u64 utc_clock = gf_net_get_utc() ;
		time_t secs = utc_clock/1000;
		struct tm t;
		t = *gmtime(&secs);
		fprintf(logs, "UTC %d-%02d-%02dT%02d:%02d:%02dZ (TS "LLU") - ", 1900+t.tm_year, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, utc_clock);
	}
	vfprintf(logs, fmt, list);
	fflush(logs);
}


static void gpac_usage(void)
{
	fprintf(stderr, "Usage: gpac [options] FILTER_ARGS [LINK] FILTER_ARGS\n"
			"This is the command line utility of GPAC for setting up and running filter chains.\n"
			"Filters are listed with their name and options are given using a list of colon-separated Name=Value:\n"
			"\tValue can be omitted for booleans, defaulting to true.\n"
			"\tName can be omitted for enumerations (eg :mode=pbo <=> :pbo).\n"
			"\tSources may be specified direcly using src=URL, or forcing a dedicated demuxer using demux_name:src=URL.\n"
			"\n"
			"LINK directives may be specified. The syntax is an '@' character optionnaly followed by an integer (0 if omitted).\n"
			"This indicates which previous (0-based) filters should be link to the next filter listed.\n"
			"Only the last link directive occuring before a filter is used to setup links for that filter.\n"
			"\tEX:, \"f1 f2 @1 f3\" inidcates to direct f1 outputs to f3\n"
			"\tEX:, \"f1 f2 @1 @0 f3\" inidcates to direct f2 outputs to f3, @1 is ignored\n"
			"If no link directives are given, the links will be dynamically solved to fullfill as many connections as possible.\n"
			"For example, \"f1 f2 f3\" may have f1 linked to f2 and f3 if f2 and f3 accept f1 outputs\n"
			"LINK directive is just a quick shortcut to set reserved argument FID=name, which sets the ID of the filter\n"
			"and SID=name1[,name2...], which restricts the list of possible inputs path on a filter\n"
			"\tEX: \"f1:FID=1 f2 f3:SID=1\" inidicates to direct f1 outputs to f3\n"
			"\tEX: \"f1:FID=1 f2:FID=2 f3:SID=1 f4:SID=1,2\" indicates to direct f1 outputs to f3, and f1 and f2 outputs to f4\n"
			"Source IDs are the names of the source filters allowed. The name can be further extended using:\n"
			"\tname#PIDNAME: accepts only PIDs with name PIDNAME\n"
			"\tname#PID=N: accepts only PIDs with ID N\n"
			"\tname#TYPE: accepts only PIDs of matching media type. TYPE can be 'audio' 'video' 'scene' 'text' 'font'\n"
			"\tname#TYPEN: accepts only Nth PID of matching type from source\n"
			"\n"
			"\n"
			"\n"
			"Global options are:\n"
#ifdef GPAC_MEMORY_TRACKING
            "\t-mem-track:  enables memory tracker\n"
            "\t-mem-track-stack:  enables memory tracker with stack dumping\n"
#endif
			"\t-list           : lists all supported filters.\n"
			"\t-list-meta      : lists all supported filters including meta-filters (ffmpeg & co).\n"
			"\t-info NAME      : print info on filter NAME. For meta-filters, use NAME:INST, eg ffavin:avfoundation\n"
			"                    Use * to print info on all filters (warning, big output!)\n"
			"                    Use *:* to print info on all filters including meta-filters (warning, big big output!)\n"
			"\t-links          : prints possible connections between each supported filters and exits\n"
			"\t-stats          : print stats after execution. Stats can be viewed at runtime by typing 's' in the prompt\n"
			"\t-graph          : print stats after  Graph can be viewed at runtime by typing 'g' in the prompt\n"
	        "\t-threads=N      : sets N extra thread for the session. -1 means use all available cores\n"
			"\t-no-block       : disables blocking mode of filters\n"
	        "\t-sched=MODE     : sets scheduler mode. Possible modes are:\n"
	        "\t             free: uses lock-free queues (default)\n"
	        "\t             lock: uses mutexes for queues when several threads\n"
	        "\t             flock: uses mutexes for queues even when no thread (debug mode)\n"
	        "\t             direct: uses no threads and direct dispatch of tasks whenever possible (debug mode)\n"
			"\n"
			"\t-ltf            : loads test filters for unit tests.\n"
	        "\t-strict-error:  exit at first error\n"
	        "\t-log-file=file: sets output log file. Also works with -lf\n"
	        "\t-logs=log_args: sets log tools and levels, formatted as a ':'-separated list of toolX[:toolZ]@levelX\n"
	        "\t                 levelX can be one of:\n"
	        "\t        \"quiet\"      : skip logs\n"
	        "\t        \"error\"      : logs only error messages\n"
	        "\t        \"warning\"    : logs error+warning messages\n"
	        "\t        \"info\"       : logs error+warning+info messages\n"
	        "\t        \"debug\"      : logs all messages\n"
	        "\t                 toolX can be one of:\n"
	        "\t        \"core\"       : libgpac core\n"
	        "\t        \"coding\"     : bitstream formats (audio, video, scene)\n"
	        "\t        \"container\"  : container formats (ISO File, MPEG-2 TS, AVI, ...)\n"
	        "\t        \"network\"    : network data exept RTP trafic\n"
	        "\t        \"rtp\"        : rtp trafic\n"
	        "\t        \"author\"     : authoring tools (hint, import, export)\n"
	        "\t        \"sync\"       : terminal sync layer\n"
	        "\t        \"codec\"      : terminal codec messages\n"
	        "\t        \"parser\"     : scene parsers (svg, xmt, bt) and other\n"
	        "\t        \"media\"      : terminal media object management\n"
	        "\t        \"scene\"      : scene graph and scene manager\n"
	        "\t        \"script\"     : scripting engine messages\n"
	        "\t        \"interact\"   : interaction engine (events, scripts, etc)\n"
	        "\t        \"smil\"       : SMIL timing engine\n"
	        "\t        \"compose\"    : composition engine (2D, 3D, etc)\n"
	        "\t        \"mmio\"       : Audio/Video HW I/O management\n"
	        "\t        \"rti\"        : various run-time stats\n"
	        "\t        \"cache\"      : HTTP cache subsystem\n"
	        "\t        \"audio\"      : Audio renderer and mixers\n"
#ifdef GPAC_MEMORY_TRACKING
	        "\t        \"mem\"        : GPAC memory tracker\n"
#endif
#ifndef GPAC_DISABLE_DASH_CLIENT
	        "\t        \"dash\"       : HTTP streaming logs\n"
#endif
	        "\t        \"module\"     : GPAC modules debugging\n"
	        "\t        \"filter\"     : GPAC modules debugging\n"
	        "\t        \"mutex\"      : mutex\n"
	        "\t        \"all\"        : all tools logged - other tools can be specified afterwards.\n"
	        "\n"
	        "\t-log-clock or -lc      : logs time in micro sec since start time of GPAC before each log line.\n"
	        "\t-log-utc or -lu        : logs UTC time in ms before each log line.\n"
			"\n"
	        "gpac - gpac command line filter engine - version "GPAC_FULL_VERSION"\n"
	        "GPAC Written by Jean Le Feuvre (c) Telecom ParisTech 2017-2018\n"
	        "GPAC Configuration: " GPAC_CONFIGURATION "\n"
	        "Features: %s\n",
	        gpac_features()
	);

}

static Bool gpac_fsess_task(GF_FilterSession *fsess, void *callback, u32 *reschedule_ms)
{
	if (gf_prompt_has_input()) {
		char c = gf_prompt_get_char();
		switch (c) {
		case 'q':
			gf_fs_abort(fsess);
			return GF_FALSE;
		case 's':
			gf_fs_print_stats(fsess);
			break;
		case 'g':
			gf_fs_print_connections(fsess);
			break;
		default:
			break;
		}
	}
	if (gf_fs_is_last_task(fsess))
		return GF_FALSE;
	*reschedule_ms = 500;
	return GF_TRUE;
}

static int gpac_main(int argc, char **argv)
{
	GF_Err e=GF_OK;
	int i;
	s32 link_prev_filter = -1;
	GF_List *loaded_filters=NULL;

	GF_FilterSchedulerType sched_type = GF_FS_SCHEDULER_LOCK_FREE;
	GF_MemTrackerType mem_track=GF_MemTrackerNone;
	GF_FilterSession *session;
	Bool disable_blocking = GF_FALSE;
	Bool view_filter_conn = GF_FALSE;

	for (i=1; i<argc; i++) {
		char *arg = argv[i];
		if (!strcmp(arg, "-mem-track") || !strcmp(arg, "-mem-track-stack")) {
#ifdef GPAC_MEMORY_TRACKING
            mem_track = !strcmp(arg, "-mem-track-stack") ? GF_MemTrackerBackTrace : GF_MemTrackerSimple;
#else
			fprintf(stderr, "WARNING - GPAC not compiled with Memory Tracker - ignoring \"%s\"\n", arg);
#endif
		} else if (!strcmp(arg, "-h") || !strcmp(arg, "-help")) {
			gpac_usage();
			return 0;
		}
	}
	gf_sys_init(mem_track);
	gf_sys_set_args(argc, (const char **) argv);

	for (i=1; i<argc; i++) {
		char *arg = argv[i];
		char *arg_val = strchr(arg, '=');
		if (arg_val) {
			arg_val[0]=0;
			arg_val++;
		}

		if (!strcmp(arg, "-strict-error")) {
			gf_log_set_strict_error(1);
		} else if (!strcmp(arg, "-log-file") || !strcmp(arg, "-lf")) {
			if (arg_val) {
				logfile = gf_fopen(arg_val, "wt");
				gf_log_set_callback(logfile, on_gpac_log);
			}
		} else if (!strcmp(arg, "-logs") ) {
			if (gf_log_set_tools_levels(arg_val) != GF_OK) {
				return 1;
			}
			logs_set = GF_TRUE;
		} else if (!strcmp(arg, "-log-clock") || !strcmp(arg, "-lc")) {
			log_time_start = 1;
		} else if (!strcmp(arg, "-log-utc") || !strcmp(arg, "-lu")) {
			log_utc_time = 1;
		} else if (arg_val && !strcmp(arg, "-threads")) {
			nb_threads = atoi(arg_val);
		} else if (arg_val && !strcmp(arg, "-sched")) {
			if (!strcmp(arg_val, "lock")) sched_type = GF_FS_SCHEDULER_LOCK;
			else if (!strcmp(arg_val, "flock")) sched_type = GF_FS_SCHEDULER_LOCK_FORCE;
			else if (!strcmp(arg_val, "direct")) sched_type = GF_FS_SCHEDULER_DIRECT;
			else if (!strcmp(arg_val, "freex")) sched_type = GF_FS_SCHEDULER_LOCK_FREE_X;
		} else if (!strcmp(arg, "-ltf")) {
			load_test_filters = GF_TRUE;
		} else if (!strcmp(arg, "-list")) {
			list_filters = 1;
		} else if (!strcmp(arg, "-list-meta")) {
			list_filters = 2;
		} else if (!strcmp(arg, "-stats")) {
			dump_stats = GF_TRUE;
		} else if (!strcmp(arg, "-graph")) {
			dump_graph = GF_TRUE;
		} else if (!strcmp(arg, "-info")) {
			print_filter_info = GF_TRUE;
		} else if (!strcmp(arg, "-no-block")) {
			disable_blocking = GF_TRUE;
		} else if (!strcmp(arg, "-links")) {
			view_filter_conn = GF_TRUE;
		}

		if (arg_val) {
			arg_val--;
			arg_val[0]='=';
		}
	}

	if (!logs_set) {
		gf_log_set_tool_level(GF_LOG_ALL, GF_LOG_WARNING);
	}

	if (gf_sys_get_rti(0, &rti, 0)) {
		if (dump_stats)
			fprintf(stderr, "System info: %d MB RAM - %d cores\n", (u32) (rti.physical_memory/1024/1024), rti.nb_cores);
		if (nb_threads<0) {
			nb_threads = rti.nb_cores-1;
			if (nb_threads<0) nb_threads=0;
		}
	}

	session = gf_fs_new(nb_threads, sched_type, NULL, ((list_filters==2) || print_filter_info) ? GF_TRUE : GF_FALSE, disable_blocking);
	if (!session) {
		return 1;
	}
	if (load_test_filters) gf_fs_register_test_filters(session);


	if (list_filters || print_filter_info) {
		print_filters(argc, argv, session);
		goto exit;
	}
	if (view_filter_conn) {
		gf_fs_filter_print_possible_connections(session);
		goto exit;
	}

	//all good to go, load filters
	loaded_filters = gf_list_new();
	for (i=1; i<argc; i++) {
		GF_Filter *filter;
		char *arg = argv[i];
		if (arg[0]=='-') continue;

		if (arg[0]=='@') {
			link_prev_filter = 0;
			if (strlen(arg)>1)
				link_prev_filter = atoi(arg+1);

			continue;
		}

		if (!strncmp(arg, "src=", 4) ) {
			filter = gf_fs_load_source(session, arg+4, NULL, NULL, &e);
		} else if (!strncmp(arg, "dst=", 4) ) {
			filter = gf_fs_load_destination(session, arg+4, NULL, NULL, &e);
		} else {
			filter = gf_fs_load_filter(session, arg);
		}
		if (link_prev_filter>=0) {
			GF_Filter *link_from = gf_list_get(loaded_filters, gf_list_count(loaded_filters)-1-link_prev_filter);
			if (!link_from) {
				fprintf(stderr, "Wrong filter index @%d\n", link_prev_filter);
				e = GF_BAD_PARAM;
				goto exit;
			}
			link_prev_filter = -1;
			gf_filter_set_source(filter, link_from);
		}

		if (!filter) {
			fprintf(stderr, "Failed to load filter %s\n", arg);
			e = GF_NOT_SUPPORTED;
			goto exit;
		}
		gf_list_add(loaded_filters, filter);
	}
	if (!gf_list_count(loaded_filters)) {
		gpac_usage();
		e = GF_BAD_PARAM;
		goto exit;
	}

	fprintf(stderr, "Running session, press 'q' to abort\n");
	gf_fs_post_user_task(session, gpac_fsess_task, NULL, "gpac_fsess_task");
	gf_fs_run(session);

	if (dump_stats)
		gf_fs_print_stats(session);
	if (dump_graph)
		gf_fs_print_connections(session);

exit:
	if (e) {
		gf_fs_run(session);
	}
	gf_fs_del(session);
	if (loaded_filters) gf_list_del(loaded_filters);
	gf_sys_close();
	if (e) return 1;

#ifdef GPAC_MEMORY_TRACKING
	if (gf_memory_size() || gf_file_handles_count() ) {
		gf_log_set_tool_level(GF_LOG_MEMORY, GF_LOG_INFO);
		gf_memory_print();
		return 2;
	}
#endif
	return 0;
}

GPAC_DEC_MAIN(gpac_main)


static void dump_caps(u32 nb_caps, const GF_FilterCapability *caps)
{
	u32 i;
	for (i=0;i<nb_caps; i++) {
		const char *szName;
		const char *szVal;
		char szDump[100];
		const GF_FilterCapability *cap = &caps[i];
		if (!(cap->flags & GF_FILTER_CAPS_IN_BUNDLE) && i+1==nb_caps) break;
		if (!i) fprintf(stderr, "Capabilities Bundle:\n");
		else if (!(cap->flags & GF_FILTER_CAPS_IN_BUNDLE) ) {
			fprintf(stderr, "Capabilities Bundle:\n");
			continue;
		}

		szName = cap->name ? cap->name : gf_props_4cc_get_name(cap->code);
		if (!szName) szName = gf_4cc_to_str(cap->code);
		fprintf(stderr, "\t Flags:");
		if (cap->flags & GF_FILTER_CAPS_INPUT) fprintf(stderr, " Input");
		if (cap->flags & GF_FILTER_CAPS_OUTPUT) fprintf(stderr, " Output");
		if (cap->flags & GF_FILTER_CAPS_EXCLUDED) fprintf(stderr, " Exclude");
		if (cap->flags & GF_FILTER_CAPS_EXPLICIT) fprintf(stderr, " ExplicitOnly");

		//dump some interesting predefined ones which are not mapped to types
		if (cap->code==GF_PROP_PID_STREAM_TYPE) szVal = gf_stream_type_name(cap->val.value.uint);
		else if (cap->code==GF_PROP_PID_CODECID) szVal = (const char *) gf_codecid_name(cap->val.value.uint);
		else szVal = gf_prop_dump_val(&cap->val, szDump, GF_FALSE);

		fprintf(stderr, " Type=%s, value=%s", szName,  szVal);
		if (cap->priority) fprintf(stderr, ", priority=%d", cap->priority);
		fprintf(stderr, "\n");
	}
}

static void print_filter(const GF_FilterRegister *reg)
{
	fprintf(stderr, "Name: %s\n", reg->name);
	if (reg->description) fprintf(stderr, "Description: %s\n", reg->description);
	if (reg->author) fprintf(stderr, "Author: %s\n", reg->author);
	if (reg->comment) fprintf(stderr, "Comment: %s\n", reg->comment);

	if (reg->max_extra_pids==(u32) -1) fprintf(stderr, "Max Input pids: any\n");
	else fprintf(stderr, "Max Input pids: %d\n", 1 + reg->max_extra_pids);

	fprintf(stderr, "Flags:");
	if (reg->explicit_only) fprintf(stderr, " ExplicitOnly");
	if (reg->requires_main_thread) fprintf(stderr, "MainThread");
	if (reg->probe_url) fprintf(stderr, " IsSource");
	if (reg->reconfigure_output) fprintf(stderr, " ReconfigurableOutput");
	fprintf(stderr, "\nPriority %d\n", reg->priority);

	if (reg->args) {
		u32 idx=0;
		fprintf(stderr, "Options:\n");
		while (1) {
			const GF_FilterArgs *a = & reg->args[idx];
			if (!a || !a->arg_name) break;
			idx++;

			fprintf(stderr, "\t%s (%s): %s.", a->arg_name, gf_props_get_type_name(a->arg_type)
, a->arg_desc);
			if (a->arg_default_val) {
				fprintf(stderr, " Default %s.", a->arg_default_val);
			} else {
				fprintf(stderr, " No default.");
			}
			if (a->min_max_enum) {
				fprintf(stderr, " %s: %s", strchr(a->min_max_enum, '|') ? "Enum" : "minmax", a->min_max_enum);
			}
			if (a->updatable) fprintf(stderr, " Updatable attribute.");
			fprintf(stderr, "\n");
		}
	} else {
		fprintf(stderr, "No options\n");
	}

	if (reg->nb_caps) {
		dump_caps(reg->nb_caps, reg->caps);
	}
	fprintf(stderr, "\n");
}

static void print_filters(int argc, char **argv, GF_FilterSession *session)
{
	u32 i, count = gf_fs_filters_registry_count(session);
	if (list_filters) fprintf(stderr, "Listing %d supported filters%s:\n", count, (list_filters==2) ? " including meta-filters" : "");
	for (i=0; i<count; i++) {
		const GF_FilterRegister *reg = gf_fs_get_filter_registry(session, i);
		if (print_filter_info) {
			u32 k;
			//all good to go, load filters
			for (k=1; k<argc; k++) {
				char *arg = argv[k];
				if (arg[0]=='-') continue;

				if (!strcmp(arg, reg->name) )
					print_filter(reg);
				else if (!strchr(reg->name, ':') && !strcmp(arg, "*"))
					print_filter(reg);
				else if (strchr(reg->name, ':') && !strcmp(arg, "*:*"))
					print_filter(reg);
				break;
			}
		} else {
			fprintf(stderr, "%s: %s\n", reg->name, reg->description);
		}
	}
}


