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

volatile bool isReadyToStartTimer = false;	// готов ли процесс запустить таймер
volatile bool isReadyToStartRead = false;	// готов ли процесс начать чтение

FILE* file;									// файл для записи трендов

// ********** ЗАВЕРШИТЬ *****************************
void terminate(union sigval arg)
{
	std::cout << "Terminate all processes" << std::endl;

	fclose(file);					// закрыть файл трендов

	kill(arg.sival_int, SIGKILL);	// убить того, что послал
	kill(getpid(), SIGKILL);		// убить текущий процесс
}

// ********** ЗАПИСЬ ********************************
void writeToFile(char* functionValue, char* time)
{
	char text[200] = "";

	strcat(text, functionValue);
	strcat(text, "\t");
	strcat(text, time);
	strcat(text, "\n");

	fputs(text, file);
}

// ********** ОБРАБОТЧИК СИГНАЛОВ *******************
/* Код "int si_code" сигнала, который формируется инициатором сигнала
 * и служит в качестве дополнительной информации,
 * например, для идентификации источника и/или причины посылки сигнала.
 *
 * Значение si_code является 8-разрядным целым со знаком.
 * Важно отметить, что пользовательскими значениями могут быть значения
 * в диапазоне -128 <= si_code <= 0 являются.
 * А вот значения 0 < signo <= 127 являются сугубо системными значениями,
 * генерируемыми ядром, и не могут использоваться пользователями. */
void handler(int signo, siginfo_t *info, void *over)
{
	switch (info->si_code)
	{
		case -10:
			std::cout << "Received signal with code \"READY_TO_START_TIMER\"" << std::endl;
			isReadyToStartTimer = true;
			break;

		case -11:
			std::cout << "Received signal with code \"READY_TO_START_READ\"" << std::endl;
			isReadyToStartRead = true;
			break;

		default:
			std::cout << "Received signal with unknown code" << std::endl;
	}
}

int main(int argc, char *argv[]) {

	std::cout << "M2 running" << std::endl;

	// ********** ДИСПОЗИЦИЯ СИГНАЛА ********************
	/* Задание диспозиции пользовательского сигнала,
	 * отвечающего за запуск таймера
	 * и начало чтения из именованной памяти.
	 *
	 * Сигнал SIGUSR1 и SIGUSR2, не инициируемый ядром,
	 * а только пользователем или прикладным процессом.
	 *
	 * Определение обработчика для сигнала SIGUSR1 так,
	 * что когда он запускается,
	 * маскируются все сигналы набора.
	 *
	 * Механизм надёжных сигналов предоставляет нитям процесса
	 * возможность блокировать (задерживать) действие сигнала на нить.
	 *
	 * Режим учёта поступления сигнала "sa_flags=0" таков,
	 * что многократное инициирование процессу или нити одного и того же сигнала
	 * при условии, что сигнал заблокирован
	 * или нить находится в ожидании выделения процессорного времени,
	 * оставляет актуальной только одну последнюю инициацию сигнала.
	 *
	 * Маска sa_mask используется для блокирования сигналов,
	 * в течение времени выполнения обработчика сигнала. */

	sigset_t signalSet;				// процесс формирует контролируемый им набор сигналов
	sigemptyset(&signalSet);		// инициализирует набор сигналов, очищая все биты
	sigaddset(&signalSet, SIGUSR1);	// добавляет сигнал

	struct sigaction act;
	act.sa_flags = 0;				// учитывать последнюю инициацию сигнала
	act.sa_mask = signalSet;		// для блокирования сигналов
	act.sa_sigaction = &handler;	// указатель обработчика сигналов
	sigaction(SIGUSR1, &act, NULL);	// обработать сигнал

	int signo;						// для номера сигнала
	sigset_t timerSignalSet;
	sigemptyset(&timerSignalSet);
	sigaddset(&timerSignalSet, SIGUSR2);

	// ********** ИМЕННОВАННАЯ ПАМЯТЬ *******************
	/* Функция shm_open() создаёт и/или присоединяет к процессу
	 * существующую именованную память с указанным именем
	 * и возвращает дескриптор именованной памяти как дескриптор файла
	 * с установленным флагом FD_CLOEXEC.
	 *
	 * Аргумент name содержит символьную строку с именем именованной памяти.
	 * Имя памяти должно начинаться с символа слеш </> и содержать только один слеш.
	 *
	 * Режим доступа к содержимому именованной памяти определяется значениями флагов,
	 * указанных в аргументе oflag.
	 * Значение oflag формируется операцией поразрядного логического сложения флагов.
	 *
	 * O_CREAT - создать новую и/или присоединиться к существующей именованной памяти.
	 * O_RDWR - открыть для чтения и записи.
	 * O_TRUNC - если именованная память существует, и она успешно присоединена с флагом O_RDWR,
	 * то память урезается до нулевой длины, а права доступа владельца и владелец не изменяются.
	 *
	 * Аргумент mode используется процессом для формирования прав доступа пользователей
	 * к создаваемой именованной памяти таким же образом, как при создании обычного файла.
	 *
	 * В результате выполнения функции mmap в адресное пространство процесса
	 * отображается выделенная в пределах именованной памяти с дескриптором fd область,
	 * начинающаяся со смещением off от её начала и длиною len байтов.
	 * Доступ к этой области именованной памяти в адресном пространстве процесса
	 * начинается с адреса, возвращённого функцией mmap().
	 * Аргумент addr – указывает желательное место в адресном пространстве процесса,
	 * куда именованная память должна быть отображена.
	 * Обычно нет необходимости указывать какое-то конкретное значение addr,
	 * можно просто задать NULL.
	 * Если устанавливается не NULL , то будет ли объект памяти отображён,
	 * зависит от того, установлен ли флаг MAP_FIXED в аргументе flags.
	 *
	 * Если MAP_FIXED установлен, то область именованной памяти отображается с адреса addr,
	 * или функция завершается с ошибкой.
	 * Если MAP_FIXED не установлен, то значение addr рассматривается как желаемый адрес
	 * начала области доступа к именованной памяти в адресном пространстве процесса,
	 * но не обязательный и заданное значение addr может быть проигнорировано.
	 * Аргумент prot определяет порядок использования именованной памяти.
	 * Можно устанавливать следующие флаги использования (определены в <sys/mman.h>).
	 * Аргумент prot определяет порядок использования именованной памяти. */

	int fd = shm_open("/sharemap", O_CREAT | O_RDWR | O_TRUNC, 0); // дескриптор памяти
	if (fd == -1)
	{
		std::cout << "The error occurred while creating share memory" << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	size_t sharedMemoryLength = 32;		// длина в байтах
	ftruncate(fd, sharedMemoryLength);	// установка размера памяти

	// отображаем именованную память в адресное пространство процесса
	char* addr = (char*)mmap(0, sharedMemoryLength, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
	{
		std::cout << "The error occurred while mapping shared memory." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// ********** ПРОЦЕСС *******************************
	/* Родительский процесс не блокируется,
	 * выполняется параллельно с дочерним процессом
	 * и должен ожидать завершения дочернего процесса. */
	// запускаем дочерний процесс на основе модуля M1
	int cid = spawnl(P_NOWAIT, "/home/host/m1/x86/o/m1", "/home/host/m1/x86/o/m1", "/home/host/m1/x86/o/m1", NULL);
	if (cid == -1)
	{
		std::cout << "The error occurred while starting new process." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	// ********** ТАЙМЕР ********************************
	/* В случае, когда посылаемый сигнал адресуется процессу без какой-либо дополнительной информации,
	 * то для настройки уведомления используется макрокоманда SIGEV_SIGNAL_INIT.
	 *
	 * CLOCK_REALTIME - стандартный POSIX-определенный тип часов реального времени.
	 * Таймер должен будет сработать, даже если процессор находится
	 * в режиме экономии энергопотребления.
	 *
	 * Абсолютный таймер всегда однократный. Он посылает уведомление один раз,
	 * как только текущее значение реального времени окажется не меньше значения,
	 * указанного в alarm.it_value.
	 * Значение alarm.it_interval для абсолютного таймера игнорируется.
	 *
	 * Чтобы получить относительный периодический таймер, необходимо,
	 * чтобы значения времени в alarm.it_value и alarm.it_interval были отличны от нуля.
	 * Таймер первый раз пошлёт уведомление,
	 * как только истекший интервал реального времени окажется не меньше значения,
	 * указанного в alarm.it_value, и с этого момента будет периодически посылать уведомления,
	 * как только истекший с момента предыдущего уведомления интервал реального времени
	 * окажется не меньше значения, указанного в alarm.it_interval.
	 *
	 * Тип уведомления SIGEV_THREAD_INIT предполагает, что в результате срабатывания таймера
	 * в процессе создаётся новая нить. */

	struct sigevent event;				// уведомления нити о наступлении заданного момента времени
	SIGEV_SIGNAL_INIT(&event, SIGUSR2);	// создание уведомления

	timer_t timerid;					// таймер
	if (timer_create(CLOCK_REALTIME, &event, &timerid) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct itimerspec timer;			// свойства таймера
	timer.it_value.tv_sec = 0;			// абсолютный момент времени или период первого уведомления
	timer.it_value.tv_nsec = 200000000;
	timer.it_interval.tv_sec = 0;		// период последующих уведомлений
	timer.it_interval.tv_nsec = 200000000;

	struct sigevent threadEvent;
	SIGEV_THREAD_INIT(&threadEvent,
			terminate,					// указатель на функцию, которую нужно запустить как нить
			cid,						// значение, которое передается функции, запускаемой как нить
			NULL);

	timer_t stopTimerId;
	if (timer_create(CLOCK_REALTIME, &threadEvent, &stopTimerId) == -1)
	{
		std::cout << "The error occurred while creating timer." << std::endl;
		std::cout << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	struct itimerspec stopTimer;
	stopTimer.it_value.tv_sec = 57;
	stopTimer.it_value.tv_nsec = 0;
	stopTimer.it_interval.tv_sec = 0;
	stopTimer.it_interval.tv_nsec = 0;

	// инициализация до старта отсчёта
	file = fopen("/home/host/trend", "wt");
	char bufferFunctionResult[sharedMemoryLength];
	char bufferTime[sizeof(double)];
	double time = 0;

	while (!isReadyToStartTimer);	// ждать сигнал для запуска таймера
	timer_settime(					// запускаем таймера считывания значений из именованной память
			timerid,				// запускаемый таймер
			0,						// относительный таймер
			&timer,					// установки моментов срабатывания таймера
			NULL					// возвращает значение предыдущей установки таймера
	);
	timer_settime(					// запускаем таймер на завершение всех процессов
			stopTimerId, 0, &stopTimer, NULL);

	while (true)
	{
		while (!isReadyToStartRead);					// ожидание сигнала для чтения первого значения

		strcpy(bufferFunctionResult, addr);				// копируем результат функции в именованную память
		sprintf(bufferTime, "%f", time);				// преобразовать double в строку
		writeToFile(bufferFunctionResult, bufferTime);	// записать тренд в файл

		sigwait(&timerSignalSet, &signo);				// ждать сигнал

		time += 0.2;									// увеличить время
	}

	return EXIT_SUCCESS;
}
