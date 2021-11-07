#include <cstdlib>
#include <iostream>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>

// Семафор для согласования M и T1
sem_t SEMAPHORE;
// Условная переменная и мутекс для согласования T1 и T2
pthread_mutex_t COND_MUTEX;
pthread_cond_t COND_VAR;
bool HAS_T1_FINISHED = false;
// Индекс вписывания символа в текст.
int TEXT_INDEX_FOR_SYMBOL = 0;

// "Вписывание" символа в конец текста
void append_symbol_to_text(char* text, char symbol) {
	// Ждём тысячу итераций
	for (int i = 0; i < 1000; i++)
		;
	// Индекс от начала текста, затем разыменование указателя
	*(text + TEXT_INDEX_FOR_SYMBOL) = symbol;
}

void* t1_func(void* args) {
	char* text = (char*) args;
	const char T1_TEXT[] = "Text1, ";

	// T1 ждёт, когда M увеличит счётчик, чтобы вписать свою строку
	std::cout << "T1 waiting M" << std::endl;
	sem_wait(&SEMAPHORE);

	// Захват мутекса для согласования с T2
	pthread_mutex_lock(&COND_MUTEX);

	std::cout << "T1 writing" << std::endl;
	for (unsigned int i = 0; i < strlen(T1_TEXT); i++) {
		append_symbol_to_text(text, T1_TEXT[i]);
		TEXT_INDEX_FOR_SYMBOL++;
	}
	// Текущая нить закончила ввод своих символов.
	// Увеличить счётчик семафора на 1 для согласования хода нити с нитью M
	sem_post(&SEMAPHORE);

	// Освобождение мутекса для T2
	HAS_T1_FINISHED = true;
	// Отправка уведомления по условной переменной
	pthread_cond_signal(&COND_VAR);
	pthread_mutex_unlock(&COND_MUTEX);

	// Оповещение о конце работы F1
	std::cout << "T1 ending" << std::endl;
	return EXIT_SUCCESS;
}

void* t2_func(void* args) {
	char* text = (char*) args;
	const char T2_TEXT[] = "Text2.\n\0";

	// T2 ждёт, когда T1 освободит мутекс
	std::cout << "T2 waiting T1" << std::endl;
	pthread_mutex_lock(&COND_MUTEX);
	// Ждать, пока T1 нить не закончит запись
	while (!HAS_T1_FINISHED) {
		pthread_cond_wait(&COND_VAR, &COND_MUTEX);
	}

	std::cout << "T2 writing" << std::endl;
	for (unsigned int i = 0; i < strlen(T2_TEXT) + 1; i++) {
		append_symbol_to_text(text, T2_TEXT[i]);
		TEXT_INDEX_FOR_SYMBOL++;
	}
	// Отправка уведомления по условной переменной
	pthread_cond_signal(&COND_VAR);
	pthread_mutex_unlock(&COND_MUTEX);

	std::cout << "T2 ending" << std::endl;
	return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
	// "Общая" строка трёх нитей.
	// В конце программы должна содержать "Text0, Text1, Text2.\n".
	// 22 символов, потому что вместе с признаком конца строки '\0'.
	char text[100];

	// Создание семафора.
	// 0 – семафор не разделяется нитями других процессов,
	// 0 – значение семафора.
	sem_init(&SEMAPHORE, 0, 0);
	// Создание мутекса и условной переменной
	pthread_mutex_init(&COND_MUTEX, NULL);
	pthread_cond_init(&COND_VAR, NULL);

	// Создание и запуск нити T1
	pthread_t t2_thread;
	pthread_create(&t2_thread, NULL, (void*(*)(void*)) t2_func, (void*) text);
	pthread_t t1_thread;
	pthread_create(&t1_thread, // дескриптор нити
			NULL, // атрибутная запись
			(void*(*)(void*)) t1_func, // функция нити
			(void*) text); // аргументы функции нити
	// Создание и запуск нити T2


	// Символы, которые должна вписать в text текущая нить main
	const char M_TEXT[] = "Text0, ";
	// Запись символов в текст нитью M.
	// Цикл по кол-ву символов в M_TEXT ("Text0, ")
	std::cout << "M writing" << std::endl;
	for (unsigned int i = 0; i < strlen(M_TEXT); i++) {
		append_symbol_to_text(text, M_TEXT[i]);
		// В след. раз вписать в след. позицию
		TEXT_INDEX_FOR_SYMBOL++;
	}
	// Текущая нить закончила ввод своих символов,
	// увеличить счётчик семафора на 1
	sem_post(&SEMAPHORE);

	// M ждёт, когда T1 впишет свои символы
	std::cout << "M waiting T1" << std::endl;
	sem_wait(&SEMAPHORE);
	// Удалить семафор
	sem_destroy(&SEMAPHORE);

	// М ждёт, когда T2 впишет символы,
	// согласование M и T2
	void *value_ret;
	std::cout << "M waiting T2" << std::endl;
	pthread_join(t2_thread, &value_ret);

	// Вывод результата согласованной работы трёх нитей
	std::cout << text << std::endl;
	// Оповещение о конце работы нити M
	std::cout << "M ending" << std::endl;
	return EXIT_SUCCESS;
}
