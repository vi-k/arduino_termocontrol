/***********************************************************************************************************
 * Работа с 4-знаковым семисегментным индикатором, подключенным напрямую к контроллеру
 */
#include <Arduino.h>
#include "indicator.h"

uint8_t g_indicator[4]; /* Значения на индикаторе */
uint8_t g_indicator_i = 0; /* Счётчик для динамической индикации */

/* Массив изображений цифр для индикатора */
const uint8_t c_digits[] = {
  DIGIT_0, DIGIT_1, DIGIT_2, DIGIT_3, DIGIT_4,
  DIGIT_5, DIGIT_6, DIGIT_7, DIGIT_8, DIGIT_9
};

/* Режимы индикатора
 *  Предделители для таймера 2:
 *  (счётчик таймера увеличивается на единицу после переполнения предделителя)
 *  001: 1 такт;
 *  010: 8 тактов;
 *  011: 32 такта;
 *  100: 64 такта;
 *  101: 128 тактов;
 *  110: 256 тактов;
 *  111: 1024 такта.
 */
const uint8_t c_indicator_prescalers_on[] =  {0b010, 0b010, 0b010, 0b100}; /* время "горения" каждого знака */
const uint8_t c_indicator_prescalers_off[] = {0b110, 0b101, 0b100,     0}; /* пауза перед следующим циклом */

/* Выбор режима индикации заметно влияет на энергопотребление. Разница между максимальным и предыдущим
 *  по энергозатратам - почти в три раза.
 */
const uint8_t c_max_brightness = sizeof(c_indicator_prescalers_on) / sizeof(*c_indicator_prescalers_on) - 1;
uint8_t g_brightness = c_max_brightness;

/***********************************************************************************************************
 * Настройка таймера TIMER2 для динамической индикации
 */
void init_indicator()
{
  /* Настраиваем таймер для динамической индикации */
  TCCR2A = 0; /* Используем обычный (Normal) режим работы таймера */
  TCCR2B = c_indicator_prescalers_on[g_brightness]; /* Устанавливаем предделитель */
  TCNT2 = 0;
  TIMSK2 = (1 << TOIE2); /* Запускаем таймер - он будет работать всегда, кроме режима глубокого сна */
}

/***********************************************************************************************************
 * Обработка таймера TIMER2 - динамическая индикация
 */
ISR(TIMER2_OVF_vect)
{
  /* Отключаем индикаторы (катоды к питанию) */
  PORTC |= 0b00111100;

  /* В самом ярком режиме "пауза" не используется */
  if (g_brightness == c_max_brightness && g_indicator_i == 4) g_indicator_i = 0;

  if (g_indicator_i == 4) {
    /* "Пауза" - на время выключаем экран, чтобы уменьшить яркость */
    TCCR2B = c_indicator_prescalers_off[g_brightness];
    g_indicator_i = 0;
  }
  else {
    TCCR2B = c_indicator_prescalers_on[g_brightness];
    
    /* Данные для индикации берём из глобального массива g_indicator[] */
    PORTB = g_indicator[g_indicator_i];
    PORTC &= ~(1 << (5 - g_indicator_i)) | 0b11000011; /* Нужный катод на землю*/

    g_indicator_i++;
  }
}

/***********************************************************************************************************
 * Установка яркости
 */
void set_brightness(int8_t brightness)
{
  if (brightness < 0) brightness = 0;
  else if (brightness > c_max_brightness) brightness = c_max_brightness;
  g_brightness = brightness;
}

/***********************************************************************************************************
 * Яркость
 */
int8_t get_brightness()
{
  return g_brightness;
}

/***********************************************************************************************************
 * Очистка индикатора
 */
void clear_indicator()
{
  g_indicator[0] = 0;
  g_indicator[1] = 0;
  g_indicator[2] = 0;
  g_indicator[3] = 0;

  PORTB = 0; /* Аноды на землю */
  PORTC |= 0b00111100; /* Катоды к питанию */
}

/******************************************************************************
 * Вывод сразу всех значений в память
 */
void show_to(uint8_t *mem, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
  mem[0] = d0;
  mem[1] = d1;
  mem[2] = d2;
  mem[3] = d3;
}

/******************************************************************************
 * Вывод сразу всех значений на индикатор
 */
void show(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
  g_indicator[0] = d0;
  g_indicator[1] = d1;
  g_indicator[2] = d2;
  g_indicator[3] = d3;
}

/******************************************************************************
 * Вывод одного значения в память
 */
void show_to(uint8_t *mem, uint8_t d, uint8_t place)
{
  if (place >= 0 && place <= 3) mem[place] = d;
}

/******************************************************************************
 * Вывод одного значения на индикатор
 */
void show(uint8_t d, uint8_t place)
{
  if (place >= 0 && place <= 3) g_indicator[place] = d;
}

/******************************************************************************
 * Вывод числа с фиксированной запятой в память
 *  mem - адрес памяти;
 *  num - выводимое число;
 *  decimals - кол-во знаков после запятой;
 *  begin, end - начальный и конечный индикаторы для вывода числа (от 0 до 3);
 *  space - заполняющий символ вместо пробелов.
 */
bool show_fix_to(uint8_t *mem, int num, uint8_t decimals,
    uint8_t begin, uint8_t end, uint8_t space)
{
  bool negative = false;
  int pos_of_dp = end - decimals;
  
  /* Для отрицательного числа выделяем положительную часть, а минус запоминаем */
  if (num < 0) {
    negative = true;
    num = -num;
  }
  
  /* Выводим число поразрядно - от единиц и далее - пока число не "закончится",
    либо пока не закончится место для числа */
  for (int i = end; i >= begin; i--) {
    /* В конце выводим минус */
    if (num == 0 && i < pos_of_dp && negative) {
      mem[i] = SIGN_MINUS; /* Минус */
      negative = false;
    }
    /* Выводим числа поразрядно. Вместо ведущих нулей - пробелы */
    else {
      uint8_t n = (num > 0 || i >= pos_of_dp ? c_digits[num % 10] : space);
      if (decimals != 0 && i == pos_of_dp) n |= SIGN_DP; /* Точка */
      mem[i] = n;
    }
    num /= 10;
  }

  /* Если число не вместилось, сигнализируем об ошибке */
  return (num != 0 || negative == true ? false : true);
}

/******************************************************************************
 * Вывод числа с фиксированной запятой на индикатор
 *  num - выводимое число;
 *  decimals - кол-во знаков после запятой;
 *  begin, end - начальный и конечный индикаторы для вывода числа (от 0 до 3);
 *  space - заполняющий символ вместо пробелов.
 */
bool show_fix(int num, uint8_t decimals, uint8_t begin, uint8_t end, uint8_t space)
{
  return show_fix_to(g_indicator, num, decimals, begin, end, space);
}

/******************************************************************************
 * Вывод целого числа в память
 *  mem - адрес памяти;
 *  num - выводимое число;
 *  begin, end - начальный и конечный индикаторы для вывода числа (от 0 до 3);
 *  space - заполняющий символ вместо пробелов.
 */
bool show_int_to(uint8_t *mem, int num, uint8_t begin, uint8_t end, uint8_t space)
{
  return show_fix_to(mem, num, 0, begin, end, space);
}

/******************************************************************************
 * Вывод целого числа на индикатор
 *  num - выводимое число;
 *  begin, end - начальный и конечный индикаторы для вывода числа (от 0 до 3);
 *  space - заполняющий символ вместо пробелов.
 */
bool show_int(int num, uint8_t begin, uint8_t end, uint8_t space)
{
  return show_fix_to(g_indicator, num, 0, begin, end, space);
}

/******************************************************************************
 * Вспомогательная функция для анимации - "отправляем" знак вверх
 * Для следующего шага надо заново запустить функцию, передав ей полученный
 * результат. Всего шагов - 2. Третий шаг приведёт к пустоте.
 */
uint8_t anim_send_up(uint8_t digit)
{
  uint8_t res = 0;

  /* Точка просто исчезает */
  if (digit & 0b00100000) res |= 0b10000000;
  if (digit & 0b00010000) res |= 0b00000010;
  if (digit & 0b00000100) res |= 0b00000001;
  if (digit & 0b00000010) res |= 0b01000000;

  return res;
}

/******************************************************************************
 * Вспомогательная функция для анимации - "принимаем" знак снизу
 * digit - знак;
 * step - номер шага (0 - пусто; 1,2 - промежуточные шаги; 3 - сам знак).
 */
uint8_t anim_take_from_bottom(uint8_t digit, uint8_t step)
{
  uint8_t res = 0;
  
  /* F-A-E-D-Dp-C-G-B
   *     A(6)
   *  F(7)  B(0)
   *     G(1)
   *  E(5)  C(2)   
   *     D(4)  Dp(3)
   */
  switch (step) {
    case 0:
      res = 0;
      break;
      
    case 1:
      if (digit & 0b01000000) res |= 0b00010000;
      break;

    case 2:
      if (digit & 0b10000000) res |= 0b00100000;
      if (digit & 0b01000000) res |= 0b00000010;
      if (digit & 0b00000010) res |= 0b00010000;
      if (digit & 0b00000001) res |= 0b00000100;
      break;

    default:
      res = digit;
  }

  return res;
}

/******************************************************************************
 * Вспомогательная функция для анимации - "отправляем" знак вниз
 * Для следующего шага надо заново запустить функцию, передав ей полученный
 * результат. Всего шагов - 2. Третий шаг приведёт к пустоте.
 */
uint8_t anim_send_down(uint8_t digit)
{
  uint8_t res = 0;

  /* Точка просто исчезает */
  if (digit & 0b10000000) res |= 0b00100000;
  if (digit & 0b01000000) res |= 0b00000010;
  if (digit & 0b00000010) res |= 0b00010000;
  if (digit & 0b00000001) res |= 0b00000100;

  return res;
}

/******************************************************************************
 * Вспомогательная функция для анимации - "принимаем" знак сверху
 * digit - знак;
 * step - номер шага (0 - пусто; 1,2 - промежуточные шаги; 3 - сам знак).
 */
uint8_t anim_take_from_above(uint8_t digit, uint8_t step)
{
  uint8_t res = 0;
  
  /* F-A-E-D-Dp-C-G-B
   *     A(6)
   *  F(7)  B(0)
   *     G(1)
   *  E(5)  C(2)   
   *     D(4)  Dp(3)
   */
  switch (step) {
    case 0:
      res = 0;
      break;
      
    case 1:
      if (digit & 0b00010000) res |= 0b01000000;
      break;

    case 2:
      if (digit & 0b00100000) res |= 0b10000000;
      if (digit & 0b00010000) res |= 0b00000010;
      if (digit & 0b00000100) res |= 0b00000001;
      if (digit & 0b00000010) res |= 0b01000000;
      break;

    default:
      res = digit;
  }

  return res;
}

/******************************************************************************
 * Анимация
 * mem - массив новые значений индикатора;
 * anim_type - вид анимации;
 * step_delay - задержка между шагами анимации.
 */
void anim(uint8_t *mem, anim_t anim_type, uint16_t step_delay, int8_t brightness)
{
  switch (anim_type) {
    case ANIM_GOLEFT:
      {
        /* Уходим */
        for (int8_t i = 0; i < 4; i++) {
          g_indicator[3] = g_indicator[2];
          g_indicator[2] = g_indicator[1];
          g_indicator[1] = g_indicator[0];
          g_indicator[0] = 0;
          delay(step_delay);
          /* Как только экран очистился, переходим к следующему этапу */
          if (g_indicator[0] == 0 && g_indicator[1] == 0 && g_indicator[2] == 0 && g_indicator[3] == 0) break;
        }

        /* Ищем последний значимый символ в новом значении */
        int8_t last_mem = -1;
        for (int i = 3; i >= 0; i--) {
          if (mem[i] != 0) {
            last_mem = i;
            break;
          }
        }
         
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);
        
        for (int8_t i = 0; i < 4; i++) {
          for (int8_t j = 0; j <= i; j++) {
            int8_t index = last_mem - i + j;
            g_indicator[j] = (index < 0 ? 0 : mem[index]);
          }
          delay(step_delay);
        }
      }
      break;

    case ANIM_GORIGHT:
      {      
        /* Уходим */
        for (int i = 0; i < 4; i++) {
          g_indicator[0] = g_indicator[1];
          g_indicator[1] = g_indicator[2];
          g_indicator[2] = g_indicator[3];
          g_indicator[3] = 0;
          delay(step_delay);
          if (g_indicator[0] == 0 && g_indicator[1] == 0 && g_indicator[2] == 0 && g_indicator[3] == 0) break;
        }

        /* Ищем первый значимый символ в новом значении */
        int8_t first_mem = 4;
        for (int i = 0; i <= 3; i++) {
          if (mem[i] != 0) {
            first_mem = i;
            break;
          }
        }

        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 0; i < 4 - first_mem; i++) {
          for (int j = 0; j <= i; j++) {
            g_indicator[3 - i + j] = mem[first_mem + j];
          }
          delay(step_delay);
        }
      }
      break;
    
    case ANIM_GODOWN:
      {
        /* Уходим */
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 4; j++) {
            g_indicator[j] = anim_send_up(g_indicator[j]);
          }
          delay(step_delay);
        }
        
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 1; i <= 3; i++) {
          for (int j = 0; j < 4; j++) {
            g_indicator[j] = anim_take_from_bottom(mem[j], i);
          }
          delay(step_delay);
        }
      }
      break;
      
   case ANIM_GOUP:
      {
        /* Уходим */
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 4; j++) {
            g_indicator[j] = anim_send_down(g_indicator[j]);
          }
          delay(step_delay);
        }
      
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 1; i <= 3; i++) {
          for (int j = 0; j < 4; j++) {
            g_indicator[j] = anim_take_from_above(mem[j], i);
          }
          delay(step_delay);
        }
      }
      break;
    
    default:
        for (int i = 0; i < 4; i++) {
          g_indicator[i] = mem[i];
        }
  } /* switch (anim_type) */
}

