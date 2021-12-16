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

// функция из варианта
double getFunctionResult(double t)
{
	return 1.5*(4.8/t-(log(1.5*t))/(4.8*4.8));
}

int main(int argc, char *argv[]) {

	if (argc < 2)	// если процесс не вызван из другого
	{
		std::cout << "Incorrect number of arguments." << std::endl;
		return EXIT_FAILURE;
	}

	std::cout << "M1 running" << std::endl;

	int const START_TIMER_CODE = -10;
	int const START_READ_CODE = -11;

	pid_t ppid = getppid();	// pid родительского процесса

	// присоединяем именнованную область памяти к процессу
	int fd = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0);
	if (fd == -1)
	{
		std::cout << "The error occurred while creating share memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	size_t sharedMemoryLength = 32;	// размер именнованной памяти
	ftruncate(fd, sharedMemoryLength);

	// отображение именованной память в адресное пространство процесса
	char *addr = (char*)mmap(0, sharedMemoryLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// Создаем канал
	int chid;
	if ((chid = ChannelCreate(0)) == -1)
	{
		std::cout << "The error occurred while creating channel." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// устанавливаем соединение с собственным каналом
	int coid = 0;
	if ((coid = ConnectAttach(
			ND_LOCAL_NODE,		// ID узла в сети (nd=ND_LOCAL_NODE, если узел местный)
			0,					// ID процесса-сервера
			chid,				// ID канала сервера
			_NTO_SIDE_CHANNEL,	// для гарантии создания соединения с требуемым каналом
			0					// набор флагов, установка которых определяет поведение соединения по отношению к нитям процесса-клиента
			)) == -1)
	{
		std::cout << "The error occurred while connecting to channel." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct sigevent event;				// уведомления таймера
	SIGEV_PULSE_INIT(					// уведомление типа импульс
			&event,
			coid,						// ID соединения (связи) с каналом, по которому уведомляющий импульс будет посылаться
			SIGEV_PULSE_PRIO_INHERIT,	// приоритет, связываемый с импульсом, который будет наследоваться нитью, принявшей импульс
			-1,							// код импульса
			0);							// значение импульса

	timer_t timerid; // id таймера
	if (timer_create(CLOCK_REALTIME, &event, &timerid) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct itimerspec timer;			// свойства таймера
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_nsec = 50000000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 50000000;

	char msg[32];								// сообщение импульса
	double functionResult = 0;					// результат функции
	char buffer[sharedMemoryLength];			// буфер для записи результата функции
	double time = 0;							// время

	timer_settime(timerid, 0, &timer, NULL);	// запуск таймера на запись значения функции в буфер

	if (SignalKill(				// отправка сигнала родительскому процессу о том, что он может запускать таймер
			ND_LOCAL_NODE,		// местный узел
			ppid,				// ID процесса, которому посылается сигнал
			0,					// tid задаёт значение ID нити, которой посылается сигнал
			SIGUSR1,			// системный номер посылаемого сигнала
			START_TIMER_CODE,	// ассоциируемые с сигналом некоторый код и некоторое значение, позволяющие передать вместе с сигналом дополнительную информацию
			0))					// значение
	{
		std::cout << "The error occurred while sending START_TIMER signal." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	while (true)
	{

		functionResult = getFunctionResult(time);	// результат функции
		sprintf(buffer, "%f", functionResult);		// преобразовать в строку
		strcpy(addr, buffer);						// копировать результат функции в именованную память

		if (time == 0)	// родительский процесс может начинать читать
		{
			if (SignalKill(ND_LOCAL_NODE, ppid, 0, SIGUSR1, START_READ_CODE, 0))
			{
				std::cout << "The error occurred while sending READY_TO_START_READ signal." << std::endl;
				std::cout << strerror(errno) << std::endl;
				return EXIT_FAILURE;
			}
		}

		MsgReceivePulse(chid, &msg, sizeof(msg), NULL);

		time += 0.05;	// увеличиваем время
	}

	return EXIT_SUCCESS;
}
