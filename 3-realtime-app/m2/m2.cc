#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <iostream>
#include <signal.h>
#include <sys/siginfo.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/siginfo.h>
#include <sys/neutrino.h>

volatile bool isReadyToStartTimer = false;
volatile bool isReadyToStartRead = false;

FILE* trendFile;

void terminateApp(union sigval arg)
{
	std::cout << "Terminate all processes" << std::endl;

	fclose(trendFile);

	kill(arg.sival_int, SIGKILL);	// убить M1
	kill(getpid(), SIGKILL);		// убить M2
}

void writeToTrendFile(char* y, char* x)
{
	char line[200] = "";

	strcat(line, y);
	strcat(line, "\t");
	strcat(line, x);
	strcat(line, "\n");

	fputs(line, trendFile);
}

void raiseStartWorkFlags(int signo, siginfo_t *info, void *over)
{
	switch (info->si_code)	// si_code – дополнительная информация
	{
		case -10:
			std::cout << "Received signal with code \"READY_TO_START_TIMER\"" << std::endl;
			isReadyToStartTimer = true;
			break;

		case -11:
			std::cout << "Received signal with code \"READY_TO_START_READ\"" << std::endl;
			isReadyToStartRead = true;
			break;

		default:			// коды определяемые программистом от -128 до 0 включительно
			std::cout << "Received signal with unknown code" << std::endl;
			break;
	}
}

int main(int argc, char *argv[]) {

	std::cout << "M2 running" << std::endl;

	sigset_t raiseStartWorkFlagsSigset;			// процесс формирует контролируемый им набор сигналов
	sigemptyset(&raiseStartWorkFlagsSigset);	// инициализировать набор сигналов, очищая все биты
	sigaddset(
			&raiseStartWorkFlagsSigset,			// добавить сигнал, на который будет реагировать процесс
			SIGUSR1);							// этот сигнал генерируется кодом программиста (не ОС)

	struct sigaction raiseStartWorkFlagsSigact;
	raiseStartWorkFlagsSigact.sa_flags = 0;							// учитывать только последнюю инициацию сигнала
	raiseStartWorkFlagsSigact.sa_mask = raiseStartWorkFlagsSigset;	// во время обработки этого сигнала, блокировать получения новых сигналов
	raiseStartWorkFlagsSigact.sa_sigaction = &raiseStartWorkFlags;	// вызвать этот обработчик, при получении такого сигнала
	sigaction(
			SIGUSR1,												// когда приходит этот сигнал (его пошлёт M1)
			&raiseStartWorkFlagsSigact,								// тогда поднять флаги начала считывания данных
			NULL);

	int sharedmemid = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0);	// создать или присоед. им. память
	if (sharedmemid == -1)													// если не удалось подключить им. память.
	{
		std::cout << "The error occurred while creating share memory" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}
	size_t sharedmemLen = 32;				// длина в байтах
	ftruncate(sharedmemid, sharedmemLen);	// установка размера памяти

	// char* – указатель на последовательность байтов
	char* addr = (char*)mmap(							// addr – желательное место в адресном пространстве процесса, куда именованная память должна быть отображена
			0, sharedmemLen, PROT_WRITE | PROT_READ,
			MAP_SHARED,									// отображаемая область памяти может разделяться с другими процессами
			sharedmemid, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	int m1pid = spawnl(
			P_NOWAIT,					// род. проц. выполняется пар. с дочерним и должен ожидать дочерний проц.
			"/home/host/m1/x86/o/m1",	// запускаем дочерний процесс на основе модуля M1
			"/home/host/m1/x86/o/m1", "/home/host/m1/x86/o/m1", NULL);
	if (m1pid == -1)
	{
		std::cout << "The error occurred while starting new process" << std::endl;
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
			0,					// ид узла в сети (nd=ND_LOCAL_NODE, если узел местный)
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
			connid,						// импульс будут слаться по каналу M2 самому процессу M2 (нужно для синхронизации)
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
	tickTimerProps.it_value.tv_nsec = 200000000;
	tickTimerProps.it_interval.tv_sec = 0;
	tickTimerProps.it_interval.tv_nsec = 200000000;

	char msg[32];			// сообщение импульса

	struct sigevent timeoverEvent;
	SIGEV_THREAD_INIT(
			&timeoverEvent,	// событие завершения работы приложения
			terminateApp,	// указатель на функцию, которую нужно запустить как нить
			m1pid,			// значение, которое передаётся функции, запускаемой как нить
			NULL);

	timer_t timeoverTimerid;
	if (timer_create(CLOCK_REALTIME, &timeoverEvent, &timeoverTimerid) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct itimerspec timeoverTimerProps;
	timeoverTimerProps.it_value.tv_sec = 57;		// сработает через 57 секунд
	timeoverTimerProps.it_value.tv_nsec = 0;
	timeoverTimerProps.it_interval.tv_sec = 0;
	timeoverTimerProps.it_interval.tv_nsec = 0;

	trendFile = fopen("/home/host/trend", "wt");
	char y[sharedmemLen];							// считывать в этот массив из именнованой памяти
	char x[sizeof(double)];							// аргументов функции будет пройденное время
	double seconds = 0;								// сколько секунд прошло

	while (!isReadyToStartTimer);	// ждать сигнал для запуска таймеров

	timer_settime(					// запустить таймер считывания значений из именованной память
			tickTimerid,			// запускаемый таймер
			0,						// относительный таймер
			&tickTimerProps,		// установка моментов срабатывания таймера
			NULL					// не возвращать значение предыдущей установки таймера
	);
	timer_settime(timeoverTimerid, 0, &timeoverTimerProps, NULL); // запуcтить отсчёт до завершения работы приложения

	while (true)
	{
		while (!isReadyToStartRead);		// ожидать разрешение от модуля M1 на чтение значения из им. памяти

		strcpy(y, addr);					// копировать результат функции из им. памяти
		sprintf(x, "%f", seconds);			// преобразовать time в строковое представление – получим x
		writeToTrendFile(y, x);				// записать тренд в файл

		// заблокировать M2, пока не получит импульс (а импульс шлёт таймер)
		MsgReceivePulse(channid, &msg, sizeof(msg), NULL);

		seconds += 0.2;	// увеличить счётчик прощедших секунд
	}

	return EXIT_SUCCESS;
}
