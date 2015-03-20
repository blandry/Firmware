#include <nuttx/config.h>
#include <sched.h>
#include <sys/types.h> // main_t
#include <systemlib/systemlib.h> // task_spawn_cmd

#include <cstdio>
#include <unistd.h>

#include "daemon.hpp"
#include "factory_addresses.hpp"
#include "io_multiplexer_flags.hpp"
#include "io_multiplexer_global.hpp"
#include "io_tty.hpp"
#include "laird/configure.hpp"
#include "laird/service_io.hpp"
#include "laird/service_state.hpp"
#include "unique_file.hpp"
#include "util.hpp"

#include "read_write_log.hpp"

namespace BT
{
namespace Daemon
{
namespace Service
{

const char PROCESS_NAME[] = "bt21_service";

static volatile bool
should_run = false;

static volatile bool
running = false;

static volatile bool
started = false;

enum class Mode : uint8_t
{
	UNDEFINED,
	ONE_CONNECT,
	LISTEN,
};

static volatile Mode
daemon_mode = Mode::UNDEFINED;

static Address6
connect_address;

static int
daemon()
{
	using namespace BT::Service::Laird;

	running = true;
	started = false;
	fprintf(stderr, "%s starting ...\n", PROCESS_NAME);

	unique_file dev = tty_open("/dev/btcmd");// TODO name #define/constexpr

	//DevLog log_dev(fileno(dev), 2, "bt21_io      ", "bt21_service ");
	auto & log_dev = dev;

	auto & mp = Globals::Multiplexer::get();
	ServiceState svc;
	ServiceBlockingIO<decltype(log_dev)> service_io(log_dev, svc);

	should_run = (daemon_mode != Mode::UNDEFINED
		and fileno(dev) > -1
		and configure_latency(service_io)
		and configure_general(service_io, daemon_mode == Mode::LISTEN)
		and configure_factory(service_io)
	);

	if (should_run) { fprintf(stderr, "%s started.\n", PROCESS_NAME); }
	else
	{
		fprintf(stderr, "%s start failed: %i %s.\n"
				, PROCESS_NAME
				, errno
				, strerror(errno)
		);
	}

	started = true;
	while (should_run)
	{
		wait_process_event(service_io);
		set_xt_ready_mask(mp, svc.xt_flow);
		if (count_connections(svc.conn) > 0)
		{
			// TODO request rssi
		}
		else if (allowed_connection_request(svc.conn))
		{
			if (daemon_mode == Mode::ONE_CONNECT)
				request_connect(log_dev, svc, connect_address);
		}
	}

	started = running = false;
	fprintf(stderr, "%s stopped.\n", PROCESS_NAME);
	return 0;
}

bool
is_running() { return running; }

bool
has_started() { return started; }

void
report_status(FILE * fp)
{
	fprintf(fp, "%s %s.\n"
		, PROCESS_NAME
		, is_running() ? "is running" : "is NOT running"
	);
}

void
start(const char mode[], const char addr_no[])
{
	if (running)
		return;

	if (streq(mode, "one-connect"))
		daemon_mode = Mode::ONE_CONNECT;
	else if (streq(mode, "listen"))
		daemon_mode = Mode::LISTEN;
	else
		daemon_mode = Mode::UNDEFINED;

	if (daemon_mode == Mode::UNDEFINED)
	{
		if (streq(mode, "loopback-test"))
			fprintf(stderr, "The '%s' mode doesn't use %s.\n"
				, mode
				, PROCESS_NAME
			);
		else
			fprintf(stderr, "%s: Invaid mode: %s.\n"
				, PROCESS_NAME
				, mode
			);
		return;
	}

	if (daemon_mode == Mode::ONE_CONNECT)
	{
		uint32_t i;
		if (not parse_uint32(addr_no, i) or i >= n_factory_addresses)
			return;

		connect_address = factory_addresses[i];
	}

	task_spawn_cmd(PROCESS_NAME,
			SCHED_DEFAULT,
			SCHED_PRIORITY_DEFAULT,
			CONFIG_TASK_SPAWN_DEFAULT_STACKSIZE,
			(main_t)daemon,
			nullptr);
}

void
request_stop()
{
	should_run = false;
	fprintf(stderr, "%s stop requested.\n", PROCESS_NAME);
}

}
// end of namespace Service
}
// end of namespace Daemon
}
// end of namespace BT
