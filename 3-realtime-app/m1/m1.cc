#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

double f(double x)
{
	return 1.5*(4.8/x-(log(1.5*x))/(4.8*4.8));
}

int main(int argc, char *argv[]) {

	if (argc < 2)	// если процесс не вызван M2
	{
		std::cout << "Incorrect number of arguments" << std::endl;
		return EXIT_FAILURE;
	}

	std::cout << "M1 running" << std::endl;

	int const START_TIMER_CODE = -10;
	int const START_READ_CODE = -11;

	pid_t m2pid = getppid();

	int sharedmemid = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0);
	if (sharedmemid == -1)
	{
		std::cout << "The error occurred while creating share memory" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}
	size_t sharedmemLen = 32;
	ftruncate(sharedmemid, sharedmemLen);

	char *addr = (char*)mmap(0, sharedmemLen, PROT_READ | PROT_WRITE, MAP_SHARED, sharedmemid, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	int channid;
	if ((channid = ChannelCreate(0)) == -1)
	{
		std::cout << "The error occurred while creating channel" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	int connid;
	if ((connid = ConnectAttach(
			ND_LOCAL_NODE,		// ид узла в сети (nd=ND_LOCAL_NODE, если узел местный)
			0,					// ид процесса-сервера (M1 будет слать себе же)
			channid,			// ид канала сервера (M1 будет слать себе же)
			_NTO_SIDE_CHANNEL,	// для гарантии создания соединения с требуемым каналом
			0					// набор флагов, установка которых определяет поведение соединения по отношению к нитям процесса-клиента
			)) == -1)
	{
		std::cout << "The error occurred while connecting to channel" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct sigevent tickSigevent;		// уведомления таймера
	SIGEV_PULSE_INIT(					// уведомление типа импульс
			&tickSigevent,
			connid,						// импульс будут слаться по каналу M1 самому процессу M1
			SIGEV_PULSE_PRIO_INHERIT,	// приоритет по-умолчанию
			-1,							// код импульса не интересует
			0);							// значение импульса

	timer_t tickTimerid;
	if (timer_create(CLOCK_REALTIME, &tickSigevent, &tickTimerid) == -1)	// таймер будет слать импульсы
	{
		std::cout << "The error occurred while creating timer" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct itimerspec tickTimerProps;
	tickTimerProps.it_value.tv_sec = 0;
	tickTimerProps.it_value.tv_nsec = 50000000;
	tickTimerProps.it_interval.tv_sec = 0;
	tickTimerProps.it_interval.tv_nsec = 50000000;

	char msg[32];				// сообщение импульса
	double y = 0;
	char buf[sharedmemLen];
	double seconds = 0;

	timer_settime(tickTimerid, 0, &tickTimerProps, NULL);

	if (SignalKill(				// отправка сигнала родительскому процессу о том, что он может запускать таймер
			ND_LOCAL_NODE,		// местный узел
			m2pid,				// ид процесса, которому посылается сигнал
			0,					// значение ид нити, которой посылается сигнал (не важно)
			SIGUSR1,			// системный номер посылаемого сигнала
			START_TIMER_CODE,	// ассоциируемые с сигналом некоторый код и некоторое значение, позволяющие передать вместе с сигналом дополнительную информацию
			0))					// значение (не важно)
	{
		std::cout << "The error occurred while sending START_TIMER signal" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	while (true)
	{
		y = f(seconds);
		sprintf(buf, "%f", y);	// преобразовать y в строку
		strcpy(addr, buf);		// копировать результат функции в им. память

		if (seconds == 0)		// родительский процесс может начинать читать (в им. памяти уже есть запись)
		{
			if (SignalKill(ND_LOCAL_NODE, m2pid, 0, SIGUSR1, START_READ_CODE, 0))
			{
				std::cout << "The error occurred while sending READY_TO_START_READ signal" << std::endl;
				std::cout << strerror(errno) << std::endl;
				return EXIT_FAILURE;
			}
		}

		// заблокировать M1, пока не получит импульс (а импульс шлёт таймер)
		MsgReceivePulse(channid, &msg, sizeof(msg), NULL);

		seconds += 0.05;	// увеличиваем время
	}

	return EXIT_SUCCESS;
}
